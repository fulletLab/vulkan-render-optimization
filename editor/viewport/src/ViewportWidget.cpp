#include <projectunity/editor/ViewportWidget.hpp>

#include "ViewportRenderWorld.hpp"

#include <projectunity/core/Log.hpp>
#include <projectunity/renderer/IRenderer.hpp>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace projectunity::editor {
namespace {

constexpr float kNearPlane = 0.05F;
constexpr float kGridExtent = 20.0F;
constexpr float kMaxCameraDistance = 500.0F;

[[nodiscard]] bool isFinite(math::Vec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] float safeLength(math::Vec3 value)
{
    return std::sqrt(value.lengthSquared());
}

[[nodiscard]] math::Vec3 safeNormalized(math::Vec3 value, math::Vec3 fallback)
{
    const auto length = safeLength(value);
    if (length <= 0.00001F || !std::isfinite(length)) {
        return fallback;
    }
    return value / length;
}

} // namespace

ViewportWidget::ViewportWidget(ViewportMode mode, QWidget* parent)
    : QWidget(parent)
    , mode_(mode)
    , debugDrawBackend_(createIm3dDebugDrawBackend())
    , gizmoBackend_(createTinyGizmoBackend())
    , renderWorld_(std::make_unique<ViewportRenderWorld>())
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setMinimumSize(320, 220);
}

ViewportWidget::~ViewportWidget()
{
    if (renderer_ != nullptr && rendererSurfaceHandle_ != nullptr) {
        renderer_->releaseSurface(rendererSurfaceHandle_);
    }
}

void ViewportWidget::setAssetManager(const assets::IAssetManager* assetManager)
{
    assetManager_ = assetManager;
    if (renderWorld_ != nullptr) {
        renderWorld_->markDirty();
    }
    update();
}

void ViewportWidget::setRenderer(renderer::IRenderer* renderer)
{
    if (renderer_ != nullptr && rendererSurfaceHandle_ != nullptr) {
        renderer_->releaseSurface(rendererSurfaceHandle_);
    }
    renderer_ = renderer;
    rendererSurfaceHandle_ = nullptr;
    rendererSurfaceWidth_ = 0;
    rendererSurfaceHeight_ = 0;
    rendererSurfaceAttempted_ = false;
    rendererSurfaceReady_ = false;
    update();
}

void ViewportWidget::setEnvironmentSettings(renderer::RenderEnvironmentSettings settings)
{
    environmentSettings_ = settings;
    update();
}

void ViewportWidget::setScene(scene::Scene* scene)
{
    const auto sceneChanged = scene_ != scene;
    scene_ = scene;
    if (sceneChanged && renderWorld_ != nullptr) {
        renderWorld_->markDirty();
    }
    update();
}

void ViewportWidget::setSelectedEntity(scene::EntityId id)
{
    if (selectedEntityId_ != id) {
        gizmoMouseLeft_ = false;
        gizmoCaptured_ = false;
        gizmoBackend_->clear();
    }
    selectedEntityId_ = id;
    update();
}

void ViewportWidget::setSelectionCallback(std::function<void(scene::EntityId)> callback)
{
    selectionCallback_ = std::move(callback);
}

void ViewportWidget::setTransformEditedCallback(std::function<void(scene::EntityId)> callback)
{
    transformEditedCallback_ = std::move(callback);
}

void ViewportWidget::setTool(ViewportTool tool)
{
    if (tool_ == tool) {
        return;
    }

    tool_ = tool;
    pendingGizmoModePulse_ = tool_ != ViewportTool::Hand;
    if (tool_ == ViewportTool::Hand) {
        gizmoMouseLeft_ = false;
        gizmoCaptured_ = false;
    }
    core::logInfo(core::LogCategory::Editor, QStringLiteral("Viewport tool selected: %1").arg(toolModeName()).toStdString());
    update();
}

ViewportTool ViewportWidget::tool() const noexcept
{
    return tool_;
}

void ViewportWidget::setTransformSpace(TransformSpace space)
{
    if (transformSpace_ == space) {
        return;
    }

    transformSpace_ = space;
    pendingGizmoSpacePulse_ = true;
    core::logInfo(
        core::LogCategory::Editor,
        transformSpace_ == TransformSpace::Local ? "Viewport gizmo space selected: Local" : "Viewport gizmo space selected: Global");
    update();
}

TransformSpace ViewportWidget::transformSpace() const noexcept
{
    return transformSpace_;
}

void ViewportWidget::focusSelected()
{
    if (!selectedEntityId_.isValid()) {
        return;
    }

    const auto position = entityWorldPosition(selectedEntityId_);
    if (!position.has_value()) {
        core::logWarning(core::LogCategory::Editor, "Viewport focus ignored a missing selected entity");
        return;
    }

    camera_.target = *position;
    camera_.distance = std::clamp(camera_.distance, 4.0F, 20.0F);
    update();
    core::logInfo(core::LogCategory::Editor, "Viewport focused selected entity");
}

void ViewportWidget::setCameraForTesting(math::Vec3 target, float distance, float yawRadians, float pitchRadians)
{
    camera_.target = target;
    camera_.distance = std::clamp(distance, 0.1F, kMaxCameraDistance);
    camera_.yawRadians = yawRadians;
    camera_.pitchRadians = std::clamp(pitchRadians, -1.45F, 1.45F);
    update();
}

ViewportRay ViewportWidget::screenPointToRay(QPointF point) const
{
    const auto widgetWidth = std::max(1, width());
    const auto widgetHeight = std::max(1, height());
    const auto normalizedX = (2.0F * static_cast<float>(point.x()) / static_cast<float>(widgetWidth)) - 1.0F;
    const auto normalizedY = 1.0F - (2.0F * static_cast<float>(point.y()) / static_cast<float>(widgetHeight));
    const auto focal = 1.0F / std::tan(camera_.verticalFovRadians * 0.5F);

    auto direction = cameraForward()
        + cameraRight() * (normalizedX * aspectRatio() / focal)
        + cameraUp() * (normalizedY / focal);
    direction = safeNormalized(direction, cameraForward());

    return {cameraPosition(), direction};
}

void ViewportWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    const bool rendererFrameRendered = renderRendererFrame();
    if (rendererFrameRendered) {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    drawBackground(painter);
    drawDebugGeometry(painter);
    drawAxes(painter);
    drawHierarchyLinks(painter);
    drawEntities(painter);
    drawGizmo(painter);
    drawOverlay(painter);
}

void ViewportWidget::mousePressEvent(QMouseEvent* event)
{
    if (mode_ == ViewportMode::Game) {
        QWidget::mousePressEvent(event);
        return;
    }

    setFocus(Qt::MouseFocusReason);
    lastMousePosition_ = event->position().toPoint();

    const auto isAltOrbit = event->button() == Qt::LeftButton && event->modifiers().testFlag(Qt::AltModifier);
    if (isAltOrbit) {
        dragMode_ = DragMode::Orbit;
        event->accept();
        return;
    }

    if (event->button() == Qt::RightButton) {
        dragMode_ = DragMode::Look;
        event->accept();
        return;
    }

    if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && tool_ == ViewportTool::Hand)) {
        dragMode_ = DragMode::Pan;
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton) {
        gizmoMouseLeft_ = true;
        if (updateGizmoFrame()) {
            gizmoCaptured_ = true;
            core::logInfo(core::LogCategory::Editor, "Viewport gizmo interaction started");
            update();
            event->accept();
            return;
        }

        const auto picked = pickEntityAt(event->position());
        if (selectionCallback_) {
            selectionCallback_(picked.value_or(scene::EntityId {}));
        }
        core::logInfo(core::LogCategory::Editor, picked.has_value() ? "Viewport selected entity" : "Viewport cleared selection");
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void ViewportWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (mode_ == ViewportMode::Game) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    const auto currentPosition = event->position().toPoint();
    const auto delta = currentPosition - lastMousePosition_;
    lastMousePosition_ = currentPosition;

    if (dragMode_ == DragMode::Orbit) {
        orbitCamera(delta);
        event->accept();
        return;
    }

    if (dragMode_ == DragMode::Look) {
        lookCamera(delta);
        event->accept();
        return;
    }

    if (dragMode_ == DragMode::Pan) {
        panCamera(delta);
        event->accept();
        return;
    }

    if (gizmoCaptured_) {
        if (updateGizmoFrame()) {
            update();
        }
        event->accept();
        return;
    }

    QWidget::mouseMoveEvent(event);
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::RightButton && dragMode_ == DragMode::Look) {
        dragMode_ = DragMode::None;
        event->accept();
        return;
    }

    if (event->button() == Qt::MiddleButton && dragMode_ == DragMode::Pan) {
        dragMode_ = DragMode::None;
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && (dragMode_ == DragMode::Orbit || dragMode_ == DragMode::Pan)) {
        dragMode_ = DragMode::None;
    }

    if (event->button() == Qt::LeftButton) {
        const bool endedGizmoInteraction = gizmoCaptured_;
        gizmoMouseLeft_ = false;
        gizmoCaptured_ = false;
        (void)updateGizmoFrame();
        if (endedGizmoInteraction) {
            core::logInfo(core::LogCategory::Editor, "Viewport gizmo interaction ended");
            update();
            event->accept();
            return;
        }
    }
    QWidget::mouseReleaseEvent(event);
}

void ViewportWidget::resizeEvent(QResizeEvent* event)
{
    rendererSurfaceAttempted_ = false;
    rendererSurfaceReady_ = false;
    QWidget::resizeEvent(event);
}

void ViewportWidget::wheelEvent(QWheelEvent* event)
{
    if (mode_ == ViewportMode::Game) {
        QWidget::wheelEvent(event);
        return;
    }

    const auto wheelSteps = static_cast<float>(event->angleDelta().y()) / 120.0F;
    if (wheelSteps != 0.0F) {
        zoomCamera(wheelSteps);
    }
    event->accept();
}

void ViewportWidget::keyPressEvent(QKeyEvent* event)
{
    if (mode_ == ViewportMode::Game) {
        QWidget::keyPressEvent(event);
        return;
    }

    if (dragMode_ == DragMode::Look) {
        const auto fastMode = event->modifiers().testFlag(Qt::ShiftModifier);
        switch (event->key()) {
        case Qt::Key_W:
            moveCameraLocal({0.0F, 0.0F, 1.0F}, fastMode);
            event->accept();
            return;
        case Qt::Key_S:
            moveCameraLocal({0.0F, 0.0F, -1.0F}, fastMode);
            event->accept();
            return;
        case Qt::Key_A:
            moveCameraLocal({-1.0F, 0.0F, 0.0F}, fastMode);
            event->accept();
            return;
        case Qt::Key_D:
            moveCameraLocal({1.0F, 0.0F, 0.0F}, fastMode);
            event->accept();
            return;
        default:
            break;
        }
    }

    switch (event->key()) {
    case Qt::Key_Q:
        setTool(ViewportTool::Hand);
        event->accept();
        return;
    case Qt::Key_W:
        setTool(ViewportTool::Move);
        event->accept();
        return;
    case Qt::Key_E:
        setTool(ViewportTool::Rotate);
        event->accept();
        return;
    case Qt::Key_R:
        setTool(ViewportTool::Scale);
        event->accept();
        return;
    case Qt::Key_F:
        focusSelected();
        event->accept();
        return;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
}

math::Vec3 ViewportWidget::cameraForward() const
{
    const auto cosPitch = std::cos(camera_.pitchRadians);
    return safeNormalized({
        cosPitch * std::sin(camera_.yawRadians),
        std::sin(camera_.pitchRadians),
        cosPitch * std::cos(camera_.yawRadians),
    }, {0.0F, 0.0F, 1.0F});
}

math::Vec3 ViewportWidget::cameraRight() const
{
    return safeNormalized(math::cross({0.0F, 1.0F, 0.0F}, cameraForward()), {1.0F, 0.0F, 0.0F});
}

math::Vec3 ViewportWidget::cameraUp() const
{
    return safeNormalized(math::cross(cameraForward(), cameraRight()), {0.0F, 1.0F, 0.0F});
}

math::Vec3 ViewportWidget::cameraPosition() const
{
    return camera_.target - cameraForward() * camera_.distance;
}

float ViewportWidget::aspectRatio() const
{
    return static_cast<float>(std::max(1, width())) / static_cast<float>(std::max(1, height()));
}

ViewportWidget::ProjectedPoint ViewportWidget::projectPoint(math::Vec3 worldPosition) const
{
    const auto relative = worldPosition - cameraPosition();
    const auto x = math::dot(relative, cameraRight());
    const auto y = math::dot(relative, cameraUp());
    const auto z = math::dot(relative, cameraForward());

    if (z <= kNearPlane) {
        return {{}, z, false};
    }

    const auto focal = 1.0F / std::tan(camera_.verticalFovRadians * 0.5F);
    const auto ndcX = (x * focal / aspectRatio()) / z;
    const auto ndcY = (y * focal) / z;
    const auto screenX = (ndcX + 1.0F) * 0.5F * static_cast<float>(width());
    const auto screenY = (1.0F - ndcY) * 0.5F * static_cast<float>(height());
    const bool visible = ndcX >= -1.2F && ndcX <= 1.2F && ndcY >= -1.2F && ndcY <= 1.2F;

    return {QPointF(screenX, screenY), z, visible};
}

std::optional<math::Vec3> ViewportWidget::entityWorldPosition(scene::EntityId id) const
{
    if (scene_ == nullptr) {
        return std::nullopt;
    }

    math::Vec3 position {};
    std::optional<scene::EntityId> currentId = id;
    int hierarchyDepth = 0;

    while (currentId.has_value()) {
        const auto* entity = scene_->findEntity(*currentId);
        if (entity == nullptr) {
            return std::nullopt;
        }

        position += entity->transform.position;
        currentId = entity->parent;
        ++hierarchyDepth;
        if (hierarchyDepth > 256) {
            core::logWarning(core::LogCategory::Editor, "Viewport stopped resolving an excessively deep hierarchy");
            return std::nullopt;
        }
    }

    return position;
}

QString ViewportWidget::toolModeName() const
{
    switch (tool_) {
    case ViewportTool::Hand:
        return QStringLiteral("Hand");
    case ViewportTool::Move:
        return QStringLiteral("Move");
    case ViewportTool::Rotate:
        return QStringLiteral("Rotate");
    case ViewportTool::Scale:
        return QStringLiteral("Scale");
    }

    return QStringLiteral("Move");
}

EditorDebugFrame ViewportWidget::createDebugFrame() const
{
    const auto ray = screenPointToRay(QPointF(lastMousePosition_));
    EditorDebugFrame frame;
    frame.viewportWidth = static_cast<float>(std::max(1, width()));
    frame.viewportHeight = static_cast<float>(std::max(1, height()));
    frame.verticalFovRadians = camera_.verticalFovRadians;
    frame.cursorRayOrigin = ray.origin;
    frame.cursorRayDirection = ray.direction;
    frame.cameraPosition = cameraPosition();
    frame.cameraForward = cameraForward();
    frame.worldUp = {0.0F, 1.0F, 0.0F};
    return frame;
}

debug::DebugDrawList ViewportWidget::createDebugDrawList() const
{
    debug::DebugDrawList draw;
    debug::DebugGrid grid;
    grid.halfExtent = kGridExtent;
    grid.divisions = static_cast<std::size_t>(kGridExtent * 2.0F);
    grid.color = {0.54F, 0.58F, 0.64F, 0.24F};
    grid.centerColor = {0.68F, 0.72F, 0.80F, 0.58F};
    (void)draw.grid(grid);

    const auto forward = cameraForward();
    const auto rayStart = cameraPosition() + forward * std::max(0.35F, camera_.distance * 0.12F);
    (void)draw.ray(rayStart, forward, std::clamp(camera_.distance * 0.24F, 1.5F, 5.0F), {0.95F, 0.46F, 0.28F, 0.72F}, 1.5F);
    (void)draw.frustum(createCameraDebugFrustum(), {0.29F, 0.75F, 0.95F, 0.48F}, 1.0F);

    if (scene_ != nullptr && selectedEntityId_.isValid()) {
        if (const auto* selected = scene_->findEntity(selectedEntityId_)) {
            const auto position = entityWorldPosition(selectedEntityId_);
            if (position.has_value()) {
                const auto radius = entityPickRadius(*selected) * 1.08F;
                (void)draw.aabb(
                    *position - math::Vec3 {radius, radius, radius},
                    *position + math::Vec3 {radius, radius, radius},
                    {1.0F, 0.84F, 0.34F, 0.76F},
                    1.6F);
            }
        }
    }

    return draw;
}

debug::FrustumCorners ViewportWidget::createCameraDebugFrustum() const
{
    const auto nearDistance = std::clamp(camera_.distance * 0.06F, 0.35F, 0.9F);
    const auto farDistance = std::clamp(camera_.distance * 0.38F, 1.5F, 5.5F);
    const auto halfHeight = [this](float distance) {
        return std::tan(camera_.verticalFovRadians * 0.5F) * distance;
    };
    const auto cornersAt = [this, &halfHeight](float distance) {
        const auto center = cameraPosition() + cameraForward() * distance;
        const auto height = halfHeight(distance);
        const auto widthValue = height * aspectRatio();
        return std::array<math::Vec3, 4> {
            center - cameraRight() * widthValue - cameraUp() * height,
            center + cameraRight() * widthValue - cameraUp() * height,
            center + cameraRight() * widthValue + cameraUp() * height,
            center - cameraRight() * widthValue + cameraUp() * height,
        };
    };

    const auto nearCorners = cornersAt(nearDistance);
    const auto farCorners = cornersAt(farDistance);
    return {
        nearCorners[0],
        nearCorners[1],
        nearCorners[2],
        nearCorners[3],
        farCorners[0],
        farCorners[1],
        farCorners[2],
        farCorners[3],
    };
}

EditorGizmoFrame ViewportWidget::createGizmoFrame() const
{
    EditorGizmoFrame frame;
    const auto ray = screenPointToRay(QPointF(lastMousePosition_));

    frame.mouseLeft = gizmoMouseLeft_;
    frame.toolChanged = pendingGizmoModePulse_;
    frame.spaceChanged = pendingGizmoSpacePulse_;
    frame.tool = tool_ == ViewportTool::Rotate
        ? EditorGizmoTool::Rotate
        : tool_ == ViewportTool::Scale ? EditorGizmoTool::Scale : EditorGizmoTool::Move;
    frame.space = transformSpace_ == TransformSpace::Local ? EditorGizmoSpace::Local : EditorGizmoSpace::Global;
    frame.viewportWidth = static_cast<float>(std::max(1, width()));
    frame.viewportHeight = static_cast<float>(std::max(1, height()));
    frame.verticalFovRadians = camera_.verticalFovRadians;
    frame.nearPlane = kNearPlane;
    frame.farPlane = kMaxCameraDistance * 4.0F;
    frame.rayOrigin = ray.origin;
    frame.rayDirection = ray.direction;
    frame.cameraPosition = cameraPosition();
    frame.cameraRight = cameraRight();
    frame.cameraUp = cameraUp();
    frame.cameraForward = cameraForward();
    return frame;
}

EditorGizmoTransform ViewportWidget::createSelectedGizmoTransform() const
{
    if (scene_ == nullptr || !selectedEntityId_.isValid()) {
        return {};
    }

    const auto* entity = scene_->findEntity(selectedEntityId_);
    const auto worldPosition = entityWorldPosition(selectedEntityId_);
    if (entity == nullptr || !worldPosition.has_value()) {
        return {};
    }

    return {
        *worldPosition,
        entity->transform.rotationEuler,
        entity->transform.scale,
    };
}

bool ViewportWidget::applySelectedGizmoTransform(const EditorGizmoTransform& transform)
{
    if (scene_ == nullptr || !selectedEntityId_.isValid()) {
        return false;
    }

    auto* entity = scene_->findEntity(selectedEntityId_);
    if (entity == nullptr) {
        return false;
    }

    auto updated = entity->transform;
    const auto worldPosition = transform.position;
    auto localPosition = worldPosition;
    if (entity->parent.has_value()) {
        const auto parentPosition = entityWorldPosition(*entity->parent);
        if (!parentPosition.has_value()) {
            core::logWarning(core::LogCategory::Editor, "Viewport gizmo rejected transform with missing parent world position");
            return false;
        }
        localPosition -= *parentPosition;
    }

    const auto rotationEuler = transform.rotationEuler;
    const auto scale = transform.scale;
    if (!isFinite(localPosition) || !isFinite(rotationEuler) || !isFinite(scale)) {
        core::logWarning(core::LogCategory::Editor, "Viewport gizmo rejected a non-finite transform");
        return false;
    }

    updated.position = localPosition;
    updated.rotationEuler = rotationEuler;
    updated.scale = scale;
    if (math::nearlyEqual(updated.position, entity->transform.position)
        && math::nearlyEqual(updated.rotationEuler, entity->transform.rotationEuler)
        && math::nearlyEqual(updated.scale, entity->transform.scale)) {
        return false;
    }

    if (!scene_->setTransform(selectedEntityId_, updated)) {
        core::logWarning(core::LogCategory::Editor, "Viewport gizmo failed to apply selected entity transform");
        return false;
    }

    if (transformEditedCallback_) {
        transformEditedCallback_(selectedEntityId_);
    }
    return true;
}

bool ViewportWidget::updateGizmoFrame()
{
    if (mode_ != ViewportMode::Scene || scene_ == nullptr || !selectedEntityId_.isValid() || tool_ == ViewportTool::Hand) {
        gizmoBackend_->clear();
        return false;
    }

    if (scene_->findEntity(selectedEntityId_) == nullptr) {
        gizmoBackend_->clear();
        return false;
    }

    auto transform = createSelectedGizmoTransform();
    const auto activated = gizmoBackend_->update(
        "entity-" + std::to_string(selectedEntityId_.value()),
        createGizmoFrame(),
        transform);
    pendingGizmoModePulse_ = false;
    pendingGizmoSpacePulse_ = false;

    (void)applySelectedGizmoTransform(transform);

    return activated;
}

} // namespace projectunity::editor
