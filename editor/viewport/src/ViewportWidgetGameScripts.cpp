#include <projectunity/editor/ViewportWidget.hpp>

#include "ViewportRenderWorld.hpp"

#include <projectunity/scripting/ScriptRuntime.hpp>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QTimer>

#include <optional>

namespace projectunity::editor {
namespace {

constexpr float kRuntimeTickSeconds = 1.0F / 60.0F;

[[nodiscard]] std::optional<scripting::KeyCode> keyCodeFromQt(int key) noexcept
{
    switch (key) {
    case Qt::Key_W:
        return scripting::KeyCode::W;
    case Qt::Key_A:
        return scripting::KeyCode::A;
    case Qt::Key_S:
        return scripting::KeyCode::S;
    case Qt::Key_D:
        return scripting::KeyCode::D;
    case Qt::Key_Q:
        return scripting::KeyCode::Q;
    case Qt::Key_E:
        return scripting::KeyCode::E;
    case Qt::Key_Space:
        return scripting::KeyCode::Space;
    case Qt::Key_Shift:
        return scripting::KeyCode::LeftShift;
    default:
        return std::nullopt;
    }
}

} // namespace

void ViewportWidget::setGameInputEnabled(bool enabled)
{
    gameInputEnabled_ = enabled;
    gameInputState_ = {};
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
}

void ViewportWidget::setGameScriptRuntime(scripting::ScriptRuntime* runtime)
{
    gameScriptRuntime_ = runtime;
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
    const auto key = keyCodeFromQt(event->key());
    if (!key.has_value()) {
        return false;
    }
    gameInputState_.setKeyDown(*key, pressed);
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
        gameInputState_.mouseLook = true;
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
        gameInputState_.mouseDeltaX += static_cast<float>(delta.x());
        gameInputState_.mouseDeltaY += static_cast<float>(delta.y());
        update();
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void ViewportWidget::handleGameMouseRelease(QMouseEvent* event)
{
    if (event != nullptr && event->button() == Qt::RightButton && gameMouseLook_) {
        gameMouseLook_ = false;
        gameInputState_.mouseLook = false;
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
    if (!gameInputEnabled_ || mode_ != ViewportMode::Game || gameScriptRuntime_ == nullptr) {
        gameInputState_.clearFrameDeltas();
        return;
    }

    gameScriptRuntime_->update(kRuntimeTickSeconds, gameInputState_);
    if (transformEditedCallback_ && gameCameraEntityId_.isValid()) {
        transformEditedCallback_(gameCameraEntityId_);
    }
    gameInputState_.clearFrameDeltas();
    update();
}

} // namespace projectunity::editor
