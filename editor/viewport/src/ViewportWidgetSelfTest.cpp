#include <projectunity/editor/ViewportWidget.hpp>

#include <QImage>
#include <QPainter>
#include <QPoint>
#include <QPointF>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace projectunity::editor {
namespace {

[[nodiscard]] bool isFinite(math::Vec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool transformChangedForTool(
    const scene::TransformComponent& before,
    const scene::TransformComponent& after,
    ViewportTool tool)
{
    switch (tool) {
    case ViewportTool::Move:
        return !math::nearlyEqual(before.position, after.position);
    case ViewportTool::Rotate:
        return !math::nearlyEqual(before.rotationEuler, after.rotationEuler);
    case ViewportTool::Scale:
        return !math::nearlyEqual(before.scale, after.scale);
    case ViewportTool::Hand:
        return false;
    }

    return false;
}

} // namespace

bool ViewportWidget::runSelfTest(QString* errorMessage)
{
    auto fail = [errorMessage](const QString& message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };

    if (width() < 64 || height() < 64) {
        return fail(QStringLiteral("Viewport has invalid dimensions for projection"));
    }

    if (scene_ == nullptr) {
        return fail(QStringLiteral("Viewport scene pointer was not assigned"));
    }

    const auto centerRay = screenPointToRay(QPointF(width() * 0.5, height() * 0.5));
    if (!isFinite(centerRay.origin) || !isFinite(centerRay.direction) || centerRay.direction.lengthSquared() < 0.99F) {
        return fail(QStringLiteral("Viewport ray generation returned invalid data"));
    }

    if (mode_ == ViewportMode::Scene) {
        const auto savedCamera = camera_;
        const auto savedRay = centerRay;
        orbitCamera(QPoint(20, -10));
        panCamera(QPoint(8, 4));
        zoomCamera(1.0F);
        lookCamera(QPoint(12, -6));
        moveCameraLocal({0.0F, 0.0F, 1.0F}, false);
        moveCameraLocal({-1.0F, 0.0F, 0.0F}, true);
        const auto movedRay = screenPointToRay(QPointF(width() * 0.5, height() * 0.5));
        const auto changedDirection = math::distanceSquared(savedRay.direction, movedRay.direction) > 0.00001F;
        const auto changedOrigin = math::distanceSquared(savedRay.origin, movedRay.origin) > 0.00001F;
        camera_ = savedCamera;
        update();

        if (!changedDirection || !changedOrigin) {
            return fail(QStringLiteral("Viewport camera navigation did not update ray state"));
        }

        const auto debugDraw = createDebugDrawList();
        if (debugDraw.lineCount() == 0
            || !debugDrawBackend_->update(createDebugFrame(), debugDraw.lines())
            || debugDrawBackend_->lineMesh().lines.empty()) {
            return fail(QStringLiteral("Viewport Im3d debug backend emitted no line geometry"));
        }

        if (selectedEntityId_.isValid()) {
            const auto* selected = scene_->findEntity(selectedEntityId_);
            if (selected != nullptr && selected->meshRenderer.has_value()) {
                const auto model = assetManager_ == nullptr ? nullptr : assetManager_->model(selected->meshRenderer->modelAssetId);
                if (model == nullptr || model->primitives.empty() || model->textures.empty()) {
                    return fail(QStringLiteral("Viewport selected mesh renderer could not resolve imported geometry"));
                }
                const auto selectedPosition = entityWorldPosition(selectedEntityId_);
                QImage drawProbe(std::max(1, width()), std::max(1, height()), QImage::Format_RGBA8888);
                drawProbe.fill(Qt::transparent);
                QPainter drawProbePainter(&drawProbe);
                if (!selectedPosition.has_value() || !drawMeshEntity(drawProbePainter, *selected, *selectedPosition)) {
                    return fail(QStringLiteral("Viewport imported mesh produced no drawable triangles"));
                }
            }

            const auto savedTool = tool_;
            const auto savedSpace = transformSpace_;
            const auto verifyGizmoMode = [this, &fail](ViewportTool tool, const QString& name) {
                setTool(tool);
                (void)updateGizmoFrame();
                if (gizmoBackend_->mesh().vertices.empty() || gizmoBackend_->mesh().triangles.empty()) {
                    return fail(QStringLiteral("Viewport gizmo emitted no geometry for %1").arg(name));
                }
                return true;
            };

            if (!verifyGizmoMode(ViewportTool::Move, QStringLiteral("Move"))
                || !verifyGizmoMode(ViewportTool::Rotate, QStringLiteral("Rotate"))
                || !verifyGizmoMode(ViewportTool::Scale, QStringLiteral("Scale"))) {
                return false;
            }

            QString gizmoDragError;
            if (!runGizmoDragSelfTest(ViewportTool::Move, &gizmoDragError)
                || !runGizmoDragSelfTest(ViewportTool::Rotate, &gizmoDragError)
                || !runGizmoDragSelfTest(ViewportTool::Scale, &gizmoDragError)) {
                return fail(gizmoDragError);
            }

            setTransformSpace(TransformSpace::Global);
            (void)updateGizmoFrame();
            setTransformSpace(TransformSpace::Local);
            (void)updateGizmoFrame();
            setTool(savedTool);
            setTransformSpace(savedSpace);
            (void)updateGizmoFrame();
        }
    }

    for (const auto& entity : scene_->entities()) {
        const auto position = entityWorldPosition(entity.id);
        if (!position.has_value()) {
            continue;
        }

        const auto projected = projectPoint(*position);
        if (!projected.visible) {
            continue;
        }

        const auto picked = pickEntityAt(projected.point);
        if (!picked.has_value() || *picked != entity.id) {
            return fail(QStringLiteral("Viewport ray picking did not return the projected entity"));
        }
        return true;
    }

    return true;
}

bool ViewportWidget::runGizmoDragSelfTest(ViewportTool tool, QString* errorMessage)
{
    auto fail = [errorMessage](const QString& message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };

    if (tool == ViewportTool::Hand || scene_ == nullptr || !selectedEntityId_.isValid()) {
        return fail(QStringLiteral("Viewport gizmo drag self-test received invalid input"));
    }

    auto* entity = scene_->findEntity(selectedEntityId_);
    if (entity == nullptr) {
        return fail(QStringLiteral("Viewport gizmo drag self-test selected entity was missing"));
    }

    const auto originalTransform = entity->transform;
    const auto originalMousePosition = lastMousePosition_;
    const auto originalMouseLeft = gizmoMouseLeft_;
    const auto originalCaptured = gizmoCaptured_;

    setTool(tool);
    gizmoMouseLeft_ = false;
    (void)updateGizmoFrame();

    std::vector<QPoint> candidatePoints;
    candidatePoints.reserve(96);
    const auto& gizmoMesh = gizmoBackend_->mesh();
    for (const auto& triangle : gizmoMesh.triangles) {
        const auto indices = std::array<std::uint32_t, 3> {triangle.x, triangle.y, triangle.z};
        if (indices[0] >= gizmoMesh.vertices.size()
            || indices[1] >= gizmoMesh.vertices.size()
            || indices[2] >= gizmoMesh.vertices.size()) {
            continue;
        }

        QPointF centroid;
        bool visible = true;
        for (const auto index : indices) {
            const auto projected = projectPoint(gizmoMesh.vertices[index].position);
            if (!projected.visible) {
                visible = false;
                break;
            }
            centroid += projected.point;
        }

        if (visible) {
            candidatePoints.push_back((centroid / 3.0).toPoint());
        }
        if (candidatePoints.size() == candidatePoints.capacity()) {
            break;
        }
    }

    constexpr std::array<QPoint, 8> dragDeltas {{
        QPoint(96, 0),
        QPoint(-96, 0),
        QPoint(0, 96),
        QPoint(0, -96),
        QPoint(72, 72),
        QPoint(-72, 72),
        QPoint(72, -72),
        QPoint(-72, -72),
    }};

    bool changed = false;
    for (const auto& point : candidatePoints) {
        (void)scene_->setTransform(selectedEntityId_, originalTransform);
        lastMousePosition_ = point;
        gizmoMouseLeft_ = false;
        (void)updateGizmoFrame();
        gizmoMouseLeft_ = true;
        if (!updateGizmoFrame()) {
            gizmoMouseLeft_ = false;
            (void)updateGizmoFrame();
            continue;
        }

        for (const auto& delta : dragDeltas) {
            lastMousePosition_ = point + delta;
            (void)updateGizmoFrame();
            const auto* edited = scene_->findEntity(selectedEntityId_);
            if (edited != nullptr && transformChangedForTool(originalTransform, edited->transform, tool)) {
                changed = true;
                break;
            }
        }

        gizmoMouseLeft_ = false;
        (void)updateGizmoFrame();
        if (changed) {
            break;
        }
    }

    (void)scene_->setTransform(selectedEntityId_, originalTransform);
    if (transformEditedCallback_) {
        transformEditedCallback_(selectedEntityId_);
    }
    lastMousePosition_ = originalMousePosition;
    gizmoMouseLeft_ = originalMouseLeft;
    gizmoCaptured_ = originalCaptured;
    (void)updateGizmoFrame();

    if (!changed) {
        return fail(QStringLiteral("Viewport tinygizmo drag self-test did not edit %1 transform").arg(toolModeName()));
    }
    return true;
}

} // namespace projectunity::editor
