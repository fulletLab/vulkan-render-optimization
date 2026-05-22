#include <projectunity/editor/ViewportWidget.hpp>

#include <algorithm>
#include <cmath>

namespace projectunity::editor {
namespace {

constexpr float kMinCameraDistance = 1.0F;
constexpr float kMaxCameraDistance = 500.0F;
constexpr float kMinPitch = -1.45F;
constexpr float kMaxPitch = 1.45F;

} // namespace

void ViewportWidget::orbitCamera(QPoint delta)
{
    camera_.yawRadians += static_cast<float>(delta.x()) * 0.008F;
    camera_.pitchRadians = std::clamp(
        camera_.pitchRadians + static_cast<float>(delta.y()) * 0.008F,
        kMinPitch,
        kMaxPitch);
    update();
}

void ViewportWidget::lookCamera(QPoint delta)
{
    const auto fixedCameraPosition = cameraPosition();
    camera_.yawRadians += static_cast<float>(delta.x()) * 0.006F;
    camera_.pitchRadians = std::clamp(
        camera_.pitchRadians - static_cast<float>(delta.y()) * 0.006F,
        kMinPitch,
        kMaxPitch);
    camera_.target = fixedCameraPosition + cameraForward() * camera_.distance;
    update();
}

void ViewportWidget::panCamera(QPoint delta)
{
    const auto panScale = 2.0F * camera_.distance * std::tan(camera_.verticalFovRadians * 0.5F)
        / static_cast<float>(std::max(1, height()));
    camera_.target -= cameraRight() * (static_cast<float>(delta.x()) * panScale);
    camera_.target += cameraUp() * (static_cast<float>(delta.y()) * panScale);
    update();
}

void ViewportWidget::moveCameraLocal(math::Vec3 localDirection, bool fastMode)
{
    auto worldDirection = cameraRight() * localDirection.x
        + cameraUp() * localDirection.y
        + cameraForward() * localDirection.z;
    const auto directionLength = std::sqrt(worldDirection.lengthSquared());
    if (directionLength <= 0.00001F || !std::isfinite(directionLength)) {
        return;
    }

    worldDirection = worldDirection / directionLength;
    const auto baseStep = std::clamp(camera_.distance * 0.12F, 0.15F, 8.0F);
    const auto step = fastMode ? baseStep * 3.0F : baseStep;
    camera_.target += worldDirection * step;
    update();
}

void ViewportWidget::zoomCamera(float wheelSteps)
{
    camera_.distance = std::clamp(camera_.distance * std::pow(0.88F, wheelSteps), kMinCameraDistance, kMaxCameraDistance);
    update();
}

} // namespace projectunity::editor
