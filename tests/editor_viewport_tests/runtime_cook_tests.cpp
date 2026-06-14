#include "ViewportRenderWorld.hpp"

#include <projectunity/assets/AssetManager.hpp>
#include <projectunity/scene/Scene.hpp>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
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

    [[nodiscard]] std::shared_ptr<const projectunity::assets::ModelAsset> model(projectunity::assets::AssetId id) const override
    {
        return modelAsset != nullptr && modelAsset->id == id ? modelAsset : nullptr;
    }

    [[nodiscard]] std::shared_ptr<const projectunity::assets::TextureAsset> texture(projectunity::assets::AssetId) const override
    {
        return nullptr;
    }

    [[nodiscard]] std::vector<projectunity::assets::AssetRecord> records() const override
    {
        return {};
    }
};

projectunity::assets::MeshBounds boundsAt(projectunity::math::Vec3 center)
{
    return {
        center - projectunity::math::Vec3 {0.5F, 0.5F, 0.5F},
        center + projectunity::math::Vec3 {0.5F, 0.5F, 0.5F},
        center,
        0.8661F,
    };
}

std::shared_ptr<projectunity::assets::ModelAsset> runtimeCookModel()
{
    auto model = std::make_shared<projectunity::assets::ModelAsset>();
    model->id = projectunity::assets::AssetId(4242);
    model->materials.resize(1U);
    for (std::uint32_t primitiveIndex = 0; primitiveIndex < 2U; ++primitiveIndex) {
        projectunity::assets::MeshPrimitive primitive;
        primitive.materialIndex = 0U;
        primitive.bounds = boundsAt({});
        primitive.vertices.resize(3U);
        primitive.indices = {0U, 1U, 2U};
        model->primitives.push_back(std::move(primitive));

        projectunity::assets::MeshPrimitiveInstance instance;
        instance.primitiveIndex = primitiveIndex;
        instance.transform[14] = 10.0F + static_cast<float>(primitiveIndex) * 4.0F;
        instance.bounds = boundsAt({0.0F, 0.0F, instance.transform[14]});
        model->primitiveInstances.push_back(instance);
    }
    projectunity::assets::MeshPrimitiveCluster cluster;
    cluster.primitiveInstanceIndices = {0U, 1U};
    cluster.bounds = boundsAt({0.0F, 0.0F, 12.0F});
    cluster.bounds.minimum.z = 9.5F;
    cluster.bounds.maximum.z = 14.5F;
    cluster.bounds.radius = 2.6F;
    model->primitiveClusters.push_back(std::move(cluster));
    return model;
}

} // namespace

int main()
{
    TestAssetManager assets;
    assets.modelAsset = runtimeCookModel();

    projectunity::scene::Scene runtimeScene;
    auto& runtimeAsset = runtimeScene.createEntity("RuntimeAssetInstance: Rock Field");
    const auto runtimeAssetId = runtimeAsset.id;
    projectunity::scene::MeshRendererComponent rootRenderer;
    rootRenderer.modelAssetId = assets.modelAsset->id;
    runtimeAsset.meshRenderer = rootRenderer;

    auto& mask = runtimeScene.createEntity("RuntimeBatchMask: Rock 1", runtimeAssetId);
    projectunity::scene::MeshRendererComponent maskRenderer;
    maskRenderer.modelAssetId = assets.modelAsset->id;
    maskRenderer.primitiveInstanceIndex = 0U;
    maskRenderer.renderable = false;
    maskRenderer.runtimeCook.staticBatchable = false;
    maskRenderer.runtimeCook.mutableRuntime = true;
    mask.meshRenderer = maskRenderer;

    auto& dynamicRock = runtimeScene.createEntity("RuntimeEntity: Rock 1", runtimeAssetId);
    const auto dynamicRockId = dynamicRock.id;
    dynamicRock.transform.position = {0.0F, 0.0F, 10.0F};
    projectunity::scene::MeshRendererComponent dynamicRenderer;
    dynamicRenderer.modelAssetId = assets.modelAsset->id;
    dynamicRenderer.primitiveInstanceIndex = 0U;
    dynamicRenderer.renderable = true;
    dynamicRenderer.runtimeCook.staticBatchable = false;
    dynamicRenderer.runtimeCook.mutableRuntime = true;
    dynamicRock.meshRenderer = dynamicRenderer;

    const projectunity::editor::ViewportRenderWorldCamera camera {
        {0.0F, 0.0F, 0.0F},
        {1.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F},
        {0.0F, 0.0F, 1.0F},
        1.04719755F,
        1.0F,
        0.05F,
        100.0F,
    };

    projectunity::editor::ViewportRenderWorld world;
    std::vector<projectunity::renderer::RenderMeshDraw> draws;
    std::vector<projectunity::renderer::RenderLight> lights;
    (void)world.buildFrame(&runtimeScene, &assets, {}, camera, {}, 1080, true, draws, lights);

    bool sawStaticCookedRemainder = false;
    bool sawDynamicExtractedRock = false;
    bool sawDuplicateStaticRock = false;
    for (const auto& draw : draws) {
        sawStaticCookedRemainder = sawStaticCookedRemainder
            || (draw.sceneNodeId == runtimeAssetId.value() && draw.primitiveIndex == 1U);
        sawDynamicExtractedRock = sawDynamicExtractedRock
            || (draw.sceneNodeId == dynamicRockId.value() && draw.primitiveIndex == 0U);
        sawDuplicateStaticRock = sawDuplicateStaticRock
            || (draw.sceneNodeId == runtimeAssetId.value() && draw.primitiveIndex == 0U);
    }

    if (!sawStaticCookedRemainder || !sawDynamicExtractedRock || sawDuplicateStaticRock) {
        std::cerr << "draws=" << draws.size()
                  << " staticRemainder=" << sawStaticCookedRemainder
                  << " dynamic=" << sawDynamicExtractedRock
                  << " duplicate=" << sawDuplicateStaticRock << '\n';
        for (const auto& draw : draws) {
            std::cerr << "  sceneNode=" << draw.sceneNodeId
                      << " primitive=" << draw.primitiveIndex
                      << " chunk=" << draw.renderChunkId << '\n';
        }
        return fail("Runtime cook masking did not split static cooked data from mutable runtime entity");
    }
    if (draws.size() != 2U) {
        return fail("Runtime cook produced unexpected draw packet count");
    }

    return EXIT_SUCCESS;
}
