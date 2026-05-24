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
    float viewportHeight,
    bool forceFullResolution) noexcept
{
    if (forceFullResolution
        || primitive.lods.empty()
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
    const auto available = static_cast<std::uint32_t>(std::min<std::size_t>(primitive.lods.size(), 8U));
    std::uint32_t requested = 0U;
    const auto sourceTriangles = primitive.indices.size() / 3U;
    const auto distanceRatio = depth / std::max(boundsRadius, 0.001F);
    const auto worldUnitsPerPixel = std::max(depth - boundsRadius, 0.0F)
        * (std::tan(verticalFovRadians * 0.5F) * 2.0F)
        / viewportHeight;
    if (worldUnitsPerPixel > 0.0F && std::isfinite(worldUnitsPerPixel)) {
        const auto lodFactor = sourceTriangles > 8192U ? 2.5F : 1.5F;
        for (std::uint32_t candidate = available; candidate > 0U; --candidate) {
            const auto error = primitive.lods[candidate - 1U].error;
            if (error > 0.0F
                && std::isfinite(error)
                && error <= worldUnitsPerPixel * lodFactor
                && indexCountForViewportLod(primitive, candidate) < primitive.indices.size()) {
                requested = candidate;
                break;
            }
        }
    }
    if (projectedRadius < 56.0F) {
        requested = available;
    } else if (projectedRadius < 112.0F) {
        requested = std::min<std::uint32_t>(4U, available);
    } else if (projectedRadius < 220.0F) {
        requested = std::min<std::uint32_t>(2U, available);
    } else if (projectedRadius < 420.0F) {
        requested = std::min<std::uint32_t>(1U, available);
    }
    if (sourceTriangles > 8192U && depth > boundsRadius * 2.5F) {
        requested = std::max<std::uint32_t>(requested, std::min<std::uint32_t>(1U, available));
    }
    if (sourceTriangles > 8192U && depth > boundsRadius * 4.0F) {
        requested = std::max<std::uint32_t>(requested, std::min<std::uint32_t>(2U, available));
    }
    if (sourceTriangles > 8192U && depth > boundsRadius * 8.0F) {
        requested = std::max<std::uint32_t>(requested, std::min<std::uint32_t>(4U, available));
    }
    if (sourceTriangles > 8192U && distanceRatio > 1.4F) {
        if (projectedRadius < 900.0F) {
            requested = std::max<std::uint32_t>(requested, std::min<std::uint32_t>(3U, available));
        }
        if (projectedRadius < 650.0F) {
            requested = std::max<std::uint32_t>(requested, std::min<std::uint32_t>(4U, available));
        }
        if (projectedRadius < 420.0F) {
            requested = std::max<std::uint32_t>(requested, std::min<std::uint32_t>(5U, available));
        }
        if (projectedRadius < 260.0F) {
            requested = std::max<std::uint32_t>(requested, std::min<std::uint32_t>(6U, available));
        }
        if (projectedRadius < 150.0F || distanceRatio > 10.0F) {
            requested = std::max(requested, available);
        }
    }
    if (sourceTriangles > 32768U && distanceRatio > 2.0F) {
        requested = std::max<std::uint32_t>(requested, std::min<std::uint32_t>(4U, available));
    }
    if (sourceTriangles > 32768U && distanceRatio > 4.0F) {
        requested = std::max<std::uint32_t>(requested, std::min<std::uint32_t>(6U, available));
    }
    if (sourceTriangles > 32768U && distanceRatio > 8.0F) {
        requested = std::max(requested, available);
    }
    if (sourceTriangles > 2048U && depth > boundsRadius * 1.05F) {
        const auto projectedArea = std::clamp(
            3.14159265F * projectedRadius * projectedRadius,
            1.0F,
            viewportHeight * viewportHeight * 1.25F);
        const auto density = sourceTriangles > 8192U ? 0.02F : 0.05F;
        const auto desiredTriangles = std::clamp(projectedArea * density, 128.0F, static_cast<float>(sourceTriangles));
        const auto minimumAcceptableTriangles = desiredTriangles * 0.10F;
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
