#include <projectunity/editor/ProjectBrowserWidget.hpp>

#include <projectunity/assets/AssetSubAssetId.hpp>
#include <projectunity/core/Log.hpp>
#include <projectunity/editor/ProjectAssetTreeWidget.hpp>

#include <QBrush>
#include <QColor>
#include <QHeaderView>
#include <QIcon>
#include <QImage>
#include <QImageReader>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace projectunity::editor {
namespace {

constexpr int kKindRole = Qt::UserRole + 1;
constexpr int kAssetIdRole = Qt::UserRole + 2;
constexpr int kSubAssetIdRole = Qt::UserRole + 3;
constexpr int kSubAssetIndexRole = Qt::UserRole + 4;
constexpr int kGroupRole = Qt::UserRole + 5;
constexpr int kRangeFirstRole = Qt::UserRole + 6;
constexpr int kRangeCountRole = Qt::UserRole + 7;
constexpr int kPopulatedRole = Qt::UserRole + 8;
constexpr int kPathRole = Qt::UserRole + 9;
constexpr std::uint32_t kSubAssetPageSize = 256;

enum class ModelGroup : std::uint8_t { Meshes, Materials, Textures, Nodes };

[[nodiscard]] QString pathToQString(const std::filesystem::path& path)
{
    return QString::fromStdWString(path.wstring());
}

[[nodiscard]] QString lowerSuffix(const std::filesystem::path& path)
{
    return pathToQString(path.extension()).toLower();
}

[[nodiscard]] ProjectBrowserItemKind fileKind(const std::filesystem::path& path)
{
    const auto suffix = lowerSuffix(path);
    if (suffix == QStringLiteral(".scene") || suffix == QStringLiteral(".json")) {
        return ProjectBrowserItemKind::Scene;
    }
    if (suffix == QStringLiteral(".glb")
        || suffix == QStringLiteral(".gltf")
        || suffix == QStringLiteral(".obj")
        || suffix == QStringLiteral(".fbx")
        || suffix == QStringLiteral(".dae")
        || suffix == QStringLiteral(".blend")
        || suffix == QStringLiteral(".ffult")) {
        return ProjectBrowserItemKind::Model;
    }
    if (suffix == QStringLiteral(".png")
        || suffix == QStringLiteral(".jpg")
        || suffix == QStringLiteral(".jpeg")
        || suffix == QStringLiteral(".hdr")
        || suffix == QStringLiteral(".ktx")
        || suffix == QStringLiteral(".ktx2")) {
        return ProjectBrowserItemKind::Texture;
    }
    if (suffix == QStringLiteral(".cpp")
        || suffix == QStringLiteral(".hpp")
        || suffix == QStringLiteral(".h")
        || suffix == QStringLiteral(".cs")) {
        return ProjectBrowserItemKind::Script;
    }
    if (suffix == QStringLiteral(".mat") || suffix == QStringLiteral(".material")) {
        return ProjectBrowserItemKind::Material;
    }
    if (suffix == QStringLiteral(".terrain")) {
        return ProjectBrowserItemKind::Terrain;
    }
    if (suffix == QStringLiteral(".prefab")) {
        return ProjectBrowserItemKind::Prefab;
    }
    return ProjectBrowserItemKind::File;
}

[[nodiscard]] QColor kindColor(ProjectBrowserItemKind kind)
{
    switch (kind) {
    case ProjectBrowserItemKind::Folder:
        return QColor(217, 165, 76);
    case ProjectBrowserItemKind::Scene:
        return QColor(98, 177, 235);
    case ProjectBrowserItemKind::Model:
    case ProjectBrowserItemKind::Mesh:
    case ProjectBrowserItemKind::ModelNode:
        return QColor(88, 190, 152);
    case ProjectBrowserItemKind::Material:
        return QColor(229, 134, 88);
    case ProjectBrowserItemKind::Texture:
        return QColor(219, 105, 152);
    case ProjectBrowserItemKind::Script:
        return QColor(110, 179, 226);
    case ProjectBrowserItemKind::Terrain:
        return QColor(121, 174, 87);
    case ProjectBrowserItemKind::Prefab:
        return QColor(89, 161, 230);
    case ProjectBrowserItemKind::SubAssetGroup:
    case ProjectBrowserItemKind::SubAssetPage:
        return QColor(147, 154, 166);
    case ProjectBrowserItemKind::File:
        return QColor(172, 177, 185);
    }
    return QColor(172, 177, 185);
}

[[nodiscard]] QIcon coloredIcon(ProjectBrowserItemKind kind, QColor overrideColor = {})
{
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto color = overrideColor.isValid() ? overrideColor : kindColor(kind);
    painter.setPen(QPen(color.lighter(135), 1.0));
    painter.setBrush(color);
    if (kind == ProjectBrowserItemKind::Folder) {
        painter.drawRoundedRect(QRectF(2.0, 7.0, 20.0, 14.0), 2.0, 2.0);
        painter.drawRoundedRect(QRectF(3.0, 4.0, 9.0, 6.0), 2.0, 2.0);
    } else if (kind == ProjectBrowserItemKind::Material) {
        painter.drawEllipse(QRectF(3.0, 3.0, 18.0, 18.0));
        painter.setBrush(QColor(255, 255, 255, 90));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QRectF(6.0, 5.0, 6.0, 5.0));
    } else if (kind == ProjectBrowserItemKind::Texture) {
        painter.drawRoundedRect(QRectF(2.5, 2.5, 19.0, 19.0), 2.0, 2.0);
        painter.setPen(QPen(QColor(255, 255, 255, 150), 1.5));
        painter.drawLine(QPointF(5.0, 17.0), QPointF(10.0, 11.0));
        painter.drawLine(QPointF(10.0, 11.0), QPointF(14.0, 15.0));
        painter.drawLine(QPointF(14.0, 15.0), QPointF(19.0, 8.0));
    } else {
        painter.drawRoundedRect(QRectF(3.0, 2.0, 18.0, 20.0), 2.0, 2.0);
        painter.setPen(QPen(QColor(255, 255, 255, 125), 1.2));
        painter.drawLine(QPointF(7.0, 8.0), QPointF(17.0, 8.0));
        painter.drawLine(QPointF(7.0, 12.0), QPointF(17.0, 12.0));
        painter.drawLine(QPointF(7.0, 16.0), QPointF(14.0, 16.0));
    }
    return QIcon(pixmap);
}

[[nodiscard]] QIcon textureIcon(const assets::TextureAsset& texture)
{
    if (texture.width == 0U || texture.height == 0U) {
        return coloredIcon(ProjectBrowserItemKind::Texture);
    }
    if (texture.rgba8.size() >= static_cast<std::size_t>(texture.width) * texture.height * 4U) {
        const QImage image(
            texture.rgba8.data(),
            static_cast<int>(texture.width),
            static_cast<int>(texture.height),
            static_cast<int>(texture.width * 4U),
            QImage::Format_RGBA8888);
        return QIcon(QPixmap::fromImage(image.copy().scaled(
            48,
            48,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation)));
    }
    if (texture.rgba32f.size() >= static_cast<std::size_t>(texture.width) * texture.height * 4U) {
        QImage image(static_cast<int>(texture.width), static_cast<int>(texture.height), QImage::Format_RGBA8888);
        for (std::uint32_t y = 0; y < texture.height; ++y) {
            auto* scanline = image.scanLine(static_cast<int>(y));
            for (std::uint32_t x = 0; x < texture.width; ++x) {
                const auto source = (static_cast<std::size_t>(y) * texture.width + x) * 4U;
                const auto destination = static_cast<std::size_t>(x) * 4U;
                for (std::size_t channel = 0; channel < 4U; ++channel) {
                    const auto value = std::clamp(texture.rgba32f[source + channel], 0.0F, 1.0F);
                    scanline[destination + channel] = static_cast<std::uint8_t>(std::round(value * 255.0F));
                }
            }
        }
        return QIcon(QPixmap::fromImage(image.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    }
    return coloredIcon(ProjectBrowserItemKind::Texture);
}

[[nodiscard]] QIcon fileTextureIcon(const std::filesystem::path& path)
{
    QImageReader reader(pathToQString(path));
    reader.setAutoTransform(true);
    reader.setScaledSize(QSize(48, 48));
    const auto image = reader.read();
    return image.isNull()
        ? coloredIcon(ProjectBrowserItemKind::Texture)
        : QIcon(QPixmap::fromImage(image));
}

[[nodiscard]] QIcon materialIcon(const assets::MaterialAsset& material)
{
    const QColor color(
        std::clamp(static_cast<int>(std::round(material.baseColor[0] * 255.0F)), 0, 255),
        std::clamp(static_cast<int>(std::round(material.baseColor[1] * 255.0F)), 0, 255),
        std::clamp(static_cast<int>(std::round(material.baseColor[2] * 255.0F)), 0, 255),
        std::clamp(static_cast<int>(std::round(material.baseColor[3] * 255.0F)), 0, 255));
    return coloredIcon(ProjectBrowserItemKind::Material, color);
}

[[nodiscard]] assets::SubAssetKind subAssetKind(ModelGroup group) noexcept
{
    switch (group) {
    case ModelGroup::Meshes:
        return assets::SubAssetKind::Mesh;
    case ModelGroup::Materials:
        return assets::SubAssetKind::Material;
    case ModelGroup::Textures:
        return assets::SubAssetKind::Texture;
    case ModelGroup::Nodes:
        return assets::SubAssetKind::Node;
    }
    return assets::SubAssetKind::Node;
}

void setItemIdentity(
    QTreeWidgetItem& item,
    ProjectBrowserItemKind kind,
    assets::AssetId assetId = {},
    std::optional<std::uint32_t> subAssetIndex = std::nullopt,
    std::uint64_t subAssetId = 0U)
{
    item.setData(0, kKindRole, static_cast<int>(kind));
    item.setData(0, kAssetIdRole, static_cast<qulonglong>(assetId.value()));
    item.setData(0, kSubAssetIdRole, static_cast<qulonglong>(subAssetId));
    item.setData(
        0,
        kSubAssetIndexRole,
        subAssetIndex.has_value() ? QVariant::fromValue(*subAssetIndex) : QVariant {});
}

[[nodiscard]] ProjectBrowserItemKind itemKind(const QTreeWidgetItem& item)
{
    return static_cast<ProjectBrowserItemKind>(item.data(0, kKindRole).toInt());
}

[[nodiscard]] assets::AssetId itemAssetId(const QTreeWidgetItem& item)
{
    return assets::AssetId(item.data(0, kAssetIdRole).toULongLong());
}

[[nodiscard]] std::optional<ProjectBrowserSelection> itemSelection(const QTreeWidgetItem* item)
{
    if (item == nullptr) {
        return std::nullopt;
    }
    const auto assetId = itemAssetId(*item);
    if (!assetId.isValid()) {
        return std::nullopt;
    }
    ProjectBrowserSelection selection;
    selection.assetId = assetId;
    selection.subAssetId = item->data(0, kSubAssetIdRole).toULongLong();
    selection.kind = itemKind(*item);
    if (item->data(0, kSubAssetIndexRole).isValid()) {
        selection.subAssetIndex = item->data(0, kSubAssetIndexRole).toUInt();
    }
    return selection;
}

[[nodiscard]] bool textMatches(const QTreeWidgetItem& item, const QString& filter)
{
    if (filter.isEmpty()) {
        return true;
    }
    return item.text(0).contains(filter, Qt::CaseInsensitive)
        || item.text(1).contains(filter, Qt::CaseInsensitive);
}

[[nodiscard]] bool applyFilter(QTreeWidgetItem& item, const QString& filter)
{
    auto descendantMatches = false;
    for (int index = 0; index < item.childCount(); ++index) {
        descendantMatches = applyFilter(*item.child(index), filter) || descendantMatches;
    }
    const auto matches = textMatches(item, filter) || descendantMatches;
    item.setHidden(!matches);
    if (!filter.isEmpty() && descendantMatches) {
        item.setExpanded(true);
    }
    return matches;
}

[[nodiscard]] std::size_t countVisibleItems(const QTreeWidgetItem& item, bool ancestorsExpanded)
{
    if (item.isHidden() || !ancestorsExpanded) {
        return 0U;
    }
    auto count = std::size_t {1};
    const auto childrenVisible = item.isExpanded();
    for (int index = 0; index < item.childCount(); ++index) {
        count += countVisibleItems(*item.child(index), childrenVisible);
    }
    return count;
}

[[nodiscard]] std::string filenameKey(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

[[nodiscard]] std::uint32_t modelGroupCount(const assets::ModelAsset& model, ModelGroup group)
{
    switch (group) {
    case ModelGroup::Meshes:
        return static_cast<std::uint32_t>(std::min<std::size_t>(
            model.primitives.size(),
            std::numeric_limits<std::uint32_t>::max()));
    case ModelGroup::Materials:
        return static_cast<std::uint32_t>(std::min<std::size_t>(
            model.materials.size(),
            std::numeric_limits<std::uint32_t>::max()));
    case ModelGroup::Textures:
        return static_cast<std::uint32_t>(std::min<std::size_t>(
            model.textures.size(),
            std::numeric_limits<std::uint32_t>::max()));
    case ModelGroup::Nodes:
        return static_cast<std::uint32_t>(std::min<std::size_t>(
            model.editorInstances.empty() ? model.primitiveInstances.size() : model.editorInstances.size(),
            std::numeric_limits<std::uint32_t>::max()));
    }
    return 0U;
}

[[nodiscard]] QString modelGroupName(ModelGroup group)
{
    switch (group) {
    case ModelGroup::Meshes:
        return QStringLiteral("Meshes");
    case ModelGroup::Materials:
        return QStringLiteral("Materials");
    case ModelGroup::Textures:
        return QStringLiteral("Textures");
    case ModelGroup::Nodes:
        return QStringLiteral("Nodes / Primitives");
    }
    return QStringLiteral("Sub-assets");
}

[[nodiscard]] QString modelGroupItemType(ModelGroup group)
{
    switch (group) {
    case ModelGroup::Meshes:
        return QStringLiteral("Mesh");
    case ModelGroup::Materials:
        return QStringLiteral("Material");
    case ModelGroup::Textures:
        return QStringLiteral("Texture");
    case ModelGroup::Nodes:
        return QStringLiteral("Node");
    }
    return QStringLiteral("Sub-asset");
}

void addLazyPlaceholder(QTreeWidgetItem& item)
{
    auto* placeholder = new QTreeWidgetItem(&item);
    placeholder->setText(0, QStringLiteral("Loading..."));
    placeholder->setForeground(0, QBrush(QColor(150, 154, 162)));
}

} // namespace

ProjectBrowserWidget::ProjectBrowserWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    filterEdit_ = new QLineEdit(this);
    filterEdit_->setPlaceholderText(QStringLiteral("Search Assets"));
    filterEdit_->setClearButtonEnabled(true);
    layout->addWidget(filterEdit_);

    tree_ = new ProjectAssetTreeWidget(this);
    tree_->setColumnCount(2);
    tree_->setHeaderLabels({QStringLiteral("Name"), QStringLiteral("Type")});
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    tree_->setUniformRowHeights(true);
    tree_->setIconSize(QSize(24, 24));
    layout->addWidget(tree_);

    connect(filterEdit_, &QLineEdit::textChanged, this, [this] {
        rebuildFilter();
    });
    connect(tree_, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* item) {
        populateExpandedItem(item);
        rebuildFilter();
    });
    connect(tree_, &QTreeWidget::itemSelectionChanged, this, [this] {
        updateSelection();
    });
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        const auto selected = itemSelection(item);
        if (selected.has_value() && assetActivatedCallback_) {
            assetActivatedCallback_(*selected);
        }
    });
}

void ProjectBrowserWidget::setAssetManager(const assets::IAssetManager* assetManager)
{
    assetManager_ = assetManager;
}

void ProjectBrowserWidget::setProjectRoot(std::filesystem::path projectRoot)
{
    projectRoot_ = std::move(projectRoot);
}

void ProjectBrowserWidget::setSelectionChangedCallback(
    std::function<void(ProjectBrowserSelection)> callback)
{
    selectionChangedCallback_ = std::move(callback);
}

void ProjectBrowserWidget::setAssetActivatedCallback(
    std::function<void(ProjectBrowserSelection)> callback)
{
    assetActivatedCallback_ = std::move(callback);
}

void ProjectBrowserWidget::rebuild()
{
    if (tree_ == nullptr) {
        return;
    }
    const auto previousSelection = selection();
    tree_->clear();
    rootAssetCount_ = 0U;
    hiddenSubAssetCount_ = 0U;

    auto* assetsRoot = new QTreeWidgetItem(tree_);
    assetsRoot->setText(0, QStringLiteral("Assets"));
    assetsRoot->setText(1, QStringLiteral("Folder"));
    assetsRoot->setIcon(0, coloredIcon(ProjectBrowserItemKind::Folder));
    setItemIdentity(*assetsRoot, ProjectBrowserItemKind::Folder);

    const auto records = assetManager_ == nullptr ? std::vector<assets::AssetRecord> {} : assetManager_->records();
    std::unordered_map<std::string, const assets::AssetRecord*> recordsBySourceName;
    recordsBySourceName.reserve(records.size());
    for (const auto& record : records) {
        recordsBySourceName[filenameKey(record.sourceName)] = &record;
    }
    std::vector<assets::AssetId> representedAssets;
    representedAssets.reserve(records.size());

    const auto addModelGroups = [this](QTreeWidgetItem& assetItem, const assets::AssetRecord& record) {
        const auto model = assetManager_ == nullptr ? nullptr : assetManager_->model(record.id);
        if (model == nullptr) {
            return;
        }
        const std::array<ModelGroup, 4> groups {
            ModelGroup::Meshes,
            ModelGroup::Materials,
            ModelGroup::Textures,
            ModelGroup::Nodes,
        };
        for (const auto group : groups) {
            const auto count = modelGroupCount(*model, group);
            hiddenSubAssetCount_ += count;
            if (count == 0U) {
                continue;
            }
            auto* groupItem = new QTreeWidgetItem(&assetItem);
            groupItem->setText(0, QStringLiteral("%1 (%2)").arg(modelGroupName(group)).arg(count));
            groupItem->setText(1, QStringLiteral("Sub-assets"));
            groupItem->setIcon(0, coloredIcon(ProjectBrowserItemKind::SubAssetGroup));
            setItemIdentity(*groupItem, ProjectBrowserItemKind::SubAssetGroup, record.id);
            groupItem->setData(0, kGroupRole, static_cast<int>(group));
            groupItem->setData(0, kRangeFirstRole, 0U);
            groupItem->setData(0, kRangeCountRole, count);
            groupItem->setData(0, kPopulatedRole, false);
            addLazyPlaceholder(*groupItem);
        }
    };

    const auto addFile = [&](auto&& self, QTreeWidgetItem& parent, const std::filesystem::path& path) -> void {
        std::error_code error;
        std::vector<std::filesystem::directory_entry> entries;
        for (std::filesystem::directory_iterator iterator(path, error), end; !error && iterator != end; iterator.increment(error)) {
            entries.push_back(*iterator);
        }
        std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs) {
            std::error_code lhsError;
            std::error_code rhsError;
            const auto lhsDirectory = lhs.is_directory(lhsError);
            const auto rhsDirectory = rhs.is_directory(rhsError);
            if (lhsDirectory != rhsDirectory) {
                return lhsDirectory;
            }
            return filenameKey(lhs.path().filename().string()) < filenameKey(rhs.path().filename().string());
        });
        for (const auto& entry : entries) {
            std::error_code entryError;
            const auto isDirectory = entry.is_directory(entryError);
            if (entryError) {
                continue;
            }
            auto* item = new QTreeWidgetItem(&parent);
            item->setText(0, pathToQString(entry.path().filename()));
            item->setData(0, kPathRole, pathToQString(entry.path()));
            if (isDirectory) {
                item->setText(1, QStringLiteral("Folder"));
                item->setIcon(0, coloredIcon(ProjectBrowserItemKind::Folder));
                setItemIdentity(*item, ProjectBrowserItemKind::Folder);
                self(self, *item, entry.path());
                continue;
            }
            ++rootAssetCount_;
            const auto kind = fileKind(entry.path());
            item->setText(1, QString::fromLatin1([kind] {
                switch (kind) {
                case ProjectBrowserItemKind::Scene: return "Scene";
                case ProjectBrowserItemKind::Model: return "Model";
                case ProjectBrowserItemKind::Material: return "Material";
                case ProjectBrowserItemKind::Texture: return "Texture";
                case ProjectBrowserItemKind::Script: return "Script";
                case ProjectBrowserItemKind::Terrain: return "Terrain";
                case ProjectBrowserItemKind::Prefab: return "Prefab";
                default: return "File";
                }
            }()));
            item->setIcon(
                0,
                kind == ProjectBrowserItemKind::Texture
                    ? fileTextureIcon(entry.path())
                    : coloredIcon(kind));
            const auto recordIt = recordsBySourceName.find(filenameKey(entry.path().filename().string()));
            const auto* record = recordIt == recordsBySourceName.end() ? nullptr : recordIt->second;
            if (record == nullptr) {
                setItemIdentity(*item, kind);
                continue;
            }
            setItemIdentity(*item, kind, record->id);
            representedAssets.push_back(record->id);
            if (record->type == assets::AssetType::Model) {
                addModelGroups(*item, *record);
            } else if (const auto texture = assetManager_->texture(record->id)) {
                item->setIcon(0, textureIcon(*texture));
            }
        }
    };

    std::error_code projectError;
    if (!projectRoot_.empty() && std::filesystem::is_directory(projectRoot_, projectError)) {
        addFile(addFile, *assetsRoot, projectRoot_);
    }

    auto* importedRoot = new QTreeWidgetItem(tree_);
    importedRoot->setText(0, QStringLiteral("Imported Assets"));
    importedRoot->setText(1, QStringLiteral("Library"));
    importedRoot->setIcon(0, coloredIcon(ProjectBrowserItemKind::Folder));
    setItemIdentity(*importedRoot, ProjectBrowserItemKind::Folder);
    for (const auto& record : records) {
        if (std::find(representedAssets.begin(), representedAssets.end(), record.id) != representedAssets.end()) {
            continue;
        }
        ++rootAssetCount_;
        auto* item = new QTreeWidgetItem(importedRoot);
        const auto kind = record.type == assets::AssetType::Model
            ? ProjectBrowserItemKind::Model
            : ProjectBrowserItemKind::Texture;
        item->setText(0, QString::fromStdString(record.displayName));
        item->setText(1, QString::fromLatin1(assets::toString(record.type)));
        item->setIcon(0, coloredIcon(kind));
        setItemIdentity(*item, kind, record.id);
        if (record.type == assets::AssetType::Model) {
            addModelGroups(*item, record);
        } else if (const auto texture = assetManager_->texture(record.id)) {
            item->setIcon(0, textureIcon(*texture));
        }
    }
    importedRoot->setHidden(importedRoot->childCount() == 0);
    assetsRoot->setExpanded(true);
    importedRoot->setExpanded(true);
    rebuildFilter();
    if (previousSelection.has_value()) {
        (void)selectAsset(previousSelection->assetId);
    }

    core::logInfo(
        core::LogCategory::Assets,
        "Project Browser rebuilt rootAssets=" + std::to_string(rootAssetCount_)
            + " hiddenSubAssets=" + std::to_string(hiddenSubAssetCount_));
}

std::optional<ProjectBrowserSelection> ProjectBrowserWidget::selection() const
{
    return tree_ == nullptr ? std::nullopt : itemSelection(tree_->currentItem());
}

bool ProjectBrowserWidget::selectAsset(assets::AssetId assetId)
{
    if (tree_ == nullptr || !assetId.isValid()) {
        return false;
    }
    std::vector<QTreeWidgetItem*> pending;
    for (int index = 0; index < tree_->topLevelItemCount(); ++index) {
        pending.push_back(tree_->topLevelItem(index));
    }
    while (!pending.empty()) {
        auto* item = pending.back();
        pending.pop_back();
        if (itemAssetId(*item) == assetId) {
            tree_->setCurrentItem(item);
            tree_->scrollToItem(item);
            return true;
        }
        for (int index = 0; index < item->childCount(); ++index) {
            pending.push_back(item->child(index));
        }
    }
    return false;
}

std::size_t ProjectBrowserWidget::rootAssetCount() const noexcept
{
    return rootAssetCount_;
}

std::size_t ProjectBrowserWidget::visibleItemCount() const
{
    if (tree_ == nullptr) {
        return 0U;
    }
    auto count = std::size_t {0};
    for (int index = 0; index < tree_->topLevelItemCount(); ++index) {
        count += countVisibleItems(*tree_->topLevelItem(index), true);
    }
    return count;
}

std::size_t ProjectBrowserWidget::hiddenSubAssetCount() const noexcept
{
    return hiddenSubAssetCount_;
}

QTreeWidget* ProjectBrowserWidget::treeWidget() const noexcept
{
    return tree_;
}

void ProjectBrowserWidget::rebuildFilter()
{
    if (tree_ == nullptr) {
        return;
    }
    const auto filter = filterEdit_ == nullptr ? QString {} : filterEdit_->text().trimmed();
    for (int index = 0; index < tree_->topLevelItemCount(); ++index) {
        (void)applyFilter(*tree_->topLevelItem(index), filter);
    }
}

void ProjectBrowserWidget::populateExpandedItem(QTreeWidgetItem* item)
{
    if (item == nullptr || item->data(0, kPopulatedRole).toBool()) {
        return;
    }
    const auto kind = itemKind(*item);
    if (kind != ProjectBrowserItemKind::SubAssetGroup
        && kind != ProjectBrowserItemKind::SubAssetPage) {
        return;
    }
    const auto first = item->data(0, kRangeFirstRole).toUInt();
    const auto count = item->data(0, kRangeCountRole).toUInt();
    const auto oldChildren = item->takeChildren();
    for (auto* child : oldChildren) {
        delete child;
    }
    if (kind == ProjectBrowserItemKind::SubAssetGroup && count > kSubAssetPageSize) {
        for (std::uint32_t offset = 0; offset < count; offset += kSubAssetPageSize) {
            const auto pageCount = std::min(kSubAssetPageSize, count - offset);
            auto* page = new QTreeWidgetItem(item);
            page->setText(0, QStringLiteral("%1 - %2").arg(offset + 1U).arg(offset + pageCount));
            page->setText(1, QStringLiteral("Page"));
            page->setIcon(0, coloredIcon(ProjectBrowserItemKind::SubAssetPage));
            setItemIdentity(*page, ProjectBrowserItemKind::SubAssetPage, itemAssetId(*item));
            page->setData(0, kGroupRole, item->data(0, kGroupRole));
            page->setData(0, kRangeFirstRole, offset);
            page->setData(0, kRangeCountRole, pageCount);
            page->setData(0, kPopulatedRole, false);
            addLazyPlaceholder(*page);
        }
    } else {
        populateModelGroup(item, first, count);
    }
    item->setData(0, kPopulatedRole, true);
}

void ProjectBrowserWidget::populateModelGroup(
    QTreeWidgetItem* item,
    std::uint32_t first,
    std::uint32_t count)
{
    if (item == nullptr || assetManager_ == nullptr) {
        return;
    }
    const auto assetId = itemAssetId(*item);
    const auto model = assetManager_->model(assetId);
    if (model == nullptr) {
        return;
    }
    const auto group = static_cast<ModelGroup>(item->data(0, kGroupRole).toInt());
    const auto maximum = modelGroupCount(*model, group);
    const auto end = std::min<std::uint64_t>(
        static_cast<std::uint64_t>(first) + count,
        maximum);
    for (auto index = first; index < end; ++index) {
        auto* child = new QTreeWidgetItem(item);
        ProjectBrowserItemKind kind = ProjectBrowserItemKind::ModelNode;
        QString name;
        QIcon icon;
        switch (group) {
        case ModelGroup::Meshes:
            kind = ProjectBrowserItemKind::Mesh;
            name = QStringLiteral("Mesh %1").arg(index + 1U);
            icon = coloredIcon(kind);
            break;
        case ModelGroup::Materials:
            kind = ProjectBrowserItemKind::Material;
            name = QString::fromStdString(model->materials[index].name);
            if (name.isEmpty()) {
                name = QStringLiteral("Material %1").arg(index + 1U);
            }
            icon = materialIcon(model->materials[index]);
            break;
        case ModelGroup::Textures:
            kind = ProjectBrowserItemKind::Texture;
            name = QString::fromStdString(model->textures[index].name);
            if (name.isEmpty()) {
                name = QStringLiteral("Texture %1").arg(index + 1U);
            }
            icon = textureIcon(model->textures[index]);
            break;
        case ModelGroup::Nodes:
            kind = ProjectBrowserItemKind::ModelNode;
            if (!model->editorInstances.empty()) {
                name = QString::fromStdString(model->editorInstances[index].name);
            }
            if (name.isEmpty()) {
                name = QStringLiteral("Node %1").arg(index + 1U);
            }
            icon = coloredIcon(kind);
            break;
        }
        child->setText(0, name);
        child->setText(1, modelGroupItemType(group));
        child->setIcon(0, icon);
        setItemIdentity(
            *child,
            kind,
            assetId,
            index,
            assets::makeSubAssetId(assetId, subAssetKind(group), index));
    }
}

void ProjectBrowserWidget::updateSelection()
{
    const auto selected = selection();
    if (!selected.has_value()) {
        return;
    }
    std::string message = "Project selection assetId=" + std::to_string(selected->assetId.value());
    if (selected->subAssetId != 0U) {
        message += " subAssetId=" + std::to_string(selected->subAssetId);
    }
    core::logInfo(core::LogCategory::Editor, message);
    if (selectionChangedCallback_) {
        selectionChangedCallback_(*selected);
    }
}

} // namespace projectunity::editor
