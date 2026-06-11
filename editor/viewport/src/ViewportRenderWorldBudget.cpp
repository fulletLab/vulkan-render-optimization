#include "ViewportRenderWorldBudget.hpp"

#include "ViewportMeshLod.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace projectunity::editor {
namespace {

struct LodBudgetCandidate {
    std::size_t drawIndex {0};
    std::uint32_t lodIndex {0};
    std::uint64_t currentTriangles {0};
    std::uint64_t budgetTriangles {0};
    float priority {0.0F};
};

[[nodiscard]] std::uint64_t currentVisibleTriangles(const std::vector<renderer::RenderMeshDraw>& meshDraws)
{
    std::uint64_t total = 0;
    for (const auto& draw : meshDraws) {
        if (draw.primitive != nullptr) {
            total += indexCountForViewportLod(*draw.primitive, draw.lodIndex) / 3U;
        }
    }
    return total;
}

[[nodiscard]] std::uint64_t viewportTriangleBudget(
    std::uint64_t visibleTriangles,
    std::uint64_t candidateTriangles) noexcept
{
    (void)candidateTriangles;
    return visibleTriangles;
}

[[nodiscard]] std::uint32_t coarserViewportLod(const assets::MeshPrimitive& primitive, std::uint32_t currentLod) noexcept
{
    const auto currentCount = indexCountForViewportLod(primitive, currentLod);
    const auto available = static_cast<std::uint32_t>(std::min<std::size_t>(primitive.lods.size(), std::numeric_limits<std::uint32_t>::max()));
    for (std::uint32_t candidate = available; candidate > currentLod; --candidate) {
        if (indexCountForViewportLod(primitive, candidate) < currentCount) {
            return candidate;
        }
    }
    return currentLod;
}

[[nodiscard]] float projectedRadiusPixels(
    const renderer::RenderMeshDraw& draw,
    const ViewportRenderWorldCamera& camera,
    int viewportHeight) noexcept
{
    if (draw.worldBoundsRadius <= 0.0F || draw.sortDepth <= 0.05F || viewportHeight <= 0) {
        return std::numeric_limits<float>::max();
    }
    const auto projectionScale = (static_cast<float>(viewportHeight) * 0.5F)
        / std::max(std::tan(camera.verticalFovRadians * 0.5F), 0.001F);
    const auto projected = draw.worldBoundsRadius * projectionScale / std::max(draw.sortDepth, 0.05F);
    return std::isfinite(projected) ? projected : std::numeric_limits<float>::max();
}

} // namespace

void applyViewportTriangleBudget(
    std::vector<renderer::RenderMeshDraw>& meshDraws,
    scene::EntityId selectedEntityId,
    const ViewportRenderWorldCamera& camera,
    int viewportHeight,
    ViewportRenderWorldStats& stats)
{
    auto visibleTriangles = currentVisibleTriangles(meshDraws);
    const auto budget = viewportTriangleBudget(visibleTriangles, stats.candidateTriangleCount);
    if (visibleTriangles <= budget) {
        return;
    }

    const auto selectedDrawCount = selectedEntityId.isValid()
        ? static_cast<std::size_t>(std::count_if(meshDraws.begin(), meshDraws.end(), [selectedEntityId](const auto& draw) {
            return draw.sceneNodeId == selectedEntityId.value();
        }))
        : 0U;
    const auto pinSelectedDraws = selectedDrawCount > 0U && selectedDrawCount <= 16U;

    std::vector<LodBudgetCandidate> candidates;
    candidates.reserve(meshDraws.size());
    for (std::size_t index = 0; index < meshDraws.size(); ++index) {
        const auto& draw = meshDraws[index];
        if (draw.primitive == nullptr || draw.primitive->lods.empty()) {
            continue;
        }
        if (pinSelectedDraws && draw.sceneNodeId == selectedEntityId.value()) {
            continue;
        }
        const auto nextLod = coarserViewportLod(*draw.primitive, draw.lodIndex);
        if (nextLod == draw.lodIndex) {
            continue;
        }
        const auto currentTriangles = static_cast<std::uint64_t>(indexCountForViewportLod(*draw.primitive, draw.lodIndex) / 3U);
        const auto budgetTriangles = static_cast<std::uint64_t>(indexCountForViewportLod(*draw.primitive, nextLod) / 3U);
        if (budgetTriangles >= currentTriangles) {
            continue;
        }
        const auto projectedRadius = projectedRadiusPixels(draw, camera, viewportHeight);
        const auto savings = static_cast<float>(currentTriangles - budgetTriangles);
        const auto nearPenalty = draw.sortDepth <= draw.worldBoundsRadius * 1.10F && projectedRadius > static_cast<float>(viewportHeight) * 0.75F
            ? 100'000.0F
            : 0.0F;
        const auto priority = draw.sortDepth * 4.0F + savings * 0.002F - projectedRadius * 0.35F - nearPenalty;
        candidates.push_back({index, nextLod, currentTriangles, budgetTriangles, priority});
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.priority > rhs.priority;
    });

    for (const auto& candidate : candidates) {
        if (visibleTriangles <= budget || candidate.drawIndex >= meshDraws.size()) {
            break;
        }
        auto& draw = meshDraws[candidate.drawIndex];
        if (draw.lodIndex == 0U && candidate.lodIndex > 0U) {
            ++stats.lodMeshDrawCount;
        }
        draw.lodIndex = candidate.lodIndex;
        const auto saved = candidate.currentTriangles - candidate.budgetTriangles;
        stats.lodTriangleReductionCount += saved;
        visibleTriangles = visibleTriangles > saved ? visibleTriangles - saved : 0U;
    }
}

} // namespace projectunity::editor
