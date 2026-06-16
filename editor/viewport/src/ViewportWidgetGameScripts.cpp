#include <projectunity/editor/ViewportWidget.hpp>

#include "ViewportRenderWorld.hpp"

#include <projectunity/scripting/ScriptRuntime.hpp>

#include <QCursor>
#include <QFocusEvent>
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
    case Qt::Key_Tab:
        return scripting::KeyCode::Tab;
    case Qt::Key_Escape:
        return scripting::KeyCode::Escape;
    default:
        return std::nullopt;
    }
}

} // namespace

void ViewportWidget::setGameInputEnabled(bool enabled)
{
    if (!enabled) {
        setMouseCaptured(false);
    }
    gameInputEnabled_ = enabled;
    gameInputState_ = {};
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
    if (runtime == nullptr) {
        setMouseCaptured(false);
    }
    if (gameScriptRuntime_ != nullptr && gameScriptRuntime_->inputService() == this) {
        gameScriptRuntime_->setInputService(nullptr);
    }
    gameScriptRuntime_ = runtime;
    if (gameScriptRuntime_ != nullptr) {
        gameScriptRuntime_->setInputService(this);
    }
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
    if (event->isAutoRepeat()) {
        event->accept();
        return true;
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
    if (gameInputState_.mouseCaptured) {
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
    lastMousePosition_ = current;
    if (gameInputState_.mouseCaptured) {
        const auto center = rect().center();
        const auto centerGlobal = mapToGlobal(center);
        const auto delta = event->globalPosition().toPoint() - centerGlobal;
        gameInputState_.mouseDeltaX += static_cast<float>(delta.x());
        gameInputState_.mouseDeltaY += static_cast<float>(delta.y());
        centerGameMouseCursor();
        update();
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void ViewportWidget::handleGameMouseRelease(QMouseEvent* event)
{
    if (event != nullptr && gameInputState_.mouseCaptured) {
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void ViewportWidget::focusOutEvent(QFocusEvent* event)
{
    if (mode_ == ViewportMode::Game) {
        setMouseCaptured(false);
        gameInputState_ = {};
    }
    QWidget::focusOutEvent(event);
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

void ViewportWidget::setMouseCaptured(bool captured)
{
    const auto nextCaptured = captured && mode_ == ViewportMode::Game && gameInputEnabled_;
    if (gameInputState_.mouseCaptured == nextCaptured) {
        return;
    }

    gameInputState_.mouseCaptured = nextCaptured;
    if (nextCaptured) {
        setFocus(Qt::OtherFocusReason);
        setCursor(QCursor(Qt::BlankCursor));
        if (isVisible()) {
            grabMouse(QCursor(Qt::BlankCursor));
            centerGameMouseCursor();
        }
    } else {
        if (QWidget::mouseGrabber() == this) {
            releaseMouse();
        }
        unsetCursor();
        lastMousePosition_ = mapFromGlobal(QCursor::pos());
    }
}

bool ViewportWidget::isMouseCaptured() const noexcept
{
    return mode_ == ViewportMode::Game && gameInputState_.mouseCaptured;
}

void ViewportWidget::centerGameMouseCursor()
{
    if (!isVisible() || width() <= 0 || height() <= 0) {
        return;
    }

    const auto center = rect().center();
    lastMousePosition_ = center;
    QCursor::setPos(mapToGlobal(center));
}

} // namespace projectunity::editor
