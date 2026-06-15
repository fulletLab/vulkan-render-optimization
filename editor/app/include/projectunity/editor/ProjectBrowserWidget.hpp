#pragma once

#include <projectunity/assets/AssetManager.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>

#include <QWidget>

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace projectunity::editor {

enum class ProjectBrowserItemKind : std::uint8_t {
    Folder,
    Scene,
    Model,
    Mesh,
    Material,
    Texture,
    Script,
    Terrain,
    Prefab,
    File,
    SubAssetGroup,
    SubAssetPage,
    ModelNode,
};

struct ProjectBrowserSelection {
    assets::AssetId assetId;
    std::uint64_t subAssetId {0};
    ProjectBrowserItemKind kind {ProjectBrowserItemKind::File};
    std::optional<std::uint32_t> subAssetIndex;
};

class ProjectBrowserWidget final : public QWidget {
public:
    explicit ProjectBrowserWidget(QWidget* parent = nullptr);

    void setAssetManager(const assets::IAssetManager* assetManager);
    void setProjectRoot(std::filesystem::path projectRoot);
    void setSelectionChangedCallback(std::function<void(ProjectBrowserSelection)> callback);
    void setAssetActivatedCallback(std::function<void(ProjectBrowserSelection)> callback);

    void rebuild();
    [[nodiscard]] std::optional<ProjectBrowserSelection> selection() const;
    [[nodiscard]] bool selectAsset(assets::AssetId assetId);
    [[nodiscard]] std::size_t rootAssetCount() const noexcept;
    [[nodiscard]] std::size_t visibleItemCount() const;
    [[nodiscard]] std::size_t hiddenSubAssetCount() const noexcept;
    [[nodiscard]] QTreeWidget* treeWidget() const noexcept;

private:
    void rebuildFilter();
    void populateExpandedItem(QTreeWidgetItem* item);
    void populateModelGroup(QTreeWidgetItem* item, std::uint32_t first, std::uint32_t count);
    void updateSelection();

    const assets::IAssetManager* assetManager_ {nullptr};
    std::filesystem::path projectRoot_;
    QLineEdit* filterEdit_ {nullptr};
    QTreeWidget* tree_ {nullptr};
    std::function<void(ProjectBrowserSelection)> selectionChangedCallback_;
    std::function<void(ProjectBrowserSelection)> assetActivatedCallback_;
    std::size_t rootAssetCount_ {0};
    std::size_t hiddenSubAssetCount_ {0};
};

} // namespace projectunity::editor
