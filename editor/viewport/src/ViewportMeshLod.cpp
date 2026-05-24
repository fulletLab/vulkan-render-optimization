#include "ViewportMeshLod.hpp"

#include <algorithm>
#include <cmath>

namespace projectunity::editor {

std::size_t indexCountForViewportLod(const assets::MeshPrimitive& primitive, std::uint32_t lodIndex)
{
    if (lodIndex == 0U || lodIndex - 1U >= primitive.lods.size()) {
        return primitive.indices.size();
    }
    const auto& indices = primitive.lods[lodIndex - 1U].indices;
    return indices.empty() ? primitive.indices.size() : indices.size();
}

std::uint32_t selectViewportMeshLod(
    const assets::MeshPrimitive& primitive,
    float boundsRadius,
    float depth,
    float verticalFovRadians,
    float viewportHeight) noexcept
{
    if (primitive.lods.empty()
        || boundsRadius <= 0.0F
        || depth <= 0.05F
        || viewportHeight < 1.0F
        || !std::isfinite(boundsRadius)
        || !std::isfinite(depth)) {
        return 0U;
    }
    const auto projectionScale = (viewportHeight * 0.5F) / std::max(std::tan(verticalFovRadians * 0.5F), 0.001F);
    const auto projectedRadius = boundsRadius * projectionScale / std::max(depth, 0.05F);
    if (!std::isfinite(projectedRadius)) {
        return 0U;
    }
    const auto available = static_cast<std::uint32_t>(std::min<std::size_t>(primitive.lods.size(), 6U));
    std::uint32_t requested = 0U;
    if (projectedRadius < 56.0F) {
        requested = available;
    } else if (projectedRadius < 112.0F) {
        requested = std::min<std::uint32_t>(4U, available);
    } else if (projectedRadius < 220.0F) {
        requested = std::min<std::uint32_t>(2U, available);
    } else if (projectedRadius < 420.0F) {
        requested = std::min<std::uint32_t>(1U, available);
    }
    const auto sourceTriangles = primitive.indices.size() / 3U;
    if (sourceTriangles > 4096U && depth > boundsRadius * 1.20F) {
        const auto projectedArea = std::clamp(
            3.14159265F * projectedRadius * projectedRadius,
            1.0F,
            viewportHeight * viewportHeight * 1.85F);
        const auto desiredTriangles = std::clamp(projectedArea * 0.55F, 1024.0F, static_cast<float>(sourceTriangles));
        const auto minimumAcceptableTriangles = desiredTriangles * 0.42F;
        for (std::uint32_t candidate = available; candidate > 0U; --candidate) {
            const auto candidateTriangles = static_cast<float>(indexCountForViewportLod(primitive, candidate) / 3U);
            if (candidateTriangles >= minimumAcceptableTriangles
                && candidateTriangles < static_cast<float>(sourceTriangles)) {
                requested = std::max(requested, candidate);
                break;
            }
        }
    }
    while (requested > 0U && indexCountForViewportLod(primitive, requested) >= primitive.indices.size()) {
        --requested;
    }
    return requested;
}

} // namespace projectunity::editor
