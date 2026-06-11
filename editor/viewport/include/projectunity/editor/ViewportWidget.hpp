#pragma once

#include <projectunity/assets/AssetManager.hpp>
#include <projectunity/debug/DebugDraw.hpp>
#include <projectunity/editor/IEditorDebugDrawBackend.hpp>
#include <projectunity/editor/IEditorGizmoBackend.hpp>
#include <projectunity/math/Vec3.hpp>
#include <projectunity/renderer/RendererTypes.hpp>
#include <projectunity/scene/Scene.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <QPoint>
#include <QPointF>
#include <QWidget>

class QKeyEvent;
class QMouseEvent;
class QPainter;
class QResizeEvent;
class QTimer;
class QWheelEvent;

namespace projectunity::renderer {
class IRenderer;
} // namespace projectunity::renderer

namespace projectunity::editor {

class ViewportRenderWorld;

enum class ViewportMode {
    Scene,
    Game,
};

enum class ViewportTool {
    Hand,
    Move,
    Rotate,
    Scale,
};

enum class TransformSpace {
    Local,
    Global,
};

struct ViewportRay {
    math::Vec3 origin;
    math::Vec3 direction;
};

class ViewportWidget final : public QWidget {
public:
    explicit ViewportWidget(ViewportMode mode, QWidget* parent = nullptr);
    ~ViewportWidget() override;

    void setAssetManager(const assets::IAssetManager* assetManager);
    void setRenderer(renderer::IRenderer* renderer);
    void setEnvironmentSettings(renderer::RenderEnvironmentSettings settings);
    void setScene(scene::Scene* scene);
    void setSelectedEntity(scene::EntityId id);
    void setSelectionCallback(std::function<void(scene::EntityId)> callback);
    void setTransformEditedCallback(std::function<void(scene::EntityId)> callback);
    void setTool(ViewportTool tool);
    [[nodiscard]] ViewportTool tool() const noexcept;
    void setTransformSpace(TransformSpace space);
    [[nodiscard]] TransformSpace transformSpace() const noexcept;
    void focusSelected();

    [[nodiscard]] ViewportRay screenPointToRay(QPointF point) const;
    [[nodiscard]] std::optional<scene::EntityId> pickEntityAt(QPointF point) const;
    [[nodiscard]] bool runSelfTest(QString* errorMessage);
    [[nodiscard]] const renderer::RendererStats* lastRendererStats() const noexcept;
    void setCameraForTesting(math::Vec3 target, float distance, float yawRadians, float pitchRadians);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    enum class DragMode {
        None,
        Orbit,
        Look,
        Pan,
    };

    struct CameraState {
        math::Vec3 target {0.0F, 0.0F, 0.0F};
        float distance {10.0F};
        float yawRadians {0.65F};
        float pitchRadians {-0.38F};
        float verticalFovRadians {1.04719755F};
    };

    struct ProjectedPoint {
        QPointF point;
        float depth {0.0F};
        bool visible {false};
    };

    [[nodiscard]] math::Vec3 cameraForward() const;
    [[nodiscard]] math::Vec3 cameraRight() const;
    [[nodiscard]] math::Vec3 cameraUp() const;
    [[nodiscard]] math::Vec3 cameraPosition() const;
    [[nodiscard]] float aspectRatio() const;
    [[nodiscard]] ProjectedPoint projectPoint(math::Vec3 worldPosition) const;
    [[nodiscard]] std::optional<math::Vec3> entityWorldPosition(scene::EntityId id) const;
    [[nodiscard]] float entityPickRadius(const scene::Entity& entity) const;
    [[nodiscard]] QString toolModeName() const;
    [[nodiscard]] EditorDebugFrame createDebugFrame() const;
    [[nodiscard]] debug::DebugDrawList createDebugDrawList() const;
    [[nodiscard]] debug::FrustumCorners createCameraDebugFrustum() const;
    [[nodiscard]] EditorGizmoFrame createGizmoFrame() const;
    [[nodiscard]] EditorGizmoTransform createSelectedGizmoTransform() const;
    [[nodiscard]] bool applySelectedGizmoTransform(const EditorGizmoTransform& transform);
    [[nodiscard]] bool updateGizmoFrame(bool applyTransform = true);
    [[nodiscard]] bool runGizmoDragSelfTest(ViewportTool tool, QString* errorMessage);
    [[nodiscard]] bool ensureRendererSurface();
    [[nodiscard]] bool renderRendererFrame();

    void drawBackground(QPainter& painter) const;
    void drawDebugGeometry(QPainter& painter);
    void drawAxes(QPainter& painter) const;
    void drawHierarchyLinks(QPainter& painter) const;
    void drawEntities(QPainter& painter) const;
    void drawEntity(QPainter& painter, const scene::Entity& entity) const;
    [[nodiscard]] bool drawMeshEntity(QPainter& painter, const scene::Entity& entity, math::Vec3 position) const;
    void drawGizmo(QPainter& painter);
    void drawOverlay(QPainter& painter) const;
    void orbitCamera(QPoint delta);
    void lookCamera(QPoint delta);
    void panCamera(QPoint delta);
    void moveCameraLocal(math::Vec3 localDirection, bool fastMode);
    void zoomCamera(float wheelSteps);

    ViewportMode mode_;
    const assets::IAssetManager* assetManager_ {nullptr};
    renderer::IRenderer* renderer_ {nullptr};
    renderer::RenderEnvironmentSettings environmentSettings_;
    scene::Scene* scene_ {nullptr};
    scene::EntityId selectedEntityId_;
    std::function<void(scene::EntityId)> selectionCallback_;
    std::function<void(scene::EntityId)> transformEditedCallback_;
    CameraState camera_;
    ViewportTool tool_ {ViewportTool::Move};
    TransformSpace transformSpace_ {TransformSpace::Local};
    DragMode dragMode_ {DragMode::None};
    QPoint lastMousePosition_;
    std::unique_ptr<IEditorDebugDrawBackend> debugDrawBackend_;
    std::unique_ptr<IEditorGizmoBackend> gizmoBackend_;
    std::unique_ptr<ViewportRenderWorld> renderWorld_;
    bool gizmoMouseLeft_ {false};
    bool gizmoCaptured_ {false};
    bool pendingGizmoModePulse_ {false};
    bool pendingGizmoSpacePulse_ {false};
    void* rendererSurfaceHandle_ {nullptr};
    int rendererSurfaceWidth_ {0};
    int rendererSurfaceHeight_ {0};
    bool rendererSurfaceAttempted_ {false};
    bool rendererSurfaceReady_ {false};
    bool rendererSurfaceResizePending_ {false};
    QTimer* rendererSurfaceResizeTimer_ {nullptr};
    bool gpuMeshFrameRendered_ {false};
    std::optional<renderer::RendererStats> lastRendererStats_;
    std::vector<renderer::RenderMeshDraw> rendererMeshDraws_;
    std::vector<renderer::RenderLight> rendererLights_;
    std::vector<renderer::RenderColorVertex> rendererGizmoVertices_;
    std::vector<std::uint32_t> rendererGizmoIndices_;
    std::vector<renderer::RenderColorMeshDraw> rendererColorMeshDraws_;
};

} // namespace projectunity::editor
