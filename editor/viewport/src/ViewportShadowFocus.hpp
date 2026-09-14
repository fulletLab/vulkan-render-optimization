#pragma once

#include <projectunity/editor/ViewportWidget.hpp>
#include <projectunity/math/Vec3.hpp>

namespace projectunity::editor {

struct ViewportShadowFocus {
    math::Vec3 center;
    float radius {80.0F};
};

[[nodiscard]] ViewportShadowFocus stableViewportShadowFocus(
    ViewportMode mode,
    math::Vec3 cameraEye,
    math::Vec3 cameraForward,
    math::Vec3 sceneCameraTarget,
    float sceneCameraDistance,
    float cameraFarPlane,
    float shadowFocusRadius,
    bool visibleBoundsValid,
    float visibleBoundsRadius);

} // namespace projectunity::editor
