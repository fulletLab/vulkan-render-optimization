#include <projectunity/renderer/RenderShadowSetup.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace projectunity::renderer {
namespace {
struct Vec3 {
    float x {0.0F};
    float y {0.0F};
    float z {0.0F};
};

[[nodiscard]] Vec3 operator+(Vec3 lhs, Vec3 rhs)
{
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

[[nodiscard]] Vec3 operator-(Vec3 lhs, Vec3 rhs)
{
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

[[nodiscard]] Vec3 operator*(Vec3 value, float scalar)
{
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

[[nodiscard]] float dot(Vec3 lhs, Vec3 rhs)
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

[[nodiscard]] Vec3 cross(Vec3 lhs, Vec3 rhs)
{
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

[[nodiscard]] float length(Vec3 value)
{
    return std::sqrt(dot(value, value));
}

[[nodiscard]] Vec3 safeNormalized(Vec3 value, Vec3 fallback)
{
    const auto valueLength = length(value);
    if (valueLength <= 0.00001F || !std::isfinite(valueLength)) {
        return fallback;
    }
    return value * (1.0F / valueLength);
}

[[nodiscard]] Vec3 vec3(std::array<float, 3> value)
{
    return {value[0], value[1], value[2]};
}

[[nodiscard]] float& at(RenderMatrix4& matrix, int row, int column)
{
    return matrix.values[static_cast<std::size_t>(column * 4 + row)];
}

[[nodiscard]] float at(const RenderMatrix4& matrix, int row, int column)
{
    return matrix.values[static_cast<std::size_t>(column * 4 + row)];
}

[[nodiscard]] RenderMatrix4 multiply(const RenderMatrix4& lhs, const RenderMatrix4& rhs)
{
    RenderMatrix4 result;
    result.values.fill(0.0F);
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            for (int index = 0; index < 4; ++index) {
                at(result, row, column) += at(lhs, row, index) * at(rhs, index, column);
            }
        }
    }
    return result;
}

[[nodiscard]] RenderMatrix4 viewMatrix(Vec3 eye, Vec3 right, Vec3 up, Vec3 forward)
{
    RenderMatrix4 view;
    view.values.fill(0.0F);
    at(view, 0, 0) = right.x;
    at(view, 0, 1) = right.y;
    at(view, 0, 2) = right.z;
    at(view, 0, 3) = -dot(right, eye);
    at(view, 1, 0) = up.x;
    at(view, 1, 1) = up.y;
    at(view, 1, 2) = up.z;
    at(view, 1, 3) = -dot(up, eye);
    at(view, 2, 0) = forward.x;
    at(view, 2, 1) = forward.y;
    at(view, 2, 2) = forward.z;
    at(view, 2, 3) = -dot(forward, eye);
    at(view, 3, 3) = 1.0F;
    return view;
}

[[nodiscard]] RenderMatrix4 orthographicMatrix(float halfWidth, float halfHeight, float nearPlane, float farPlane)
{
    RenderMatrix4 projection;
    projection.values.fill(0.0F);
    at(projection, 0, 0) = 1.0F / std::max(halfWidth, 0.001F);
    at(projection, 1, 1) = -1.0F / std::max(halfHeight, 0.001F);
    at(projection, 2, 2) = 1.0F / std::max(farPlane - nearPlane, 0.001F);
    at(projection, 2, 3) = -nearPlane / std::max(farPlane - nearPlane, 0.001F);
    at(projection, 3, 3) = 1.0F;
    return projection;
}

[[nodiscard]] RenderMatrix4 perspectiveMatrix(float verticalFovRadians, float nearPlane, float farPlane)
{
    RenderMatrix4 projection;
    projection.values.fill(0.0F);
    const auto focal = 1.0F / std::tan(verticalFovRadians * 0.5F);
    at(projection, 0, 0) = focal;
    at(projection, 1, 1) = -focal;
    at(projection, 2, 2) = farPlane / std::max(farPlane - nearPlane, 0.001F);
    at(projection, 2, 3) = -(nearPlane * farPlane) / std::max(farPlane - nearPlane, 0.001F);
    at(projection, 3, 2) = 1.0F;
    return projection;
}

[[nodiscard]] Vec3 stableRight(Vec3 forward)
{
    Vec3 upAxis {0.0F, 1.0F, 0.0F};
    if (std::fabs(dot(forward, upAxis)) > 0.96F) {
        upAxis = {1.0F, 0.0F, 0.0F};
    }
    return safeNormalized(cross(upAxis, forward), {1.0F, 0.0F, 0.0F});
}

[[nodiscard]] RenderMatrix4 directionalShadowMatrix(const RenderLight& light, Vec3 center, float boundsRadius)
{
    const auto forward = safeNormalized(vec3(light.direction), {0.35F, -0.82F, 0.45F});
    const auto right = stableRight(forward);
    const auto up = safeNormalized(cross(forward, right), {0.0F, 1.0F, 0.0F});
    const auto halfExtent = std::clamp(std::max(boundsRadius, 1.0F) * 1.35F, 12.0F, 640.0F);
    constexpr float shadowMapSize = 4096.0F;
    const auto texelWorldSize = (halfExtent * 2.0F) / shadowMapSize;
    const auto snapAxis = [texelWorldSize](float value) {
        return std::floor(value / texelWorldSize) * texelWorldSize;
    };
    const auto snappedCenter = center
        + right * (snapAxis(dot(center, right)) - dot(center, right))
        + up * (snapAxis(dot(center, up)) - dot(center, up));
    const auto shadowDistance = halfExtent * 3.0F + std::max(boundsRadius, 1.0F);
    const auto eye = snappedCenter - forward * shadowDistance;
    return multiply(
        orthographicMatrix(halfExtent, halfExtent, 0.05F, shadowDistance + halfExtent * 2.0F),
        viewMatrix(eye, right, up, forward));
}

[[nodiscard]] RenderMatrix4 spotShadowMatrix(const RenderLight& light, Vec3 center, float boundsRadius)
{
    const auto position = vec3(light.position);
    const auto forward = safeNormalized(vec3(light.direction), safeNormalized(center - position, {0.0F, -1.0F, 0.0F}));
    const auto right = stableRight(forward);
    const auto up = safeNormalized(cross(forward, right), {0.0F, 1.0F, 0.0F});
    const auto distanceToBounds = length(center - position) + std::max(boundsRadius, 1.0F);
    const auto farPlane = std::max({light.range, distanceToBounds + 1.0F, 2.0F});
    const auto outerCone = light.outerConeAngle > 0.001F ? light.outerConeAngle : 0.7853981634F;
    const auto fov = std::clamp(outerCone * 2.16F, 0.15F, 2.75F);
    return multiply(perspectiveMatrix(fov, 0.05F, farPlane), viewMatrix(position, right, up, forward));
}
} // namespace

RenderShadowMapSelection chooseShadowMap(
    std::span<const RenderLight> lights,
    std::array<float, 3> visibleBoundsCenter,
    float visibleBoundsRadius)
{
    const auto center = vec3(visibleBoundsCenter);
    const auto radius = std::isfinite(visibleBoundsRadius) ? std::max(visibleBoundsRadius, 1.0F) : 1.0F;
    const auto directional = std::find_if(lights.begin(), lights.end(), [](const RenderLight& light) {
        return light.type == RenderLightType::Directional;
    });
    if (directional != lights.end()) {
        const auto index = static_cast<std::uint32_t>(std::distance(lights.begin(), directional));
        return {true, index, RenderLightType::Directional, directionalShadowMatrix(*directional, center, radius)};
    }
    const auto spot = std::find_if(lights.begin(), lights.end(), [](const RenderLight& light) {
        return light.type == RenderLightType::Spot;
    });
    if (spot != lights.end()) {
        const auto index = static_cast<std::uint32_t>(std::distance(lights.begin(), spot));
        return {true, index, RenderLightType::Spot, spotShadowMatrix(*spot, center, radius)};
    }
    return {};
}

} // namespace projectunity::renderer
