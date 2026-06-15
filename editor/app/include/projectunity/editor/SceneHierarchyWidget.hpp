#pragma once

#include <projectunity/assets/AssetManager.hpp>
#include <projectunity/scene/Scene.hpp>

#include <filesystem>
#include <functional>

#include <QTreeWidget>

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;

namespace projectunity::editor {

class SceneHierarchyWidget final : public QTreeWidget {
public:
    explicit SceneHierarchyWidget(QWidget* parent = nullptr);

    void setScene(scene::Scene* scene);
    void setAssetManager(const assets::IAssetManager* assetManager);
    void setAssetDropCallback(std::function<void(assets::AssetId, std::filesystem::path)> callback);

    void rebuild(scene::EntityId selectedEntityId = {});
    [[nodiscard]] scene::EntityId selectedSceneEntity();
    [[nodiscard]] bool selectSceneEntity(scene::EntityId entityId);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void addEntityItem(QTreeWidgetItem* parentItem, scene::EntityId entityId);
    void addModelGroups(QTreeWidgetItem& entityItem, const scene::Entity& entity, const assets::ModelAsset& model);
    void populateExpandedItem(QTreeWidgetItem* item);
    void populateRange(QTreeWidgetItem& parent, std::uint32_t first, std::uint32_t count);
    [[nodiscard]] scene::EntityId materializeNode(QTreeWidgetItem& item);
    [[nodiscard]] QTreeWidgetItem* findEntityItem(scene::EntityId entityId) const;

    scene::Scene* scene_ {nullptr};
    const assets::IAssetManager* assetManager_ {nullptr};
    std::function<void(assets::AssetId, std::filesystem::path)> assetDropCallback_;
};

} // namespace projectunity::editor
