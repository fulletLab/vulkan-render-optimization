#include <projectunity/renderer/RenderDrawOrdering.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace projectunity::renderer {
namespace {

[[nodiscard]] float finiteSortDepth(float depth) noexcept
{
    return std::isfinite(depth) ? depth : -std::numeric_limits<float>::infinity();
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
            return lhsTransparent && finiteSortDepth(lhs->sortDepth) > finiteSortDepth(rhs->sortDepth);
        });
}

} // namespace projectunity::renderer
