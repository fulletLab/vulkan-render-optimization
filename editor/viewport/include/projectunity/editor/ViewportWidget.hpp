#pragma once

#include <projectunity/assets/AssetManager.hpp>
#include <projectunity/debug/DebugDraw.hpp>
#include <projectunity/editor/IEditorDebugDrawBackend.hpp>
#include <projectunity/editor/IEditorGizmoBackend.hpp>
#include <projectunity/editor/ViewportQualitySettings.hpp>
#include <projectunity/math/Vec3.hpp>
#include <projectunity/renderer/RenderShadowSetup.hpp>
#include <projectunity/renderer/RendererTypes.hpp>
#include <projectunity/scene/Scene.hpp>
#include <projectunity/scripting/InputState.hpp>

#include <array>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <cstdint>
#include <vector>

#include <QPoint>
#include <QPointF>
#include <QWidget>

class QKeyEvent;
class QFocusEvent;
class QMouseEvent;
class QPainter;
class QResizeEvent;
class QTimer;
class QWheelEvent;

namespace projectunity::renderer {
class IRenderer;
} // namespace projectunity::renderer

namespace projectunity::scripting {
class ScriptRuntime;
} // namespace projectunity::scripting

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

enum class ViewportPickMode : std::uint8_t {
    AssetOwner,
    SubObject,
};

struct ViewportRay {
    math::Vec3 origin;
    math::Vec3 direction;
};

enum class ViewportTerrainBrushPhase : std::uint8_t {
    Hover,
    Begin,
    Drag,
    End,
};

struct ViewportTerrainBrushEvent {
    ViewportRay ray;
    ViewportTerrainBrushPhase phase {ViewportTerrainBrushPhase::Hover};
    bool lowerModifier {false};
};

struct ViewportPickResult {
    scene::EntityId ownerEntityId;
    scene::EntityId selectedEntityId;
    assets::AssetId assetId;
    std::uint64_t subObjectId {0};
    std::optional<std::uint32_t> subObjectIndex;
    float distance {0.0F};
};

class ViewportWidget final : public QWidget, public scripting::InputService {
public:
    explicit ViewportWidget(ViewportMode mode, QWidget* parent = nullptr);
    ~ViewportWidget() override;

    void setAssetManager(const assets::IAssetManager* assetManager);
    void setRenderer(renderer::IRenderer* renderer);
    void setEnvironmentSettings(renderer::RenderEnvironmentSettings settings);
    void setEditorSunLight(renderer::RenderLight light);
    void setShadowUpdateMode(renderer::RenderShadowUpdateMode mode);
    [[nodiscard]] renderer::RenderShadowUpdateMode shadowUpdateMode() const noexcept;
    void setVSyncEnabled(bool enabled);
    [[nodiscard]] bool vSyncEnabled() const noexcept;
    void setFrameRateLimitFps(int fps);
    [[nodiscard]] int frameRateLimitFps() const noexcept;
    void setAssetLodSettings(ViewportAssetLodSettings settings);
    [[nodiscard]] ViewportAssetLodSettings assetLodSettings() const noexcept;
    void setTextureDebugSettings(renderer::RenderTextureDebugSettings settings);
    void setScene(scene::Scene* scene);
    void setSelectedEntity(scene::EntityId id);
    void setSelectionCallback(std::function<void(scene::EntityId)> callback);
    void setPickResultCallback(std::function<void(ViewportPickResult)> callback);
    void setPickMode(ViewportPickMode mode);
    [[nodiscard]] ViewportPickMode pickMode() const noexcept;
    void setTransformEditedCallback(std::function<void(scene::EntityId)> callback);
    void setTerrainBrushCallback(
        std::function<std::optional<math::Vec3>(const ViewportTerrainBrushEvent&)> callback);
    void setTerrainBrushEnabled(bool enabled);
    [[nodiscard]] bool terrainBrushEnabled() const noexcept;
    void setTerrainBrushRadius(float radius);
    void setTool(ViewportTool tool);
    [[nodiscard]] ViewportTool tool() const noexcept;
    void setTransformSpace(TransformSpace space);
    [[nodiscard]] TransformSpace transformSpace() const noexcept;
    void setMeshWireOverlayEnabled(bool enabled);
    [[nodiscard]] bool meshWireOverlayEnabled() const noexcept;
    void setAssetXrayDebugEnabled(bool enabled) { if (assetXrayDebugEnabled_ != enabled) { assetXrayDebugEnabled_ = enabled; update(); } }
    [[nodiscard]] bool assetXrayDebugEnabled() const noexcept { return assetXrayDebugEnabled_; }
    void setLodDebugOverlayEnabled(bool enabled) { if (lodDebugOverlayEnabled_ != enabled) { lodDebugOverlayEnabled_ = enabled; update(); } }
    [[nodiscard]] bool lodDebugOverlayEnabled() const noexcept { return lodDebugOverlayEnabled_; }
    void setShadowDebugOverlayEnabled(bool enabled) { if (shadowDebugOverlayEnabled_ != enabled) { shadowDebugOverlayEnabled_ = enabled; update(); } }
    [[nodiscard]] bool shadowDebugOverlayEnabled() const noexcept { return shadowDebugOverlayEnabled_; }
    void setSourceObjectDebugOverlayEnabled(bool enabled) { if (sourceObjectDebugOverlayEnabled_ != enabled) { sourceObjectDebugOverlayEnabled_ = enabled; update(); } }
    [[nodiscard]] bool sourceObjectDebugOverlayEnabled() const noexcept { return sourceObjectDebugOverlayEnabled_; }
    void setSunDirectionDebugEnabled(bool enabled) { if (sunDirectionDebugEnabled_ != enabled) { sunDirectionDebugEnabled_ = enabled; update(); } }
    [[nodiscard]] bool sunDirectionDebugEnabled() const noexcept { return sunDirectionDebugEnabled_; }
    void focusSelected();

    [[nodiscard]] ViewportRay screenPointToRay(QPointF point) const;
    [[nodiscard]] std::optional<ViewportPickResult> pickResultAt(QPointF point) const;
    [[nodiscard]] std::optional<scene::EntityId> pickEntityAt(QPointF point) const;
    [[nodiscard]] bool runSelfTest(QString* errorMessage);
    [[nodiscard]] const renderer::RendererStats* lastRendererStats() const noexcept;
    void setCameraForTesting(math::Vec3 target, float distance, float yawRadians, float pitchRadians);
    void setGameInputEnabled(bool enabled);
    void setGameCameraEntity(scene::EntityId id);
    void setGameScriptRuntime(scripting::ScriptRuntime* runtime);
    void setGameRuntimeSnapshotEnabled(bool enabled);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

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
    [[nodiscard]] bool handleGameKey(QKeyEvent* event, bool pressed);
    void handleGameMousePress(QMouseEvent* event);
    void handleGameMouseMove(QMouseEvent* event);
    void handleGameMouseRelease(QMouseEvent* event);
    void tickGameScripts();
    void setMouseCaptured(bool captured) override;
    [[nodiscard]] bool isMouseCaptured() const noexcept override;
    void centerGameMouseCursor();

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
    renderer::RenderLight editorSunLight_;
    renderer::RenderShadowUpdateMode shadowUpdateMode_ {renderer::RenderShadowUpdateMode::Off};
    std::optional<renderer::RenderShadowMapSelection> frozenShadowSelection_;
    scene::Scene* scene_ {nullptr};
    scene::EntityId selectedEntityId_;
    std::function<void(scene::EntityId)> selectionCallback_;
    std::function<void(ViewportPickResult)> pickResultCallback_;
    std::function<void(scene::EntityId)> transformEditedCallback_;
    std::function<std::optional<math::Vec3>(const ViewportTerrainBrushEvent&)> terrainBrushCallback_;
    ViewportPickMode pickMode_ {ViewportPickMode::AssetOwner};
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
    bool terrainBrushEnabled_ {false};
    bool terrainBrushDragging_ {false};
    float terrainBrushRadius_ {6.0F};
    std::optional<math::Vec3> terrainBrushHit_;
    bool pendingGizmoModePulse_ {false};
    bool pendingGizmoSpacePulse_ {false};
    void* rendererSurfaceHandle_ {nullptr};
    int rendererSurfaceWidth_ {0};
    int rendererSurfaceHeight_ {0};
    bool rendererSurfaceAttempted_ {false};
    bool rendererSurfaceReady_ {false};
    bool rendererSurfaceResizePending_ {false};
    QTimer* rendererSurfaceResizeTimer_ {nullptr};
    QTimer* frameRateLimitTimer_ {nullptr};
    std::chrono::steady_clock::time_point lastRendererFrameTime_ {};
    bool hasLastRendererFrameTime_ {false};
    bool vSyncEnabled_ {true};
    int frameRateLimitFps_ {0};
    ViewportAssetLodSettings assetLodSettings_ {viewportAssetLodSettingsFromEnvironment()};
    renderer::RenderTextureDebugSettings textureDebugSettings_;
    bool gpuMeshFrameRendered_ {false};
    bool meshWireOverlayEnabled_ {false};
    bool assetXrayDebugEnabled_ {false};
    bool lodDebugOverlayEnabled_ {false};
    bool shadowDebugOverlayEnabled_ {false};
    bool sourceObjectDebugOverlayEnabled_ {false};
    bool sunDirectionDebugEnabled_ {false};
    bool gameInputEnabled_ {false};
    bool gameRuntimeSnapshotEnabled_ {false};
    scripting::InputState gameInputState_;
    scripting::ScriptRuntime* gameScriptRuntime_ {nullptr};
    scene::EntityId gameCameraEntityId_;
    QTimer* gameScriptTimer_ {nullptr};
    std::uint64_t cullingLogFrameCounter_ {0};
    std::uint64_t lastCullingLogFrame_ {0};
    std::uint64_t lastCullingLogSignature_ {0};
    std::uint64_t lastSourceModelDebugSignature_ {0};
    std::optional<renderer::RendererStats> lastRendererStats_;
    std::vector<renderer::RenderMeshDraw> rendererMeshDraws_;
    std::vector<renderer::RenderMeshDraw> rendererShadowMeshDraws_;
    std::uint64_t rendererShadowCandidateInstances_ {0};
    std::uint64_t rendererShadowPolicyRejectedInstances_ {0};
    std::uint64_t rendererShadowVisibleInstances_ {0};
    std::uint64_t rendererShadowOnlyCandidateInstances_ {0};
    std::uint64_t rendererShadowOnlyRejectedInstances_ {0};
    std::vector<renderer::RenderLight> rendererLights_;
    std::vector<renderer::RenderColorVertex> rendererGizmoVertices_;
    std::vector<std::uint32_t> rendererGizmoIndices_;
    std::vector<renderer::RenderColorMeshDraw> rendererColorMeshDraws_;
};

} // namespace projectunity::editor
