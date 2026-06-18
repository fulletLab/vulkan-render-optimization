#include <projectunity/editor/SceneHierarchyWidget.hpp>

#include <projectunity/editor/ProjectAssetTreeWidget.hpp>
#include <projectunity/core/Log.hpp>

#include <QAbstractItemView>
#include <QBrush>
#include <QColor>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFont>
#include <QIcon>
#include <QImage>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QSignalBlocker>
#include <QTreeWidgetItem>
#include <QVariant>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace projectunity::editor {
namespace {

constexpr int kEntityIdRole = Qt::UserRole + 1;
constexpr int kItemKindRole = Qt::UserRole + 20;
constexpr int kOwnerEntityIdRole = Qt::UserRole + 21;
constexpr int kAssetIdRole = Qt::UserRole + 22;
constexpr int kSubObjectIndexRole = Qt::UserRole + 23;
constexpr int kGroupRole = Qt::UserRole + 24;
constexpr int kRangeFirstRole = Qt::UserRole + 25;
constexpr int kRangeCountRole = Qt::UserRole + 26;
constexpr int kPopulatedRole = Qt::UserRole + 27;
constexpr std::uint32_t kPageSize = 256U;

enum class ItemKind : std::uint8_t { Entity, Group, Page, AssetReference, NodeReference, Placeholder };
enum class ModelGroup : std::uint8_t { Meshes, Materials, Textures, Nodes };
enum class IconKind : std::uint8_t { GameObject, Model, Mesh, Material, Texture, Node, Camera, Light, Player, Page };

[[nodiscard]] QIcon hierarchyIcon(IconKind kind, QColor overrideColor = {})
{
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const std::array<QColor, 10> colors {{
        QColor(164, 174, 188), QColor(76, 184, 151), QColor(88, 190, 152),
        QColor(225, 134, 88), QColor(216, 104, 151), QColor(102, 174, 220),
        QColor(96, 166, 222), QColor(239, 194, 76), QColor(111, 188, 118), QColor(145, 153, 166),
    }};
    const auto color = overrideColor.isValid() ? overrideColor : colors[static_cast<std::size_t>(kind)];
    painter.setPen(QPen(color.lighter(140), 1.2));
    painter.setBrush(color);
    switch (kind) {
    case IconKind::Material:
        painter.drawEllipse(QRectF(3.0, 3.0, 18.0, 18.0));
        painter.setBrush(QColor(255, 255, 255, 90));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QRectF(6.0, 5.0, 6.0, 5.0));
        break;
    case IconKind::Texture:
        painter.drawRoundedRect(QRectF(2.5, 2.5, 19.0, 19.0), 2.0, 2.0);
        painter.setPen(QPen(QColor(255, 255, 255, 155), 1.4));
        painter.drawLine(QPointF(5.0, 17.0), QPointF(10.0, 11.0));
        painter.drawLine(QPointF(10.0, 11.0), QPointF(14.0, 15.0));
        painter.drawLine(QPointF(14.0, 15.0), QPointF(19.0, 8.0));
        break;
    case IconKind::Camera:
        painter.drawRoundedRect(QRectF(3.0, 7.0, 13.0, 11.0), 2.0, 2.0);
        painter.drawPolygon(QPolygonF({QPointF(16.0, 10.0), QPointF(22.0, 7.0), QPointF(22.0, 18.0), QPointF(16.0, 15.0)}));
        break;
    case IconKind::Light:
        painter.drawEllipse(QRectF(6.0, 3.0, 12.0, 12.0));
        painter.drawRoundedRect(QRectF(8.0, 15.0, 8.0, 6.0), 1.0, 1.0);
        break;
    case IconKind::Mesh:
        painter.drawPolygon(QPolygonF({QPointF(12.0, 2.0), QPointF(22.0, 20.0), QPointF(2.0, 20.0)}));
        painter.setPen(QPen(QColor(255, 255, 255, 120), 1.0));
        painter.drawLine(QPointF(12.0, 2.0), QPointF(12.0, 20.0));
        break;
    case IconKind::Node:
        painter.drawRoundedRect(QRectF(3.0, 3.0, 8.0, 8.0), 1.0, 1.0);
        painter.drawRoundedRect(QRectF(13.0, 13.0, 8.0, 8.0), 1.0, 1.0);
        painter.drawLine(QPointF(10.0, 10.0), QPointF(14.0, 14.0));
        break;
    case IconKind::Page:
        painter.drawRoundedRect(QRectF(2.0, 7.0, 20.0, 14.0), 2.0, 2.0);
        painter.drawRoundedRect(QRectF(3.0, 4.0, 9.0, 6.0), 2.0, 2.0);
        break;
    default:
        painter.drawRoundedRect(QRectF(3.0, 3.0, 18.0, 18.0), 3.0, 3.0);
        painter.setPen(QPen(QColor(255, 255, 255, 120), 1.0));
        painter.drawLine(QPointF(7.0, 8.0), QPointF(17.0, 8.0));
        painter.drawLine(QPointF(7.0, 12.0), QPointF(17.0, 12.0));
        painter.drawLine(QPointF(7.0, 16.0), QPointF(14.0, 16.0));
        break;
    }
    return QIcon(pixmap);
}

[[nodiscard]] QIcon textureIcon(const assets::TextureAsset& texture)
{
    if (texture.width == 0U || texture.height == 0U
        || texture.rgba8.size() < static_cast<std::size_t>(texture.width) * texture.height * 4U) {
        return hierarchyIcon(IconKind::Texture);
    }
    const QImage image(
        texture.rgba8.data(), static_cast<int>(texture.width), static_cast<int>(texture.height),
        static_cast<int>(texture.width * 4U), QImage::Format_RGBA8888);
    return QIcon(QPixmap::fromImage(image.copy().scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
}

[[nodiscard]] QIcon materialIcon(const assets::MaterialAsset& material)
{
    return hierarchyIcon(IconKind::Material, QColor(
        std::clamp(static_cast<int>(std::round(material.baseColor[0] * 255.0F)), 0, 255),
        std::clamp(static_cast<int>(std::round(material.baseColor[1] * 255.0F)), 0, 255),
        std::clamp(static_cast<int>(std::round(material.baseColor[2] * 255.0F)), 0, 255),
        255));
}

[[nodiscard]] IconKind entityIconKind(const scene::Entity& entity)
{
    if (entity.camera.has_value() && !entity.scripts.empty()) return IconKind::Player;
    if (entity.camera.has_value()) return IconKind::Camera;
    if (entity.light.has_value()) return IconKind::Light;
    if (entity.meshRenderer.has_value()) return entity.meshRenderer->renderable ? IconKind::Model : IconKind::Node;
    return IconKind::GameObject;
}

[[nodiscard]] std::uint32_t clampedCount(std::size_t count)
{
    return static_cast<std::uint32_t>(std::min<std::size_t>(count, std::numeric_limits<std::uint32_t>::max()));
}

[[nodiscard]] std::uint32_t groupCount(const assets::ModelAsset& model, ModelGroup group)
{
    switch (group) {
    case ModelGroup::Meshes: return clampedCount(model.primitives.size());
    case ModelGroup::Materials: return clampedCount(model.materials.size());
    case ModelGroup::Textures: return clampedCount(model.textures.size());
    case ModelGroup::Nodes: return clampedCount(model.editorInstances.empty() ? model.primitiveInstances.size() : model.editorInstances.size());
    }
    return 0U;
}

[[nodiscard]] QString groupName(ModelGroup group)
{
    switch (group) {
    case ModelGroup::Meshes: return QStringLiteral("Meshes");
    case ModelGroup::Materials: return QStringLiteral("Materials");
    case ModelGroup::Textures: return QStringLiteral("Textures");
    case ModelGroup::Nodes: return QStringLiteral("Nodes / Primitives");
    }
    return QStringLiteral("Group");
}

[[nodiscard]] IconKind groupIcon(ModelGroup group)
{
    switch (group) {
    case ModelGroup::Meshes: return IconKind::Mesh;
    case ModelGroup::Materials: return IconKind::Material;
    case ModelGroup::Textures: return IconKind::Texture;
    case ModelGroup::Nodes: return IconKind::Node;
    }
    return IconKind::Page;
}

void addPlaceholder(QTreeWidgetItem& parent)
{
    auto* item = new QTreeWidgetItem(&parent);
    item->setText(0, QStringLiteral("Loading..."));
    item->setData(0, kItemKindRole, static_cast<int>(ItemKind::Placeholder));
    item->setForeground(0, QBrush(QColor(150, 154, 162)));
}

void clearChildren(QTreeWidgetItem& item)
{
    while (item.childCount() > 0) {
        delete item.takeChild(0);
    }
}

[[nodiscard]] scene::EntityId entityId(const QTreeWidgetItem& item)
{
    return scene::EntityId(item.data(0, kEntityIdRole).toULongLong());
}

[[nodiscard]] bool isEditableProxy(const scene::Entity& entity, assets::AssetId assetId)
{
    return entity.meshRenderer.has_value()
        && !entity.meshRenderer->renderable
        && entity.meshRenderer->modelAssetId == assetId;
}

[[nodiscard]] std::optional<std::uint32_t> proxyNodeIndex(
    const scene::Entity& entity,
    const assets::ModelAsset& model)
{
    if (!entity.meshRenderer.has_value()) return std::nullopt;
    if (!model.editorInstances.empty() && entity.meshRenderer->editorInstanceIndex.has_value()) {
        return entity.meshRenderer->editorInstanceIndex;
    }
    if (entity.meshRenderer->primitiveInstanceIndex.has_value()) {
        return entity.meshRenderer->primitiveInstanceIndex;
    }
    return std::nullopt;
}

} // namespace

SceneHierarchyWidget::SceneHierarchyWidget(QWidget* parent)
    : QTreeWidget(parent)
{
    setHeaderLabel(QStringLiteral("GameObjects"));
    setSelectionMode(QAbstractItemView::SingleSelection);
    setUniformRowHeights(true);
    setIconSize(QSize(24, 24));
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DropOnly);
    connect(this, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* item) {
        populateExpandedItem(item);
    });
}

void SceneHierarchyWidget::setScene(scene::Scene* scene)
{
    scene_ = scene;
}

void SceneHierarchyWidget::setAssetManager(const assets::IAssetManager* assetManager)
{
    assetManager_ = assetManager;
}

void SceneHierarchyWidget::setAssetDropCallback(
    std::function<void(assets::AssetId, std::filesystem::path)> callback)
{
    assetDropCallback_ = std::move(callback);
}

void SceneHierarchyWidget::setHierarchyChangedCallback(std::function<void(scene::EntityId)> callback)
{
    hierarchyChangedCallback_ = std::move(callback);
}

void SceneHierarchyWidget::rebuild(scene::EntityId selectedEntityId)
{
    const QSignalBlocker blocker(this);
    clear();
    if (scene_ == nullptr) return;
    auto virtualNodeCount = std::size_t {0};
    auto materializedProxyCount = std::size_t {0};
    for (const auto& entity : scene_->entities()) {
        if (entity.meshRenderer.has_value() && !entity.meshRenderer->renderable) {
            ++materializedProxyCount;
        }
        if (assetManager_ == nullptr || !entity.meshRenderer.has_value() || !entity.meshRenderer->renderable) continue;
        if (const auto model = assetManager_->model(entity.meshRenderer->modelAssetId)) {
            virtualNodeCount += model->editorInstances.empty()
                ? model->primitiveInstances.size()
                : model->editorInstances.size();
        }
    }
    for (const auto rootId : scene_->rootEntities()) addEntityItem(nullptr, rootId);
    for (int index = 0; index < topLevelItemCount(); ++index) topLevelItem(index)->setExpanded(true);
    if (selectedEntityId.isValid()) (void)selectSceneEntity(selectedEntityId);
    core::logInfo(core::LogCategory::Editor,
        "Hierarchy rebuilt sceneRoots=" + std::to_string(scene_->rootEntities().size())
            + " sceneEntities=" + std::to_string(scene_->entityCount())
            + " virtualNodes=" + std::to_string(virtualNodeCount)
            + " materializedProxies=" + std::to_string(materializedProxyCount));
}

void SceneHierarchyWidget::addEntityItem(QTreeWidgetItem* parentItem, scene::EntityId entityIdValue)
{
    if (scene_ == nullptr) return;
    const auto* entity = scene_->findEntity(entityIdValue);
    if (entity == nullptr) return;
    auto* item = parentItem == nullptr ? new QTreeWidgetItem(this) : new QTreeWidgetItem(parentItem);
    item->setText(0, QString::fromStdString(entity->name));
    item->setIcon(0, hierarchyIcon(entityIconKind(*entity)));
    item->setData(0, kItemKindRole, static_cast<int>(ItemKind::Entity));
    item->setData(0, kEntityIdRole, static_cast<qulonglong>(entity->id.value()));

    std::shared_ptr<const assets::ModelAsset> model;
    if (assetManager_ != nullptr && entity->meshRenderer.has_value() && entity->meshRenderer->renderable) {
        model = assetManager_->model(entity->meshRenderer->modelAssetId);
    }
    if (model != nullptr) addModelGroups(*item, *entity, *model);
    for (const auto childId : entity->children) {
        const auto* child = scene_->findEntity(childId);
        if (child == nullptr || (model != nullptr && isEditableProxy(*child, model->id))) continue;
        addEntityItem(item, childId);
    }
}

void SceneHierarchyWidget::addModelGroups(
    QTreeWidgetItem& entityItem,
    const scene::Entity& entity,
    const assets::ModelAsset& model)
{
    const std::array<ModelGroup, 4> groups {
        ModelGroup::Meshes, ModelGroup::Materials, ModelGroup::Textures, ModelGroup::Nodes,
    };
    for (const auto group : groups) {
        const auto count = groupCount(model, group);
        auto* item = new QTreeWidgetItem(&entityItem);
        item->setText(0, QStringLiteral("%1 (%2)").arg(groupName(group)).arg(count));
        item->setIcon(0, hierarchyIcon(groupIcon(group)));
        item->setData(0, kItemKindRole, static_cast<int>(ItemKind::Group));
        item->setData(0, kOwnerEntityIdRole, static_cast<qulonglong>(entity.id.value()));
        item->setData(0, kAssetIdRole, static_cast<qulonglong>(model.id.value()));
        item->setData(0, kGroupRole, static_cast<int>(group));
        item->setData(0, kRangeFirstRole, 0U);
        item->setData(0, kRangeCountRole, count);
        item->setData(0, kPopulatedRole, false);
        if (count > 0U) addPlaceholder(*item);
    }
}

void SceneHierarchyWidget::populateExpandedItem(QTreeWidgetItem* item)
{
    if (item == nullptr || item->data(0, kPopulatedRole).toBool()) return;
    const auto kind = static_cast<ItemKind>(item->data(0, kItemKindRole).toInt());
    if (kind != ItemKind::Group && kind != ItemKind::Page) return;
    const auto first = item->data(0, kRangeFirstRole).toUInt();
    const auto count = item->data(0, kRangeCountRole).toUInt();
    clearChildren(*item);
    if (kind == ItemKind::Group && count > kPageSize) {
        for (std::uint32_t offset = 0U; offset < count; offset += kPageSize) {
            const auto pageCount = std::min(kPageSize, count - offset);
            auto* page = new QTreeWidgetItem(item);
            page->setText(0, QStringLiteral("%1 %2-%3")
                .arg(groupName(static_cast<ModelGroup>(item->data(0, kGroupRole).toInt())))
                .arg(offset + 1U).arg(offset + pageCount));
            page->setIcon(0, hierarchyIcon(IconKind::Page));
            page->setData(0, kItemKindRole, static_cast<int>(ItemKind::Page));
            for (const auto role : {kOwnerEntityIdRole, kAssetIdRole, kGroupRole}) {
                page->setData(0, role, item->data(0, role));
            }
            page->setData(0, kRangeFirstRole, offset);
            page->setData(0, kRangeCountRole, pageCount);
            page->setData(0, kPopulatedRole, false);
            addPlaceholder(*page);
        }
    } else {
        populateRange(*item, first, count);
    }
    item->setData(0, kPopulatedRole, true);
}

void SceneHierarchyWidget::populateRange(QTreeWidgetItem& parent, std::uint32_t first, std::uint32_t count)
{
    if (scene_ == nullptr || assetManager_ == nullptr) return;
    const auto ownerId = scene::EntityId(parent.data(0, kOwnerEntityIdRole).toULongLong());
    const auto assetId = assets::AssetId(parent.data(0, kAssetIdRole).toULongLong());
    const auto model = assetManager_->model(assetId);
    const auto* owner = scene_->findEntity(ownerId);
    if (model == nullptr || owner == nullptr) return;
    const auto group = static_cast<ModelGroup>(parent.data(0, kGroupRole).toInt());
    for (std::uint32_t index = first; index < first + count; ++index) {
        auto* item = new QTreeWidgetItem(&parent);
        item->setData(0, kOwnerEntityIdRole, static_cast<qulonglong>(ownerId.value()));
        item->setData(0, kAssetIdRole, static_cast<qulonglong>(assetId.value()));
        item->setData(0, kSubObjectIndexRole, index);
        item->setData(0, kGroupRole, static_cast<int>(group));
        item->setData(0, kItemKindRole, static_cast<int>(group == ModelGroup::Nodes ? ItemKind::NodeReference : ItemKind::AssetReference));
        switch (group) {
        case ModelGroup::Meshes:
            item->setText(0, QStringLiteral("Mesh %1").arg(index + 1U));
            item->setIcon(0, hierarchyIcon(IconKind::Mesh));
            break;
        case ModelGroup::Materials:
            item->setText(0, QString::fromStdString(model->materials[index].name));
            item->setIcon(0, materialIcon(model->materials[index]));
            break;
        case ModelGroup::Textures:
            item->setText(0, QString::fromStdString(model->textures[index].name));
            item->setIcon(0, textureIcon(model->textures[index]));
            break;
        case ModelGroup::Nodes: {
            const auto name = model->editorInstances.empty()
                ? QStringLiteral("Primitive %1").arg(index + 1U)
                : QString::fromStdString(model->editorInstances[index].name);
            item->setText(0, name);
            item->setIcon(0, hierarchyIcon(IconKind::Node));
            for (const auto childId : owner->children) {
                const auto* child = scene_->findEntity(childId);
                if (child == nullptr || !isEditableProxy(*child, assetId)) continue;
                const auto childIndex = proxyNodeIndex(*child, *model);
                if (childIndex.has_value() && *childIndex == index) {
                    item->setText(0, QString::fromStdString(child->name));
                    item->setData(0, kEntityIdRole, static_cast<qulonglong>(child->id.value()));
                    break;
                }
            }
            break;
        }
        }
    }
}

scene::EntityId SceneHierarchyWidget::materializeNode(QTreeWidgetItem& item)
{
    if (scene_ == nullptr || assetManager_ == nullptr) return {};
    const auto existing = entityId(item);
    if (existing.isValid() && scene_->contains(existing)) return existing;
    const auto ownerId = scene::EntityId(item.data(0, kOwnerEntityIdRole).toULongLong());
    const auto assetId = assets::AssetId(item.data(0, kAssetIdRole).toULongLong());
    const auto index = item.data(0, kSubObjectIndexRole).toUInt();
    const auto model = assetManager_->model(assetId);
    const auto* owner = scene_->findEntity(ownerId);
    if (model == nullptr || owner == nullptr) return {};
    for (const auto childId : owner->children) {
        const auto* child = scene_->findEntity(childId);
        if (child == nullptr || !isEditableProxy(*child, assetId)) continue;
        const auto childIndex = proxyNodeIndex(*child, *model);
        if (childIndex.has_value() && *childIndex == index) {
            item.setData(0, kEntityIdRole, static_cast<qulonglong>(childId.value()));
            return childId;
        }
    }
    const auto usesEditorInstances = !model->editorInstances.empty();
    if ((usesEditorInstances && index >= model->editorInstances.size())
        || (!usesEditorInstances && index >= model->primitiveInstances.size())) return {};
    const auto name = usesEditorInstances
        ? model->editorInstances[index].name
        : std::string("Primitive ") + std::to_string(index + 1U);
    auto& proxy = scene_->createEntity(name, ownerId);
    const auto proxyId = proxy.id;
    scene::TransformComponent transform;
    transform.position = usesEditorInstances
        ? model->editorInstances[index].bounds.center
        : model->primitiveInstances[index].bounds.center;
    scene::MeshRendererComponent renderer;
    renderer.modelAssetId = assetId;
    renderer.renderable = false;
    if (usesEditorInstances) renderer.editorInstanceIndex = index;
    else renderer.primitiveInstanceIndex = index;
    (void)scene_->setTransform(proxyId, transform);
    (void)scene_->setMeshRenderer(proxyId, renderer);
    item.setData(0, kEntityIdRole, static_cast<qulonglong>(proxyId.value()));
    core::logInfo(core::LogCategory::Editor,
        "Hierarchy materialized editable node objectId=" + std::to_string(proxyId.value())
            + " ownerObjectId=" + std::to_string(ownerId.value())
            + " assetId=" + std::to_string(assetId.value())
            + " nodeIndex=" + std::to_string(index));
    if (hierarchyChangedCallback_) {
        hierarchyChangedCallback_(proxyId);
    }
    return proxyId;
}

scene::EntityId SceneHierarchyWidget::selectedSceneEntity()
{
    auto* item = currentItem();
    if (item == nullptr) return {};
    const auto kind = static_cast<ItemKind>(item->data(0, kItemKindRole).toInt());
    if (kind == ItemKind::NodeReference) return materializeNode(*item);
    return kind == ItemKind::Entity ? entityId(*item) : scene::EntityId {};
}

QTreeWidgetItem* SceneHierarchyWidget::findEntityItem(scene::EntityId searchId) const
{
    const auto matches = findItems(QStringLiteral("*"), Qt::MatchWildcard | Qt::MatchRecursive);
    const auto it = std::find_if(matches.begin(), matches.end(), [searchId](const QTreeWidgetItem* item) {
        return item != nullptr && entityId(*item) == searchId;
    });
    return it == matches.end() ? nullptr : *it;
}

bool SceneHierarchyWidget::selectSceneEntity(scene::EntityId searchId)
{
    if (!searchId.isValid() || scene_ == nullptr) return false;
    if (auto* existing = findEntityItem(searchId)) {
        setCurrentItem(existing);
        scrollToItem(existing);
        return true;
    }
    const auto* entity = scene_->findEntity(searchId);
    if (entity == nullptr || !entity->parent.has_value() || !entity->meshRenderer.has_value()
        || entity->meshRenderer->renderable) return false;
    auto* ownerItem = findEntityItem(*entity->parent);
    const auto model = assetManager_ == nullptr ? nullptr : assetManager_->model(entity->meshRenderer->modelAssetId);
    const auto index = model == nullptr ? std::optional<std::uint32_t> {} : proxyNodeIndex(*entity, *model);
    if (ownerItem == nullptr || !index.has_value()) return false;
    QTreeWidgetItem* nodes = nullptr;
    for (int child = 0; child < ownerItem->childCount(); ++child) {
        auto* candidate = ownerItem->child(child);
        if (candidate->data(0, kItemKindRole).toInt() == static_cast<int>(ItemKind::Group)
            && candidate->data(0, kGroupRole).toInt() == static_cast<int>(ModelGroup::Nodes)) {
            nodes = candidate;
            break;
        }
    }
    if (nodes == nullptr) return false;
    nodes->setExpanded(true);
    populateExpandedItem(nodes);
    QTreeWidgetItem* container = nodes;
    if (nodes->data(0, kRangeCountRole).toUInt() > kPageSize) {
        const auto pageIndex = *index / kPageSize;
        if (pageIndex >= static_cast<std::uint32_t>(nodes->childCount())) return false;
        container = nodes->child(static_cast<int>(pageIndex));
        container->setExpanded(true);
        populateExpandedItem(container);
    }
    for (int child = 0; child < container->childCount(); ++child) {
        auto* candidate = container->child(child);
        if (candidate->data(0, kSubObjectIndexRole).toUInt() == *index) {
            candidate->setData(0, kEntityIdRole, static_cast<qulonglong>(searchId.value()));
            setCurrentItem(candidate);
            scrollToItem(candidate);
            return true;
        }
    }
    return false;
}

void SceneHierarchyWidget::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasFormat(kProjectUnityAssetIdMime)
        || event->mimeData()->hasFormat(kProjectUnityAssetPathMime)) event->acceptProposedAction();
    else QTreeWidget::dragEnterEvent(event);
}

void SceneHierarchyWidget::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->mimeData()->hasFormat(kProjectUnityAssetIdMime)
        || event->mimeData()->hasFormat(kProjectUnityAssetPathMime)) event->acceptProposedAction();
    else QTreeWidget::dragMoveEvent(event);
}

void SceneHierarchyWidget::dropEvent(QDropEvent* event)
{
    if (!assetDropCallback_) {
        QTreeWidget::dropEvent(event);
        return;
    }
    const auto* mime = event->mimeData();
    const auto assetId = assets::AssetId(mime->data(kProjectUnityAssetIdMime).toULongLong());
    const auto pathBytes = mime->data(kProjectUnityAssetPathMime);
    const auto path = pathBytes.isEmpty()
        ? std::filesystem::path {}
        : std::filesystem::path(QString::fromUtf8(pathBytes).toStdWString());
    if (assetId.isValid() || !path.empty()) {
        core::logInfo(core::LogCategory::Assets,
            "Hierarchy asset drop assetId=" + std::to_string(assetId.value())
                + " path=" + path.string());
        assetDropCallback_(assetId, path);
        event->acceptProposedAction();
        return;
    }
    QTreeWidget::dropEvent(event);
}

} // namespace projectunity::editor
