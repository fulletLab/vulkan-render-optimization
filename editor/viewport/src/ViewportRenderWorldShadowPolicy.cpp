#include "ViewportRenderWorldShadowPolicy.hpp"

#include <projectunity/assets/AssetManager.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace projectunity::editor {
namespace {

[[nodiscard]] float projectedRadiusPixels(
    float radius,
    float depth,
    float verticalFovRadians,
    int viewportHeight) noexcept
{
    if (radius <= 0.0F || depth <= 0.05F || viewportHeight <= 0 || !std::isfinite(radius) || !std::isfinite(depth)) {
        return 0.0F;
    }
    const auto projectionScale = (static_cast<float>(viewportHeight) * 0.5F)
        / std::max(std::tan(verticalFovRadians * 0.5F), 0.001F);
    const auto projected = radius * projectionScale / std::max(depth, 0.05F);
    return std::isfinite(projected) ? projected : 0.0F;
}

} // namespace

void applyViewportShadowPolicy(
    std::vector<renderer::RenderMeshDraw>& meshDraws,
    scene::EntityId selectedEntityId,
    const ViewportRenderWorldCamera& camera,
    int viewportHeight)
{
    constexpr std::size_t kLargeSceneDrawThreshold = 1024;
    constexpr std::size_t kLargeSceneShadowBudget = 512;
    if (meshDraws.size() <= kLargeSceneDrawThreshold) {
        return;
    }

    struct Candidate {
        std::size_t index {0};
        float score {0.0F};
        bool pinned {false};
    };

    std::vector<Candidate> candidates;
    candidates.reserve(meshDraws.size());
    for (std::size_t index = 0; index < meshDraws.size(); ++index) {
        auto& draw = meshDraws[index];
        draw.castsShadow = true;
        if (draw.material != nullptr && draw.material->alphaMode == assets::MaterialAlphaMode::Blend) {
            draw.castsShadow = false;
            continue;
        }
        const auto pinned = selectedEntityId.isValid() && draw.sceneNodeId == selectedEntityId.value();
        const auto projectedRadius = projectedRadiusPixels(
            draw.worldBoundsRadius,
            draw.sortDepth,
            camera.verticalFovRadians,
            viewportHeight);
        if (!pinned
            && (projectedRadius < 8.0F
                || (draw.lodIndex >= 4U && projectedRadius < 96.0F)
                || (draw.sortDepth > draw.worldBoundsRadius * 12.0F && projectedRadius < 80.0F))) {
            draw.castsShadow = false;
            continue;
        }
        const auto lodBonus = draw.lodIndex == 0U ? 64.0F : 0.0F;
        const auto score = pinned
            ? std::numeric_limits<float>::max()
            : projectedRadius * 8.0F + draw.worldBoundsRadius * 0.5F + lodBonus;
        candidates.push_back({index, score, pinned});
    }
    if (candidates.size() <= kLargeSceneShadowBudget) {
        return;
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& lhs, const Candidate& rhs) {
        return lhs.score > rhs.score;
    });
    for (const auto& candidate : candidates) {
        if (!candidate.pinned) {
            meshDraws[candidate.index].castsShadow = false;
        }
    }
    std::size_t kept = 0;
    for (const auto& candidate : candidates) {
        auto& draw = meshDraws[candidate.index];
        if (candidate.pinned || kept < kLargeSceneShadowBudget) {
            draw.castsShadow = true;
            if (!candidate.pinned) {
                ++kept;
            }
        }
    }
}

} // namespace projectunity::editor
