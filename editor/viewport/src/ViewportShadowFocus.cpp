#include "ViewportShadowFocus.hpp"

#include <algorithm>
#include <cmath>

namespace projectunity::editor {
namespace {

[[nodiscard]] math::Vec3 safeNormalized(math::Vec3 value, math::Vec3 fallback)
{
    const auto length = value.length();
    if (length <= 0.00001F || !std::isfinite(length)) {
        return fallback;
    }
    return value / length;
}

} // namespace

ViewportShadowFocus stableViewportShadowFocus(
    ViewportMode mode,
    math::Vec3 cameraEye,
    math::Vec3 cameraForward,
    math::Vec3 sceneCameraTarget,
    float sceneCameraDistance,
    float cameraFarPlane,
    float shadowFocusRadius,
    bool visibleBoundsValid,
    float visibleBoundsRadius)
{
    const auto configuredRadius = std::isfinite(shadowFocusRadius)
        ? std::clamp(shadowFocusRadius, 48.0F, 5000.0F)
        : 220.0F;
    const auto horizontalForward = safeNormalized(
        {cameraForward.x, 0.0F, cameraForward.z},
        {0.0F, 0.0F, 1.0F});
    const auto focusDistance = mode == ViewportMode::Scene
        ? std::clamp(sceneCameraDistance * 0.65F, 8.0F, 96.0F)
        : std::clamp(cameraFarPlane * 0.035F, 24.0F, 120.0F);
    auto center = mode == ViewportMode::Scene
        ? sceneCameraTarget
        : cameraEye + horizontalForward * focusDistance;
    center.y = mode == ViewportMode::Scene ? sceneCameraTarget.y : cameraEye.y;

    auto radius = mode == ViewportMode::Scene
        ? std::max(sceneCameraDistance * 1.65F, 48.0F)
        : 96.0F;
    if (visibleBoundsValid) {
        radius = std::max(radius, std::min(visibleBoundsRadius * 1.15F, configuredRadius));
    }
    if (mode == ViewportMode::Game) {
        radius = std::max(radius, std::min(cameraFarPlane * 0.12F, configuredRadius));
    }
    return {center, std::clamp(radius, 48.0F, configuredRadius)};
}

} // namespace projectunity::editor
