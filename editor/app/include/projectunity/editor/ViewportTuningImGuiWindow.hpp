#pragma once

#include <projectunity/editor/EditorQualitySettings.hpp>
#include <projectunity/renderer/RendererTypes.hpp>

#include <functional>
#include <string>

#include <QElapsedTimer>
#include <QOpenGLWindow>

class QKeyEvent;
class QMouseEvent;
class QEvent;
class QTimer;
class QWheelEvent;
struct ImGuiContext;

namespace projectunity::editor {

class ViewportTuningImGuiWindow final : public QOpenGLWindow {
public:
    using ApplySettingsCallback = std::function<void(EditorQualitySettings, bool)>;
    using StatsProvider = std::function<const renderer::RendererStats*()>;
    using CameraSettingsCallback = std::function<void(float, float, float)>;

    ViewportTuningImGuiWindow(
        EditorQualitySettings settings,
        ApplySettingsCallback applySettings,
        StatsProvider statsProvider,
        CameraSettingsCallback cameraSettings,
        QWindow* parent = nullptr,
        bool embedded = false);
    ~ViewportTuningImGuiWindow() override;

    void syncSettings(const EditorQualitySettings& settings);
    void showToolWindow();
    [[nodiscard]] bool isEmbedded() const noexcept { return embedded_; }

protected:
    void initializeGL() override;
    void paintGL() override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    bool event(QEvent* event) override;

private:
    [[nodiscard]] bool drawTuningUi();
    void applyLiveSettings();
    void importSettingsFromJson();
    void setCurrentImGuiContext() const;
    void submitKeyboardModifiers(Qt::KeyboardModifiers modifiers);

    EditorQualitySettings settings_;
    ApplySettingsCallback applySettings_;
    StatsProvider statsProvider_;
    CameraSettingsCallback cameraSettings_;
    ImGuiContext* imguiContext_ {nullptr};
    QTimer* repaintTimer_ {nullptr};
    QTimer* persistTimer_ {nullptr};
    QElapsedTimer frameTimer_;
    bool imguiReady_ {false};
    bool applyingSettings_ {false};
    bool embedded_ {false};
    bool balancedPresetRequested_ {false};
    bool importJsonRequested_ {false};
    float cameraNearPlane_ {0.05F};
    float cameraFarPlane_ {4000.0F};
    float cameraFovDegrees_ {60.0F};
    std::string importJsonStatus_;
};

} // namespace projectunity::editor
