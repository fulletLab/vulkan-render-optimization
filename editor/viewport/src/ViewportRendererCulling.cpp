#include "ViewportRendererCulling.hpp"

#include <algorithm>
#include <cmath>

namespace projectunity::editor {
namespace {

[[nodiscard]] float matrixAt(const renderer::RenderMatrix4& matrix, int row, int column)
{
    return matrix.values[static_cast<std::size_t>(column * 4 + row)];
}

[[nodiscard]] math::Vec3 transformMatrixPoint(const renderer::RenderMatrix4& matrix, math::Vec3 point)
{
    return {
        matrixAt(matrix, 0, 0) * point.x + matrixAt(matrix, 0, 1) * point.y + matrixAt(matrix, 0, 2) * point.z + matrixAt(matrix, 0, 3),
        matrixAt(matrix, 1, 0) * point.x + matrixAt(matrix, 1, 1) * point.y + matrixAt(matrix, 1, 2) * point.z + matrixAt(matrix, 1, 3),
        matrixAt(matrix, 2, 0) * point.x + matrixAt(matrix, 2, 1) * point.y + matrixAt(matrix, 2, 2) * point.z + matrixAt(matrix, 2, 3),
    };
}

[[nodiscard]] bool nearVec3(math::Vec3 lhs, math::Vec3 rhs, float epsilon = 0.0005F) noexcept
{
    return std::fabs(lhs.x - rhs.x) <= epsilon
        && std::fabs(lhs.y - rhs.y) <= epsilon
        && std::fabs(lhs.z - rhs.z) <= epsilon;
}

[[nodiscard]] bool finiteVec3(math::Vec3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool pointInsideBounds(math::Vec3 point, const ViewportWorldBounds& bounds, float padding) noexcept
{
    if (!finiteVec3(point) || bounds.corners.empty()) {
        return false;
    }
    auto minimum = bounds.corners.front();
    auto maximum = bounds.corners.front();
    for (const auto corner : bounds.corners) {
        if (!finiteVec3(corner)) {
            return false;
        }
        minimum.x = std::min(minimum.x, corner.x);
        minimum.y = std::min(minimum.y, corner.y);
        minimum.z = std::min(minimum.z, corner.z);
        maximum.x = std::max(maximum.x, corner.x);
        maximum.y = std::max(maximum.y, corner.y);
        maximum.z = std::max(maximum.z, corner.z);
    }
    const auto safePadding = std::max(padding, 0.0F);
    return point.x >= minimum.x - safePadding
        && point.x <= maximum.x + safePadding
        && point.y >= minimum.y - safePadding
        && point.y <= maximum.y + safePadding
        && point.z >= minimum.z - safePadding
        && point.z <= maximum.z + safePadding;
}

} // namespace

ViewportWorldBounds transformViewportBounds(const renderer::RenderMatrix4& matrix, const assets::MeshBounds& bounds)
{
    ViewportWorldBounds result;
    result.corners = {{
        transformMatrixPoint(matrix, {bounds.minimum.x, bounds.minimum.y, bounds.minimum.z}),
        transformMatrixPoint(matrix, {bounds.maximum.x, bounds.minimum.y, bounds.minimum.z}),
        transformMatrixPoint(matrix, {bounds.minimum.x, bounds.maximum.y, bounds.minimum.z}),
        transformMatrixPoint(matrix, {bounds.maximum.x, bounds.maximum.y, bounds.minimum.z}),
        transformMatrixPoint(matrix, {bounds.minimum.x, bounds.minimum.y, bounds.maximum.z}),
        transformMatrixPoint(matrix, {bounds.maximum.x, bounds.minimum.y, bounds.maximum.z}),
        transformMatrixPoint(matrix, {bounds.minimum.x, bounds.maximum.y, bounds.maximum.z}),
        transformMatrixPoint(matrix, {bounds.maximum.x, bounds.maximum.y, bounds.maximum.z}),
    }};
    auto minimum = result.corners.front();
    auto maximum = result.corners.front();
    for (const auto corner : result.corners) {
        minimum.x = std::min(minimum.x, corner.x);
        minimum.y = std::min(minimum.y, corner.y);
        minimum.z = std::min(minimum.z, corner.z);
        maximum.x = std::max(maximum.x, corner.x);
        maximum.y = std::max(maximum.y, corner.y);
        maximum.z = std::max(maximum.z, corner.z);
    }
    result.center = (minimum + maximum) * 0.5F;
    result.radius = (maximum - result.center).length();
    return result;
}

bool viewportBoundsVisible(
    const ViewportWorldBounds& bounds,
    math::Vec3 eye,
    math::Vec3 right,
    math::Vec3 up,
    math::Vec3 forward,
    float verticalFovRadians,
    float aspectRatio,
    float nearPlane,
    float farPlane,
    float boundsPadding)
{
    if (bounds.radius < 0.0F || !std::isfinite(bounds.radius)) {
        return true;
    }
    const auto safePadding = std::max(boundsPadding, 0.0F);
    if (pointInsideBounds(eye, bounds, safePadding)) {
        return true;
    }
    const auto tanY = std::tan(verticalFovRadians * 0.5F);
    const auto tanX = tanY * std::max(aspectRatio, 0.001F);
    if (!std::isfinite(tanX) || !std::isfinite(tanY) || tanX <= 0.0F || tanY <= 0.0F) {
        return true;
    }
    const auto planePaddingX = safePadding * std::sqrt(tanX * tanX + 1.0F);
    const auto planePaddingY = safePadding * std::sqrt(tanY * tanY + 1.0F);

    bool outsideNear = true;
    bool outsideFar = true;
    bool outsideLeft = true;
    bool outsideRight = true;
    bool outsideBottom = true;
    bool outsideTop = true;
    for (const auto corner : bounds.corners) {
        const auto relative = corner - eye;
        const auto x = math::dot(relative, right);
        const auto y = math::dot(relative, up);
        const auto z = math::dot(relative, forward);
        outsideNear = outsideNear && z < nearPlane - safePadding;
        outsideFar = outsideFar && z > farPlane + safePadding;
        outsideLeft = outsideLeft && (z * tanX + x) < -planePaddingX;
        outsideRight = outsideRight && (z * tanX - x) < -planePaddingX;
        outsideBottom = outsideBottom && (z * tanY + y) < -planePaddingY;
        outsideTop = outsideTop && (z * tanY - y) < -planePaddingY;
    }
    return !(outsideNear || outsideFar || outsideLeft || outsideRight || outsideBottom || outsideTop);
}

bool defaultPrimitiveProxyTransform(const scene::TransformComponent& transform, math::Vec3 expectedPosition) noexcept
{
    return nearVec3(transform.position, expectedPosition)
        && nearVec3(transform.rotationEuler, {0.0F, 0.0F, 0.0F})
        && nearVec3(transform.scale, {1.0F, 1.0F, 1.0F});
}

} // namespace projectunity::editor
