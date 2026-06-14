#include "ViewportMeshLod.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

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

float viewportLodDistanceToBounds(
    math::Vec3 eye,
    math::Vec3 forward,
    const std::array<math::Vec3, 8>& boundsCorners,
    float nearPlane) noexcept
{
    const auto minimumDistance = std::max(nearPlane, 0.001F);
    const auto viewDirection = forward.normalized();
    if (!std::isfinite(viewDirection.x)
        || !std::isfinite(viewDirection.y)
        || !std::isfinite(viewDirection.z)
        || viewDirection.lengthSquared() <= 0.0F) {
        return minimumDistance;
    }
    auto minimum = boundsCorners.front();
    auto maximum = boundsCorners.front();
    auto nearestForwardDepth = std::numeric_limits<float>::max();
    for (const auto corner : boundsCorners) {
        if (!std::isfinite(corner.x) || !std::isfinite(corner.y) || !std::isfinite(corner.z)) {
            return minimumDistance;
        }
        const auto depth = math::dot(corner - eye, viewDirection);
        if (depth > minimumDistance) {
            nearestForwardDepth = std::min(nearestForwardDepth, depth);
        }
        minimum.x = std::min(minimum.x, corner.x);
        minimum.y = std::min(minimum.y, corner.y);
        minimum.z = std::min(minimum.z, corner.z);
        maximum.x = std::max(maximum.x, corner.x);
        maximum.y = std::max(maximum.y, corner.y);
        maximum.z = std::max(maximum.z, corner.z);
    }
    const auto axisDistance = [](float value, float axisMinimum, float axisMaximum) noexcept {
        if (value < axisMinimum) {
            return axisMinimum - value;
        }
        return value > axisMaximum ? value - axisMaximum : 0.0F;
    };
    const math::Vec3 distance {
        axisDistance(eye.x, minimum.x, maximum.x),
        axisDistance(eye.y, minimum.y, maximum.y),
        axisDistance(eye.z, minimum.z, maximum.z),
    };
    const auto closestDistance = distance.length();
    if (!std::isfinite(closestDistance)) {
        return minimumDistance;
    }
    if (closestDistance > minimumDistance) {
        return closestDistance;
    }
    if (nearestForwardDepth != std::numeric_limits<float>::max()
        && std::isfinite(nearestForwardDepth)) {
        return std::max(nearestForwardDepth, minimumDistance);
    }
    return minimumDistance;
}

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
        || !validProjectionInputs(boundsRadius, depth, verticalFovRadians, viewportHeight)) {
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
