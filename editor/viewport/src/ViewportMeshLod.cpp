#include "ViewportMeshLod.hpp"

#include <algorithm>
#include <cmath>

namespace projectunity::editor {
namespace {

constexpr float kMaximumLodErrorPixels = 1.25F;

[[nodiscard]] bool validProjectionInputs(
    float boundsRadius,
    float depth,
    float verticalFovRadians,
    float viewportHeight) noexcept
{
    return std::isfinite(boundsRadius)
        && std::isfinite(depth)
        && std::isfinite(verticalFovRadians)
        && std::isfinite(viewportHeight)
        && boundsRadius > 0.0F
        && depth > 0.0F
        && verticalFovRadians > 0.0F
        && verticalFovRadians < 3.14159265F
        && viewportHeight > 0.0F;
}

} // namespace

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
        || primitive.indices.empty()
        || primitive.bounds.radius <= 0.0F
        || !std::isfinite(primitive.bounds.radius)
        || !validProjectionInputs(boundsRadius, depth, verticalFovRadians, viewportHeight)
        || depth <= boundsRadius) {
        return 0U;
    }

    const auto tangent = std::tan(verticalFovRadians * 0.5F);
    if (!std::isfinite(tangent) || tangent <= 0.0F) {
        return 0U;
    }

    const auto worldScale = boundsRadius / primitive.bounds.radius;
    const auto projectionScale = (viewportHeight * 0.5F) / tangent;
    if (!std::isfinite(worldScale) || worldScale <= 0.0F
        || !std::isfinite(projectionScale) || projectionScale <= 0.0F) {
        return 0U;
    }

    auto selectedLod = 0U;
    auto selectedIndexCount = primitive.indices.size();
    for (std::size_t lodOffset = 0; lodOffset < primitive.lods.size(); ++lodOffset) {
        const auto& lod = primitive.lods[lodOffset];
        if (lod.indices.empty()
            || lod.indices.size() >= selectedIndexCount
            || !std::isfinite(lod.error)
            || lod.error <= 0.0F) {
            continue;
        }

        const auto projectedErrorPixels = lod.error * worldScale * projectionScale / depth;
        if (!std::isfinite(projectedErrorPixels) || projectedErrorPixels > kMaximumLodErrorPixels) {
            continue;
        }

        selectedLod = static_cast<std::uint32_t>(lodOffset + 1U);
        selectedIndexCount = lod.indices.size();
    }
    return selectedLod;
}

} // namespace projectunity::editor
