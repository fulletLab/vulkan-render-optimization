#pragma once

#include <projectunity/editor/EditorQualitySettings.hpp>
#include <projectunity/renderer/RendererTypes.hpp>

#include <functional>

#include <QElapsedTimer>
#include <QOpenGLWidget>

class QKeyEvent;
class QMouseEvent;
class QEvent;
class QTimer;
class QWheelEvent;
struct ImGuiContext;

namespace projectunity::editor {

class ViewportTuningImGuiWindow final : public QOpenGLWidget {
public:
    using ApplySettingsCallback = std::function<void(EditorQualitySettings, bool)>;
    using StatsProvider = std::function<const renderer::RendererStats*()>;

    ViewportTuningImGuiWindow(
        EditorQualitySettings settings,
        ApplySettingsCallback applySettings,
        StatsProvider statsProvider,
        QWidget* parent = nullptr);
    ~ViewportTuningImGuiWindow() override;

    void syncSettings(const EditorQualitySettings& settings);
    void showToolWindow();

protected:
    void initializeGL() override;
    void paintGL() override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    [[nodiscard]] bool drawTuningUi();
    void applyLiveSettings();
    void setCurrentImGuiContext() const;
    void submitKeyboardModifiers(Qt::KeyboardModifiers modifiers);

    EditorQualitySettings settings_;
    ApplySettingsCallback applySettings_;
    StatsProvider statsProvider_;
    ImGuiContext* imguiContext_ {nullptr};
    QTimer* repaintTimer_ {nullptr};
    QTimer* persistTimer_ {nullptr};
    QElapsedTimer frameTimer_;
    bool imguiReady_ {false};
    bool applyingSettings_ {false};
};

} // namespace projectunity::editor
