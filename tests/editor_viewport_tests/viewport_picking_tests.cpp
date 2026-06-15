#include <projectunity/assets/AssetSubAssetId.hpp>
#include <projectunity/editor/ViewportWidget.hpp>

#include <QApplication>

#include <cstdlib>
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

std::shared_ptr<projectunity::assets::ModelAsset> makePickModel()
{
    auto model = std::make_shared<projectunity::assets::ModelAsset>();
    model->id = projectunity::assets::AssetId(5150);
    model->materials.resize(1U);

    projectunity::assets::MeshPrimitive primitive;
    primitive.vertices.resize(3U);
    primitive.vertices[0].position = {-1.0F, -1.0F, 0.0F};
    primitive.vertices[1].position = {1.0F, -1.0F, 0.0F};
    primitive.vertices[2].position = {0.0F, 1.0F, 0.0F};
    primitive.indices = {0U, 1U, 2U};
    primitive.bounds.minimum = {-1.0F, -1.0F, -0.01F};
    primitive.bounds.maximum = {1.0F, 1.0F, 0.01F};
    primitive.bounds.center = {};
    primitive.bounds.radius = 1.42F;
    model->primitives.push_back(primitive);

    projectunity::assets::MeshPrimitiveInstance instance;
    instance.primitiveIndex = 0U;
    instance.transform[14] = 10.0F;
    instance.bounds.minimum = {-1.0F, -1.0F, 9.99F};
    instance.bounds.maximum = {1.0F, 1.0F, 10.01F};
    instance.bounds.center = {0.0F, 0.0F, 10.0F};
    instance.bounds.radius = 1.42F;
    model->primitiveInstances.push_back(instance);
    return model;
}

} // namespace

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    TestAssetManager assets;
    assets.modelAsset = makePickModel();

    projectunity::scene::Scene scene;
    auto& root = scene.createEntity("RockField Asset Instance");
    const auto rootId = root.id;
    projectunity::scene::MeshRendererComponent rootRenderer;
    rootRenderer.modelAssetId = assets.modelAsset->id;
    root.meshRenderer = rootRenderer;

    auto& proxy = scene.createEntity("Rock 1", rootId);
    const auto proxyId = proxy.id;
    proxy.transform.position = {0.0F, 0.0F, 10.0F};
    projectunity::scene::MeshRendererComponent proxyRenderer;
    proxyRenderer.modelAssetId = assets.modelAsset->id;
    proxyRenderer.primitiveInstanceIndex = 0U;
    proxyRenderer.renderable = false;
    proxy.meshRenderer = proxyRenderer;

    projectunity::editor::ViewportWidget viewport(projectunity::editor::ViewportMode::Scene);
    viewport.resize(640, 480);
    viewport.setAssetManager(&assets);
    viewport.setScene(&scene);
    viewport.setCameraForTesting({0.0F, 0.0F, 10.0F}, 10.0F, 0.0F, 0.0F);

    const auto ownerPick = viewport.pickResultAt({320.0, 240.0});
    const auto expectedSubObjectId = projectunity::assets::makeSubAssetId(
        assets.modelAsset->id,
        projectunity::assets::SubAssetKind::Node,
        0U);
    if (!ownerPick.has_value()
        || ownerPick->ownerEntityId != rootId
        || ownerPick->selectedEntityId != rootId
        || ownerPick->assetId != assets.modelAsset->id
        || ownerPick->subObjectIndex.value_or(UINT32_MAX) != 0U
        || ownerPick->subObjectId != expectedSubObjectId) {
        return fail("Normal viewport picking did not select the owning asset instance");
    }

    viewport.setPickMode(projectunity::editor::ViewportPickMode::SubObject);
    const auto subObjectPick = viewport.pickResultAt({320.0, 240.0});
    if (!subObjectPick.has_value()
        || subObjectPick->ownerEntityId != rootId
        || subObjectPick->selectedEntityId != proxyId
        || subObjectPick->subObjectId != expectedSubObjectId) {
        return fail("Sub-object viewport picking did not select the editable proxy");
    }

    if (!scene.destroyEntity(proxyId)) {
        return fail("Unable to remove test proxy");
    }
    const auto implicitSubObjectPick = viewport.pickResultAt({320.0, 240.0});
    if (!implicitSubObjectPick.has_value()
        || implicitSubObjectPick->ownerEntityId != rootId
        || implicitSubObjectPick->selectedEntityId != rootId
        || implicitSubObjectPick->subObjectIndex.value_or(UINT32_MAX) != 0U
        || implicitSubObjectPick->subObjectId != expectedSubObjectId) {
        return fail("Picking lost sub-object identity when the hierarchy proxy was absent");
    }

    return EXIT_SUCCESS;
}
