#include <projectunity/editor/ProjectBrowserWidget.hpp>

#include <QApplication>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

namespace {

int fail(const char* message)
{
    std::cerr << message << '\n';
    return EXIT_FAILURE;
}

class TestAssetManager final : public projectunity::assets::IAssetManager {
public:
    std::shared_ptr<const projectunity::assets::ModelAsset> modelAsset;
    std::shared_ptr<const projectunity::assets::TextureAsset> textureAsset;
    std::vector<projectunity::assets::AssetRecord> assetRecords;

    [[nodiscard]] std::shared_ptr<const projectunity::assets::ModelAsset> model(
        projectunity::assets::AssetId id) const override
    {
        return modelAsset != nullptr && modelAsset->id == id ? modelAsset : nullptr;
    }

    [[nodiscard]] std::shared_ptr<const projectunity::assets::TextureAsset> texture(
        projectunity::assets::AssetId id) const override
    {
        return textureAsset != nullptr && textureAsset->id == id ? textureAsset : nullptr;
    }

    [[nodiscard]] std::vector<projectunity::assets::AssetRecord> records() const override
    {
        return assetRecords;
    }
};

std::shared_ptr<projectunity::assets::ModelAsset> makeLargeModel()
{
    auto model = std::make_shared<projectunity::assets::ModelAsset>();
    model->id = projectunity::assets::AssetId(101);
    model->name = "RockField";
    model->primitives.resize(600U);
    model->materials.resize(2U);
    model->materials[0].name = "Granite";
    model->materials[0].baseColor = {0.4F, 0.45F, 0.5F, 1.0F};
    model->materials[1].name = "Moss";
    model->materials[1].baseColor = {0.18F, 0.5F, 0.2F, 1.0F};
    model->textures.resize(1U);
    model->textures[0].name = "Granite Albedo";
    model->textures[0].width = 1U;
    model->textures[0].height = 1U;
    model->textures[0].rgba8 = {128U, 140U, 150U, 255U};
    model->editorInstances.resize(600U);
    for (std::size_t index = 0; index < model->editorInstances.size(); ++index) {
        model->editorInstances[index].name = "Rock " + std::to_string(index + 1U);
    }
    return model;
}

QTreeWidgetItem* findAssetItem(QTreeWidget& tree, std::uint64_t assetId)
{
    constexpr int assetIdRole = Qt::UserRole + 2;
    std::vector<QTreeWidgetItem*> pending;
    for (int index = 0; index < tree.topLevelItemCount(); ++index) {
        pending.push_back(tree.topLevelItem(index));
    }
    while (!pending.empty()) {
        auto* item = pending.back();
        pending.pop_back();
        if (item->data(0, assetIdRole).toULongLong() == assetId) {
            return item;
        }
        for (int index = 0; index < item->childCount(); ++index) {
            pending.push_back(item->child(index));
        }
    }
    return nullptr;
}

QTreeWidgetItem* findChildPrefix(QTreeWidgetItem& parent, const QString& prefix)
{
    for (int index = 0; index < parent.childCount(); ++index) {
        auto* child = parent.child(index);
        if (child->text(0).startsWith(prefix)) {
            return child;
        }
    }
    return nullptr;
}

} // namespace

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    const auto root = std::filesystem::temp_directory_path() / "projectunity_project_browser_tests";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root / "Assets" / "Scripts", error);
    if (error) {
        return fail("unable to create Project Browser test directory");
    }
    {
        std::ofstream(root / "Assets" / "RockField.glb") << "fixture";
        std::ofstream(root / "Assets" / "Scripts" / "Player.cpp") << "void update() {}";
    }

    TestAssetManager assets;
    assets.modelAsset = makeLargeModel();
    auto texture = std::make_shared<projectunity::assets::TextureAsset>();
    texture->id = projectunity::assets::AssetId(202);
    texture->name = "External Sky";
    texture->width = 1U;
    texture->height = 1U;
    texture->rgba8 = {60U, 110U, 190U, 255U};
    assets.textureAsset = texture;
    assets.assetRecords = {
        {
            projectunity::assets::AssetId(101),
            projectunity::assets::AssetType::Model,
            "RockField",
            "RockField.glb",
            "101.ffult",
            1'800U,
            1'800U,
            1U,
        },
        {
            projectunity::assets::AssetId(202),
            projectunity::assets::AssetType::Texture2D,
            "External Sky",
            "external_sky.hdr",
            "202.ffult",
            0U,
            0U,
            1U,
        },
    };

    projectunity::editor::ProjectBrowserWidget browser;
    browser.setAssetManager(&assets);
    browser.setProjectRoot(root / "Assets");
    browser.rebuild();
    if (browser.rootAssetCount() != 3U) {
        return fail("Project Browser did not show filesystem assets plus unmatched imported asset");
    }
    if (browser.hiddenSubAssetCount() != 1'203U) {
        return fail("Project Browser sub-asset metadata count is incorrect");
    }
    if (browser.visibleItemCount() > 16U) {
        return fail("Large imported model expanded too many Project Browser rows by default");
    }

    auto* modelItem = findAssetItem(*browser.treeWidget(), 101U);
    if (modelItem == nullptr || modelItem->childCount() != 4 || modelItem->isExpanded()) {
        return fail("Imported model root did not expose four collapsed sub-asset groups");
    }
    auto* nodes = findChildPrefix(*modelItem, QStringLiteral("Nodes / Primitives"));
    if (nodes == nullptr) {
        return fail("Imported model Nodes / Primitives group is missing");
    }
    nodes->setExpanded(true);
    QApplication::processEvents();
    if (nodes->childCount() != 3) {
        return fail("Large sub-asset group was not split into lazy pages");
    }
    auto* firstPage = nodes->child(0);
    firstPage->setExpanded(true);
    QApplication::processEvents();
    if (firstPage->childCount() != 256) {
        return fail("Lazy sub-asset page did not materialize the expected range");
    }
    auto* firstNode = firstPage->child(0);
    constexpr int subAssetIdRole = Qt::UserRole + 3;
    const auto firstStableId = firstNode->data(0, subAssetIdRole).toULongLong();
    if (firstStableId == 0U) {
        return fail("Project Browser sub-asset did not receive a stable id");
    }

    browser.rebuild();
    modelItem = findAssetItem(*browser.treeWidget(), 101U);
    nodes = modelItem == nullptr ? nullptr : findChildPrefix(*modelItem, QStringLiteral("Nodes / Primitives"));
    if (nodes == nullptr) {
        return fail("Project Browser rebuild lost model sub-assets");
    }
    nodes->setExpanded(true);
    QApplication::processEvents();
    nodes->child(0)->setExpanded(true);
    QApplication::processEvents();
    const auto rebuiltStableId = nodes->child(0)->child(0)->data(0, subAssetIdRole).toULongLong();
    if (rebuiltStableId != firstStableId) {
        return fail("Project Browser sub-asset id changed across rebuild");
    }

    std::filesystem::remove_all(root, error);
    return EXIT_SUCCESS;
}
