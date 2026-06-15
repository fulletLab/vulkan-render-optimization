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
    math::Vec3 /*forward*/,
    const std::array<math::Vec3, 8>& boundsCorners,
    float nearPlane) noexcept
{
    const auto minimumDistance = std::max(nearPlane, 0.001F);
    auto minimum = boundsCorners.front();
    auto maximum = boundsCorners.front();
    for (const auto corner : boundsCorners) {
        if (!std::isfinite(corner.x) || !std::isfinite(corner.y) || !std::isfinite(corner.z)) {
            return minimumDistance;
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
    const auto nearestFaceDistance = std::min({
        eye.x - minimum.x,
        maximum.x - eye.x,
        eye.y - minimum.y,
        maximum.y - eye.y,
        eye.z - minimum.z,
        maximum.z - eye.z,
    });
    return std::isfinite(nearestFaceDistance)
        ? std::max(nearestFaceDistance, minimumDistance)
        : minimumDistance;
}

std::size_t indexCountForViewportLod(const assets::MeshPrimitive& primitive, std::uint32_t lodIndex)
{
    if (lodIndex == 0U || lodIndex - 1U >= primitive.lods.size()) {
        return primitive.indices.size();
    }
    const auto& indices = primitive.lods[lodIndex - 1U].indices;
    return indices.empty() ? primitive.indices.size() : indices.size();
}

ViewportMeshLodSelection evaluateViewportMeshLod(
    const assets::MeshPrimitive& primitive,
    float boundsRadius,
    float distanceToCenter,
    float verticalFovRadians,
    float viewportHeight,
    bool forceFullResolution,
    float lodBias,
    std::optional<std::uint32_t> previousLodIndex,
    float hysteresisRatio) noexcept
{
    ViewportMeshLodSelection result;
    result.hadPreviousSelection = previousLodIndex.has_value();
    result.previousLodIndex = previousLodIndex.value_or(0U);
    if (forceFullResolution
        || primitive.lods.empty()
        || primitive.indices.empty()
        || primitive.bounds.radius <= 0.0F
        || !std::isfinite(primitive.bounds.radius)
        || !validProjectionInputs(boundsRadius, distanceToCenter, verticalFovRadians, viewportHeight)) {
        return result;
    }

    const auto tangent = std::tan(verticalFovRadians * 0.5F);
    if (!std::isfinite(tangent) || tangent <= 0.0F) {
        return result;
    }

    const auto worldScale = boundsRadius / primitive.bounds.radius;
    const auto projectionScale = (viewportHeight * 0.5F) / tangent;
    if (!std::isfinite(worldScale) || worldScale <= 0.0F
        || !std::isfinite(projectionScale) || projectionScale <= 0.0F) {
        return result;
    }

    const auto maximumErrorPixels = kMaximumLodErrorPixels * std::clamp(lodBias, 0.25F, 8.0F);
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

        const auto projectedErrorPixels = lod.error * worldScale * projectionScale / distanceToCenter;
        if (!std::isfinite(projectedErrorPixels) || projectedErrorPixels > maximumErrorPixels) {
            continue;
        }

        selectedLod = static_cast<std::uint32_t>(lodOffset + 1U);
        selectedIndexCount = lod.indices.size();
    }
    result.lodIndex = selectedLod;
    result.reason = selectedLod == 0U ? ViewportMeshLodReason::FullResolution : ViewportMeshLodReason::ScreenError;
    if (selectedLod > 0U) {
        result.projectedErrorPixels = primitive.lods[selectedLod - 1U].error * worldScale * projectionScale / distanceToCenter;
    }
    const auto previous = previousLodIndex.value_or(selectedLod);
    if (!previousLodIndex.has_value() || previous > primitive.lods.size() || previous == selectedLod) {
        return result;
    }
    const auto previousError = previous == 0U ? std::numeric_limits<float>::max()
        : primitive.lods[previous - 1U].error * worldScale * projectionScale / distanceToCenter;
    const auto hysteresis = std::clamp(hysteresisRatio, 0.0F, 0.45F);
    const auto holdPrevious = selectedLod > previous
        ? result.lodIndex > 0U
            && primitive.lods[result.lodIndex - 1U].error * worldScale * projectionScale / distanceToCenter
                > maximumErrorPixels * (1.0F - hysteresis)
        : std::isfinite(previousError) && previousError <= maximumErrorPixels * (1.0F + hysteresis);
    if (holdPrevious) {
        result.lodIndex = previous;
        result.projectedErrorPixels = std::isfinite(previousError) ? previousError : 0.0F;
        result.hysteresisActive = true;
        result.reason = ViewportMeshLodReason::HysteresisHold;
        return result;
    }
    return result;
}

std::uint32_t selectViewportMeshLod(
    const assets::MeshPrimitive& primitive,
    float boundsRadius,
    float depth,
    float verticalFovRadians,
    float viewportHeight,
    bool forceFullResolution,
    float lodBias) noexcept
{
    return evaluateViewportMeshLod(
        primitive,
        boundsRadius,
        depth,
        verticalFovRadians,
        viewportHeight,
        forceFullResolution,
        lodBias).lodIndex;
}

} // namespace projectunity::editor
