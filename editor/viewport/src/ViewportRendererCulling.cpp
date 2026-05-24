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
    float farPlane)
{
    if (bounds.radius < 0.0F || !std::isfinite(bounds.radius)) {
        return true;
    }
    const auto tanY = std::tan(verticalFovRadians * 0.5F);
    const auto tanX = tanY * std::max(aspectRatio, 0.001F);
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
        outsideNear = outsideNear && z < nearPlane;
        outsideFar = outsideFar && z > farPlane;
        outsideLeft = outsideLeft && (z * tanX + x) < 0.0F;
        outsideRight = outsideRight && (z * tanX - x) < 0.0F;
        outsideBottom = outsideBottom && (z * tanY + y) < 0.0F;
        outsideTop = outsideTop && (z * tanY - y) < 0.0F;
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
