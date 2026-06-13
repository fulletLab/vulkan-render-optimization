#include <projectunity/editor/ViewportWidget.hpp>

#include "ViewportRenderWorld.hpp"

#include <projectunity/core/Log.hpp>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace projectunity::editor {
namespace {

constexpr float kFlyMoveSpeed = 7.5F;
constexpr float kFlyFastMultiplier = 3.0F;
constexpr float kFlyLookSensitivity = 0.0035F;
constexpr float kFlyTickSeconds = 1.0F / 60.0F;
constexpr float kMinPitch = -1.52F;
constexpr float kMaxPitch = 1.52F;

enum GameKey : std::size_t {
    Forward,
    Back,
    Left,
    Right,
    Up,
    Down,
    Fast,
};

[[nodiscard]] float safeLength(math::Vec3 value) noexcept
{
    return std::sqrt(value.lengthSquared());
}

[[nodiscard]] math::Vec3 safeNormalized(math::Vec3 value, math::Vec3 fallback) noexcept
{
    const auto length = safeLength(value);
    return length > 0.00001F && std::isfinite(length) ? value / length : fallback;
}

[[nodiscard]] math::Vec3 forwardFromYawPitch(float yaw, float pitch) noexcept
{
    const auto cosPitch = std::cos(pitch);
    return safeNormalized({
        cosPitch * std::sin(yaw),
        std::sin(pitch),
        cosPitch * std::cos(yaw),
    }, {0.0F, 0.0F, 1.0F});
}

[[nodiscard]] math::Vec3 rightFromForward(math::Vec3 forward) noexcept
{
    return safeNormalized(math::cross({0.0F, 1.0F, 0.0F}, forward), {1.0F, 0.0F, 0.0F});
}

[[nodiscard]] math::Vec3 upFromBasis(math::Vec3 forward, math::Vec3 right) noexcept
{
    return safeNormalized(math::cross(forward, right), {0.0F, 1.0F, 0.0F});
}

[[nodiscard]] bool isFlyPlayerScript(const scene::Entity& entity) noexcept
{
    return entity.script.has_value()
        && entity.script->enabled
        && entity.script->scriptName == "FlyPlayerController"
        && entity.camera.has_value();
}

} // namespace

void ViewportWidget::setGameInputEnabled(bool enabled)
{
    gameInputEnabled_ = enabled;
    gameKeys_.fill(false);
    gameMouseLook_ = false;
    if (mode_ != ViewportMode::Game) {
        return;
    }
    if (gameScriptTimer_ == nullptr) {
        gameScriptTimer_ = new QTimer(this);
        gameScriptTimer_->setInterval(16);
        connect(gameScriptTimer_, &QTimer::timeout, this, [this] { tickGameScripts(); });
    }
    if (enabled) {
        setFocus(Qt::OtherFocusReason);
        gameScriptTimer_->start();
    } else {
        gameScriptTimer_->stop();
    }
    update();
}

void ViewportWidget::setGameCameraEntity(scene::EntityId id)
{
    gameCameraEntityId_ = id;
    if (scene_ == nullptr || !id.isValid()) {
        return;
    }
    const auto* entity = scene_->findEntity(id);
    if (entity == nullptr || !entity->camera.has_value()) {
        return;
    }
    const auto forward = safeNormalized(entity->camera->direction, {0.0F, 0.0F, 1.0F});
    gameYawRadians_ = std::atan2(forward.x, forward.z);
    gamePitchRadians_ = std::asin(std::clamp(forward.y, -1.0F, 1.0F));
}

void ViewportWidget::setGameRuntimeSnapshotEnabled(bool enabled)
{
    if (gameRuntimeSnapshotEnabled_ == enabled) {
        return;
    }
    gameRuntimeSnapshotEnabled_ = enabled;
    if (renderWorld_ != nullptr) {
        renderWorld_->markDirty();
    }
    update();
}

bool ViewportWidget::handleGameKey(QKeyEvent* event, bool pressed)
{
    if (!gameInputEnabled_ || event == nullptr) {
        return false;
    }
    switch (event->key()) {
    case Qt::Key_W: gameKeys_[Forward] = pressed; break;
    case Qt::Key_S: gameKeys_[Back] = pressed; break;
    case Qt::Key_A: gameKeys_[Left] = pressed; break;
    case Qt::Key_D: gameKeys_[Right] = pressed; break;
    case Qt::Key_E: gameKeys_[Up] = pressed; break;
    case Qt::Key_Q: gameKeys_[Down] = pressed; break;
    case Qt::Key_Shift: gameKeys_[Fast] = pressed; break;
    default: return false;
    }
    event->accept();
    return true;
}

void ViewportWidget::handleGameMousePress(QMouseEvent* event)
{
    if (!gameInputEnabled_ || event == nullptr) {
        QWidget::mousePressEvent(event);
        return;
    }
    setFocus(Qt::MouseFocusReason);
    lastMousePosition_ = event->position().toPoint();
    if (event->button() == Qt::RightButton) {
        gameMouseLook_ = true;
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void ViewportWidget::handleGameMouseMove(QMouseEvent* event)
{
    if (!gameInputEnabled_ || event == nullptr) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    const auto current = event->position().toPoint();
    const auto delta = current - lastMousePosition_;
    lastMousePosition_ = current;
    if (gameMouseLook_) {
        gameYawRadians_ += static_cast<float>(delta.x()) * kFlyLookSensitivity;
        gamePitchRadians_ = std::clamp(gamePitchRadians_ - static_cast<float>(delta.y()) * kFlyLookSensitivity, kMinPitch, kMaxPitch);
        tickGameScripts();
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void ViewportWidget::handleGameMouseRelease(QMouseEvent* event)
{
    if (event != nullptr && event->button() == Qt::RightButton && gameMouseLook_) {
        gameMouseLook_ = false;
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void ViewportWidget::keyReleaseEvent(QKeyEvent* event)
{
    if (mode_ == ViewportMode::Game) {
        if (!handleGameKey(event, false)) {
            QWidget::keyReleaseEvent(event);
        }
        return;
    }
    QWidget::keyReleaseEvent(event);
}

void ViewportWidget::tickGameScripts()
{
    if (!gameInputEnabled_ || mode_ != ViewportMode::Game || scene_ == nullptr || !gameCameraEntityId_.isValid()) {
        return;
    }
    auto* entity = scene_->findEntity(gameCameraEntityId_);
    if (entity == nullptr || !isFlyPlayerScript(*entity)) {
        return;
    }
    const auto forward = forwardFromYawPitch(gameYawRadians_, gamePitchRadians_);
    const auto right = rightFromForward(forward);
    const auto up = upFromBasis(forward, right);
    auto transform = entity->transform;
    auto movement = math::Vec3 {};
    if (gameKeys_[Forward]) { movement += forward; }
    if (gameKeys_[Back]) { movement -= forward; }
    if (gameKeys_[Right]) { movement += right; }
    if (gameKeys_[Left]) { movement -= right; }
    if (gameKeys_[Up]) { movement += math::Vec3 {0.0F, 1.0F, 0.0F}; }
    if (gameKeys_[Down]) { movement -= math::Vec3 {0.0F, 1.0F, 0.0F}; }
    if (safeLength(movement) > 0.00001F) {
        const auto speed = kFlyMoveSpeed * (gameKeys_[Fast] ? kFlyFastMultiplier : 1.0F);
        transform.position += safeNormalized(movement, {}) * (speed * kFlyTickSeconds);
        (void)scene_->setTransform(entity->id, transform);
    }
    auto camera = *entity->camera;
    camera.direction = forward;
    camera.right = right;
    camera.up = up;
    (void)scene_->setCamera(entity->id, camera);
    if (transformEditedCallback_) {
        transformEditedCallback_(entity->id);
    }
    update();
}

} // namespace projectunity::editor
