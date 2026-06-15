#include <projectunity/editor/SceneHierarchyWidget.hpp>
#include <projectunity/editor/ProjectAssetTreeWidget.hpp>

#include <QApplication>
#include <QDropEvent>
#include <QMimeData>
#include <QTreeWidgetItem>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
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

    [[nodiscard]] std::shared_ptr<const projectunity::assets::ModelAsset> model(
        projectunity::assets::AssetId id) const override
    {
        return modelAsset != nullptr && modelAsset->id == id ? modelAsset : nullptr;
    }

    [[nodiscard]] std::shared_ptr<const projectunity::assets::TextureAsset> texture(
        projectunity::assets::AssetId) const override
    {
        return nullptr;
    }

    [[nodiscard]] std::vector<projectunity::assets::AssetRecord> records() const override
    {
        return {};
    }
};

std::shared_ptr<projectunity::assets::ModelAsset> makeLargeModel()
{
    auto model = std::make_shared<projectunity::assets::ModelAsset>();
    model->id = projectunity::assets::AssetId(7001U);
    model->name = "NodePerformanceTest";
    model->primitives.resize(400U);
    model->materials.resize(100U);
    model->textures.resize(100U);
    model->primitiveInstances.resize(10'000U);
    model->editorInstances.resize(10'000U);
    for (std::size_t index = 0; index < model->primitiveInstances.size(); ++index) {
        auto& instance = model->primitiveInstances[index];
        instance.primitiveIndex = static_cast<std::uint32_t>(index % model->primitives.size());
        instance.bounds.center = {static_cast<float>(index), 2.0F, 3.0F};
        auto& editor = model->editorInstances[index];
        editor.name = index % 2U == 0U
            ? "Rock_Granite_" + std::to_string(index + 1U)
            : "Rock_Basalt_" + std::to_string(index + 1U);
        editor.sourcePrimitiveIndex = instance.primitiveIndex;
        editor.bounds = instance.bounds;
    }
    return model;
}

QTreeWidgetItem* findChildPrefix(QTreeWidgetItem& parent, const QString& prefix)
{
    for (int index = 0; index < parent.childCount(); ++index) {
        auto* child = parent.child(index);
        if (child->text(0).startsWith(prefix)) return child;
    }
    return nullptr;
}

} // namespace

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    TestAssetManager assets;
    assets.modelAsset = makeLargeModel();

    projectunity::scene::Scene scene;
    auto& root = scene.createEntity("NodePerformanceTest");
    const auto rootId = root.id;
    projectunity::scene::MeshRendererComponent renderer;
    renderer.modelAssetId = assets.modelAsset->id;
    (void)scene.setMeshRenderer(rootId, renderer);

    projectunity::editor::SceneHierarchyWidget hierarchy;
    hierarchy.setScene(&scene);
    hierarchy.setAssetManager(&assets);
    auto droppedAssetId = projectunity::assets::AssetId {};
    hierarchy.setAssetDropCallback([&droppedAssetId](projectunity::assets::AssetId assetId, std::filesystem::path) {
        droppedAssetId = assetId;
    });
    QMimeData dropData;
    dropData.setData(projectunity::editor::kProjectUnityAssetIdMime, QByteArray::number(assets.modelAsset->id.value()));
    QDropEvent dropEvent(QPointF(4.0, 4.0), Qt::CopyAction, &dropData, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&hierarchy, &dropEvent);
    if (droppedAssetId != assets.modelAsset->id || !dropEvent.isAccepted()) {
        return fail("Hierarchy did not accept the Project Browser AssetId drag payload");
    }
    hierarchy.rebuild();
    if (hierarchy.topLevelItemCount() != 1 || scene.entityCount() != 1U) {
        return fail("Hierarchy eagerly materialized scene nodes");
    }
    auto* rootItem = hierarchy.topLevelItem(0);
    if (rootItem == nullptr || rootItem->childCount() != 4 || rootItem->icon(0).isNull()) {
        return fail("Model root did not expose four icon groups");
    }
    auto* meshes = findChildPrefix(*rootItem, QStringLiteral("Meshes (400)"));
    auto* materials = findChildPrefix(*rootItem, QStringLiteral("Materials (100)"));
    auto* textures = findChildPrefix(*rootItem, QStringLiteral("Textures (100)"));
    auto* nodes = findChildPrefix(*rootItem, QStringLiteral("Nodes / Primitives (10000)"));
    if (meshes == nullptr || materials == nullptr || textures == nullptr || nodes == nullptr
        || meshes->icon(0).isNull() || materials->icon(0).isNull()
        || textures->icon(0).isNull() || nodes->icon(0).isNull()) {
        return fail("Hierarchy asset groups or icons are missing");
    }

    nodes->setExpanded(true);
    QApplication::processEvents();
    if (nodes->childCount() != 40 || scene.entityCount() != 1U) {
        return fail("Hierarchy did not page 10000 nodes lazily");
    }
    auto* firstPage = nodes->child(0);
    firstPage->setExpanded(true);
    QApplication::processEvents();
    if (firstPage->childCount() != 256 || scene.entityCount() != 1U) {
        return fail("Hierarchy page did not lazily expose 256 node references");
    }

    auto* firstNode = firstPage->child(0);
    hierarchy.setCurrentItem(firstNode);
    const auto proxyId = hierarchy.selectedSceneEntity();
    const auto* proxy = scene.findEntity(proxyId);
    if (!proxyId.isValid() || scene.entityCount() != 2U || proxy == nullptr
        || proxy->parent != std::optional<projectunity::scene::EntityId>(rootId)
        || !proxy->meshRenderer.has_value() || proxy->meshRenderer->renderable
        || proxy->meshRenderer->modelAssetId != assets.modelAsset->id
        || proxy->meshRenderer->editorInstanceIndex.value_or(UINT32_MAX) != 0U
        || proxy->transform.position.x != 0.0F || proxy->transform.position.y != 2.0F) {
        return fail("Selecting a virtual node did not create one lightweight editable proxy");
    }

    auto overrideTransform = proxy->transform;
    overrideTransform.position.x = 42.0F;
    overrideTransform.rotationEuler.y = 35.0F;
    overrideTransform.scale = {1.25F, 1.25F, 1.25F};
    (void)scene.setTransform(proxyId, overrideTransform);
    std::string error;
    const auto serialized = scene.serialize(&error);
    projectunity::scene::Scene loaded;
    if (serialized.empty() || !loaded.deserialize(serialized, &error)) {
        return fail("Hierarchy proxy scene override did not serialize");
    }
    const auto* loadedProxy = loaded.findEntity(proxyId);
    if (loadedProxy == nullptr || loadedProxy->transform.position.x != 42.0F
        || loadedProxy->transform.rotationEuler.y != 35.0F
        || loadedProxy->meshRenderer->modelAssetId != assets.modelAsset->id
        || assets.modelAsset->primitives.size() != 400U
        || assets.modelAsset->textures.size() != 100U) {
        return fail("Scene load lost the node override or mutated the source asset");
    }

    projectunity::editor::SceneHierarchyWidget loadedHierarchy;
    loadedHierarchy.setScene(&loaded);
    loadedHierarchy.setAssetManager(&assets);
    loadedHierarchy.rebuild(proxyId);
    if (loadedHierarchy.selectedSceneEntity() != proxyId
        || loadedHierarchy.topLevelItem(0)->childCount() != 4) {
        return fail("Loaded proxy was not restored inside the lazy Nodes group");
    }
    return EXIT_SUCCESS;
}
