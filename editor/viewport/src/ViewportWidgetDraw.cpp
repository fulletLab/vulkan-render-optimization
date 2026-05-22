#include <projectunity/editor/ViewportWidget.hpp>

#include <QFontMetrics>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QTransform>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace projectunity::editor {
namespace {

constexpr float kNearPlane = 0.05F;

struct DrawTriangle {
    QPolygonF polygon;
    std::array<QPointF, 3> projectedPoints;
    std::array<std::array<float, 2>, 3> texCoords;
    std::array<float, 4> baseColor {1.0F, 1.0F, 1.0F, 1.0F};
    const assets::TextureAsset* texture {nullptr};
    float depth {0.0F};
};

[[nodiscard]] QColor withAlpha(QColor color, int alpha)
{
    color.setAlpha(alpha);
    return color;
}

[[nodiscard]] QString entityLabel(const scene::Entity& entity)
{
    return QString::fromStdString(entity.name);
}

[[nodiscard]] QColor colorFromGizmo(const std::array<float, 4>& value)
{
    return QColor::fromRgbF(
        std::clamp(static_cast<qreal>(value[0]), 0.0, 1.0),
        std::clamp(static_cast<qreal>(value[1]), 0.0, 1.0),
        std::clamp(static_cast<qreal>(value[2]), 0.0, 1.0),
        std::clamp(static_cast<qreal>(value[3]), 0.0, 1.0));
}

[[nodiscard]] float radians(float degrees)
{
    return degrees * 0.01745329251994329577F;
}

[[nodiscard]] math::Vec3 rotateEuler(math::Vec3 value, math::Vec3 rotationEuler)
{
    const auto sinX = std::sin(radians(rotationEuler.x));
    const auto cosX = std::cos(radians(rotationEuler.x));
    const auto sinY = std::sin(radians(rotationEuler.y));
    const auto cosY = std::cos(radians(rotationEuler.y));
    const auto sinZ = std::sin(radians(rotationEuler.z));
    const auto cosZ = std::cos(radians(rotationEuler.z));

    value = {value.x, value.y * cosX - value.z * sinX, value.y * sinX + value.z * cosX};
    value = {value.x * cosY + value.z * sinY, value.y, -value.x * sinY + value.z * cosY};
    return {value.x * cosZ - value.y * sinZ, value.x * sinZ + value.y * cosZ, value.z};
}

[[nodiscard]] math::Vec3 scaled(math::Vec3 value, math::Vec3 scale)
{
    return {value.x * scale.x, value.y * scale.y, value.z * scale.z};
}

[[nodiscard]] std::size_t sourceTriangleCount(const assets::ModelAsset& model)
{
    std::size_t count = 0;
    for (const auto& primitive : model.primitives) {
        count += primitive.indices.size() / 3U;
    }
    return count;
}

[[nodiscard]] bool textureUsable(const assets::TextureAsset* texture)
{
    return texture != nullptr
        && texture->width > 0
        && texture->height > 0
        && texture->rgba8.size() >= static_cast<std::size_t>(texture->width) * static_cast<std::size_t>(texture->height) * 4U;
}

[[nodiscard]] QColor baseColorToQColor(const std::array<float, 4>& color)
{
    return QColor::fromRgbF(
        std::clamp(static_cast<qreal>(color[0]), 0.0, 1.0),
        std::clamp(static_cast<qreal>(color[1]), 0.0, 1.0),
        std::clamp(static_cast<qreal>(color[2]), 0.0, 1.0),
        std::clamp(static_cast<qreal>(color[3]), 0.0, 1.0));
}

[[nodiscard]] QPointF texturePoint(const assets::TextureAsset& texture, std::array<float, 2> uv)
{
    return {
        static_cast<qreal>(uv[0]) * static_cast<qreal>(texture.width),
        (1.0 - static_cast<qreal>(uv[1])) * static_cast<qreal>(texture.height),
    };
}

[[nodiscard]] QImage textureImage(const assets::TextureAsset& texture)
{
    return QImage(
        texture.rgba8.data(),
        static_cast<int>(texture.width),
        static_cast<int>(texture.height),
        static_cast<int>(texture.width * 4U),
        QImage::Format_RGBA8888);
}

} // namespace

void ViewportWidget::drawBackground(QPainter& painter) const
{
    painter.fillRect(rect(), QColor(30, 32, 36));
    QLinearGradient gradient(0.0, 0.0, 0.0, static_cast<qreal>(height()));
    gradient.setColorAt(0.0, QColor(42, 45, 51));
    gradient.setColorAt(1.0, QColor(23, 25, 29));
    painter.fillRect(rect(), gradient);
}

void ViewportWidget::drawDebugGeometry(QPainter& painter)
{
    if (mode_ != ViewportMode::Scene) {
        return;
    }

    const auto draw = createDebugDrawList();
    if (!debugDrawBackend_->update(createDebugFrame(), draw.lines())) {
        return;
    }

    painter.save();
    for (const auto& line : debugDrawBackend_->lineMesh().lines) {
        const auto projectedStart = projectPoint(line.start.position);
        const auto projectedEnd = projectPoint(line.end.position);
        if (projectedStart.depth <= kNearPlane || projectedEnd.depth <= kNearPlane) {
            continue;
        }

        QPen pen(QColor::fromRgbF(
            std::clamp(static_cast<qreal>(line.start.color[0]), 0.0, 1.0),
            std::clamp(static_cast<qreal>(line.start.color[1]), 0.0, 1.0),
            std::clamp(static_cast<qreal>(line.start.color[2]), 0.0, 1.0),
            std::clamp(static_cast<qreal>(line.start.color[3]), 0.0, 1.0)));
        pen.setWidthF(std::max(static_cast<qreal>(line.start.size), 1.0));
        painter.setPen(pen);
        painter.drawLine(projectedStart.point, projectedEnd.point);
    }
    painter.restore();
}

void ViewportWidget::drawAxes(QPainter& painter) const
{
    const auto drawAxis = [this, &painter](math::Vec3 end, QColor color) {
        const auto startPoint = projectPoint({0.0F, 0.0F, 0.0F});
        const auto endPoint = projectPoint(end);
        if (startPoint.depth <= kNearPlane || endPoint.depth <= kNearPlane) {
            return;
        }
        painter.setPen(QPen(color, 2));
        painter.drawLine(startPoint.point, endPoint.point);
    };

    drawAxis({3.0F, 0.0F, 0.0F}, QColor(220, 80, 80));
    drawAxis({0.0F, 3.0F, 0.0F}, QColor(95, 190, 110));
    drawAxis({0.0F, 0.0F, 3.0F}, QColor(80, 135, 230));
}

void ViewportWidget::drawHierarchyLinks(QPainter& painter) const
{
    if (scene_ == nullptr) {
        return;
    }

    painter.setPen(QPen(QColor(160, 170, 185, 95), 1));
    for (const auto& entity : scene_->entities()) {
        if (!entity.parent.has_value()) {
            continue;
        }

        const auto childPosition = entityWorldPosition(entity.id);
        const auto parentPosition = entityWorldPosition(*entity.parent);
        if (!childPosition.has_value() || !parentPosition.has_value()) {
            continue;
        }

        const auto childPoint = projectPoint(*childPosition);
        const auto parentPoint = projectPoint(*parentPosition);
        if (childPoint.depth > kNearPlane && parentPoint.depth > kNearPlane) {
            painter.drawLine(parentPoint.point, childPoint.point);
        }
    }
}

void ViewportWidget::drawEntities(QPainter& painter) const
{
    if (scene_ == nullptr) {
        return;
    }

    for (const auto& entity : scene_->entities()) {
        if (entity.id != selectedEntityId_) {
            drawEntity(painter, entity);
        }
    }
    if (selectedEntityId_.isValid()) {
        if (const auto* selected = scene_->findEntity(selectedEntityId_)) {
            drawEntity(painter, *selected);
        }
    }
}

void ViewportWidget::drawEntity(QPainter& painter, const scene::Entity& entity) const
{
    const auto position = entityWorldPosition(entity.id);
    if (!position.has_value()) {
        return;
    }

    if ((gpuMeshFrameRendered_ && entity.meshRenderer.has_value()) || drawMeshEntity(painter, entity, *position)) {
        const auto center = projectPoint(*position);
        if (center.visible) {
            painter.setBrush(entity.id == selectedEntityId_ ? QColor(255, 213, 95) : QColor(190, 205, 230));
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(center.point, entity.id == selectedEntityId_ ? 4.0 : 2.5, entity.id == selectedEntityId_ ? 4.0 : 2.5);
            painter.setPen(withAlpha(QColor(226, 231, 240), entity.id == selectedEntityId_ ? 230 : 170));
            painter.drawText(center.point + QPointF(8.0, -8.0), entityLabel(entity));
        }
        return;
    }

    const auto halfSize = entityPickRadius(entity);
    const std::array<math::Vec3, 8> corners {
        *position + math::Vec3 {-halfSize, -halfSize, -halfSize},
        *position + math::Vec3 {halfSize, -halfSize, -halfSize},
        *position + math::Vec3 {halfSize, halfSize, -halfSize},
        *position + math::Vec3 {-halfSize, halfSize, -halfSize},
        *position + math::Vec3 {-halfSize, -halfSize, halfSize},
        *position + math::Vec3 {halfSize, -halfSize, halfSize},
        *position + math::Vec3 {halfSize, halfSize, halfSize},
        *position + math::Vec3 {-halfSize, halfSize, halfSize},
    };
    constexpr std::array<std::pair<int, int>, 12> edges {{
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7},
    }};

    const bool isSelected = entity.id == selectedEntityId_;
    painter.setPen(QPen(isSelected ? QColor(255, 213, 95) : QColor(190, 205, 230), isSelected ? 3 : 2));
    for (const auto& edge : edges) {
        const auto start = projectPoint(corners[static_cast<std::size_t>(edge.first)]);
        const auto end = projectPoint(corners[static_cast<std::size_t>(edge.second)]);
        if (start.depth > kNearPlane && end.depth > kNearPlane) {
            painter.drawLine(start.point, end.point);
        }
    }

    const auto center = projectPoint(*position);
    if (center.visible) {
        painter.setBrush(isSelected ? QColor(255, 213, 95) : QColor(190, 205, 230));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(center.point, isSelected ? 5.0 : 3.5, isSelected ? 5.0 : 3.5);
        painter.setPen(withAlpha(QColor(226, 231, 240), isSelected ? 230 : 170));
        painter.drawText(center.point + QPointF(8.0, -8.0), entityLabel(entity));
    }
}

bool ViewportWidget::drawMeshEntity(QPainter& painter, const scene::Entity& entity, math::Vec3 position) const
{
    if (assetManager_ == nullptr || !entity.meshRenderer.has_value()) {
        return false;
    }

    const auto model = assetManager_->model(entity.meshRenderer->modelAssetId);
    if (model == nullptr || model->primitives.empty() || model->materials.empty()) {
        return false;
    }

    std::vector<DrawTriangle> triangles;
    triangles.reserve(sourceTriangleCount(*model));

    for (const auto& primitive : model->primitives) {
        const auto indices = std::span<const std::uint32_t>(primitive.indices);
        const auto materialIndex = std::min(primitive.materialIndex, model->materials.size() - 1U);
        const auto& material = model->materials[materialIndex];
        const assets::TextureAsset* texture = nullptr;
        if (material.baseColorTexture.has_value() && *material.baseColorTexture < model->textures.size()) {
            texture = &model->textures[*material.baseColorTexture];
        }

        for (std::size_t index = 2; index < indices.size(); index += 3U) {
            DrawTriangle triangle;
            triangle.baseColor = material.baseColor;
            triangle.texture = texture;
            const std::array<std::uint32_t, 3> triangleIndices {indices[index - 2], indices[index - 1], indices[index]};
            bool visible = true;
            for (std::size_t vertexIndex = 0; vertexIndex < triangleIndices.size(); ++vertexIndex) {
                if (triangleIndices[vertexIndex] >= primitive.vertices.size()) {
                    visible = false;
                    break;
                }

                const auto& sourceVertex = primitive.vertices[triangleIndices[vertexIndex]];
                const auto worldPosition = position + rotateEuler(
                    scaled(sourceVertex.position, entity.transform.scale),
                    entity.transform.rotationEuler);
                const auto projected = projectPoint(worldPosition);
                if (projected.depth <= kNearPlane) {
                    visible = false;
                    break;
                }

                triangle.polygon << projected.point;
                triangle.projectedPoints[vertexIndex] = projected.point;
                triangle.texCoords[vertexIndex] = sourceVertex.texCoord;
                triangle.depth += projected.depth;
            }

            if (visible && triangle.polygon.boundingRect().intersects(rect().adjusted(-48, -48, 48, 48))) {
                triangle.depth /= 3.0F;
                triangles.push_back(std::move(triangle));
            }
        }
    }

    std::sort(triangles.begin(), triangles.end(), [](const DrawTriangle& lhs, const DrawTriangle& rhs) {
        return lhs.depth > rhs.depth;
    });

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    std::unordered_map<const assets::TextureAsset*, QImage> textureCache;
    for (const auto& triangle : triangles) {
        bool textured = false;
        if (textureUsable(triangle.texture)) {
            const auto [it, inserted] = textureCache.try_emplace(triangle.texture, textureImage(*triangle.texture));
            Q_UNUSED(inserted);
            const auto& image = it->second;
            QPolygonF sourceQuad;
            sourceQuad
                << texturePoint(*triangle.texture, triangle.texCoords[0])
                << texturePoint(*triangle.texture, triangle.texCoords[1])
                << texturePoint(*triangle.texture, triangle.texCoords[2]);
            sourceQuad << sourceQuad[0] + sourceQuad[2] - sourceQuad[1];

            QPolygonF destinationQuad;
            destinationQuad
                << triangle.projectedPoints[0]
                << triangle.projectedPoints[1]
                << triangle.projectedPoints[2];
            destinationQuad << destinationQuad[0] + destinationQuad[2] - destinationQuad[1];

            QTransform textureTransform;
            if (QTransform::quadToQuad(sourceQuad, destinationQuad, textureTransform)) {
                QPainterPath clipPath;
                clipPath.addPolygon(triangle.polygon);
                painter.save();
                painter.setClipPath(clipPath);
                painter.setTransform(textureTransform, true);
                painter.drawImage(QPointF(0.0, 0.0), image);
                painter.restore();
                textured = true;
            }
        }

        if (!textured) {
            painter.setBrush(baseColorToQColor(triangle.baseColor));
            painter.setPen(Qt::NoPen);
            painter.drawPolygon(triangle.polygon);
        }
    }
    painter.restore();
    return !triangles.empty();
}

void ViewportWidget::drawGizmo(QPainter& painter)
{
    (void)updateGizmoFrame();

    struct GizmoTriangle {
        QPolygonF polygon;
        QColor color;
        float depth {0.0F};
    };

    std::vector<GizmoTriangle> triangles;
    const auto& gizmoMesh = gizmoBackend_->mesh();
    triangles.reserve(gizmoMesh.triangles.size());
    for (const auto& triangle : gizmoMesh.triangles) {
        const auto indices = std::array<std::uint32_t, 3> {triangle.x, triangle.y, triangle.z};
        if (indices[0] >= gizmoMesh.vertices.size()
            || indices[1] >= gizmoMesh.vertices.size()
            || indices[2] >= gizmoMesh.vertices.size()) {
            continue;
        }

        GizmoTriangle drawTriangle;
        drawTriangle.color = colorFromGizmo(gizmoMesh.vertices[indices[0]].color);
        bool allProjected = true;
        for (const auto index : indices) {
            const auto projected = projectPoint(gizmoMesh.vertices[index].position);
            if (projected.depth <= kNearPlane) {
                allProjected = false;
                break;
            }
            drawTriangle.polygon << projected.point;
            drawTriangle.depth += projected.depth;
        }
        if (allProjected) {
            drawTriangle.depth /= 3.0F;
            triangles.push_back(std::move(drawTriangle));
        }
    }

    std::sort(triangles.begin(), triangles.end(), [](const GizmoTriangle& lhs, const GizmoTriangle& rhs) {
        return lhs.depth > rhs.depth;
    });
    painter.save();
    for (const auto& triangle : triangles) {
        auto outline = triangle.color.darker(165);
        outline.setAlpha(210);
        painter.setPen(QPen(outline, 1));
        painter.setBrush(triangle.color);
        painter.drawPolygon(triangle.polygon);
    }
    painter.restore();
}

void ViewportWidget::drawOverlay(QPainter& painter) const
{
    painter.setPen(QColor(220, 225, 235));
    painter.setBrush(withAlpha(QColor(18, 20, 24), 185));
    const auto title = mode_ == ViewportMode::Scene ? QStringLiteral("Scene View") : QStringLiteral("Game View");
    const auto entityCount = scene_ == nullptr ? 0 : static_cast<int>(scene_->entityCount());
    const auto selectedText = selectedEntityId_.isValid()
        ? QStringLiteral("Selected: %1").arg(static_cast<qulonglong>(selectedEntityId_.value()))
        : QStringLiteral("Selected: none");
    const auto spaceText = transformSpace_ == TransformSpace::Local ? QStringLiteral("Local") : QStringLiteral("Global");
    const auto line1 = mode_ == ViewportMode::Scene ? QStringLiteral("%1 | %2 | %3").arg(title, toolModeName(), spaceText) : title;
    const auto line2 = QStringLiteral("Entities: %1 | %2").arg(entityCount).arg(selectedText);
    const QFontMetrics metrics(painter.font());
    const auto widthValue = std::max(metrics.horizontalAdvance(line1), metrics.horizontalAdvance(line2)) + 20;
    const QRectF box(10.0, 10.0, static_cast<qreal>(widthValue), 52.0);
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(box, 4.0, 4.0);
    painter.setPen(QColor(220, 225, 235));
    painter.drawText(QPointF(20.0, 31.0), line1);
    painter.setPen(QColor(170, 180, 195));
    painter.drawText(QPointF(20.0, 53.0), line2);
    if (mode_ == ViewportMode::Game) {
        painter.setPen(QColor(120, 130, 145));
        painter.drawRect(rect().adjusted(18, 18, -18, -18));
    }
}

} // namespace projectunity::editor
