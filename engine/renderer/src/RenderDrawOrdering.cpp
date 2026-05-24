#include <projectunity/renderer/RenderDrawOrdering.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <tuple>

namespace projectunity::renderer {
namespace {

[[nodiscard]] float finiteSortDepth(float depth) noexcept
{
    return std::isfinite(depth) ? depth : -std::numeric_limits<float>::infinity();
}

[[nodiscard]] std::uintptr_t pointerKey(const void* pointer) noexcept
{
    return reinterpret_cast<std::uintptr_t>(pointer);
}

[[nodiscard]] auto batchKey(const RenderMeshDraw& draw) noexcept
{
    return std::tuple {
        draw.modelAssetId.value(),
        draw.primitiveIndex,
        draw.lodIndex,
        pointerKey(draw.material),
        pointerKey(draw.baseColorTexture),
        pointerKey(draw.normalTexture),
        pointerKey(draw.metallicRoughnessTexture),
        pointerKey(draw.occlusionTexture),
        pointerKey(draw.emissiveTexture),
        draw.flipsWinding,
    };
}

} // namespace

bool isTransparentMeshDraw(const RenderMeshDraw& draw) noexcept
{
    return draw.material != nullptr && draw.material->alphaMode == assets::MaterialAlphaMode::Blend;
}

void orderMeshDraws(std::span<const RenderMeshDraw> draws, std::vector<const RenderMeshDraw*>& ordered)
{
    ordered.clear();
    ordered.reserve(draws.size());
    for (const auto& draw : draws) {
        ordered.push_back(&draw);
    }

    std::stable_sort(
        ordered.begin(),
        ordered.end(),
        [](const RenderMeshDraw* lhs, const RenderMeshDraw* rhs) {
            const auto lhsTransparent = isTransparentMeshDraw(*lhs);
            const auto rhsTransparent = isTransparentMeshDraw(*rhs);
            if (lhsTransparent != rhsTransparent) {
                return !lhsTransparent;
            }
            if (!lhsTransparent) {
                return batchKey(*lhs) < batchKey(*rhs);
            }
            const auto lhsDepth = finiteSortDepth(lhs->sortDepth);
            const auto rhsDepth = finiteSortDepth(rhs->sortDepth);
            if (lhsDepth != rhsDepth) {
                return lhsDepth > rhsDepth;
            }
            return batchKey(*lhs) < batchKey(*rhs);
        });
}

} // namespace projectunity::renderer
