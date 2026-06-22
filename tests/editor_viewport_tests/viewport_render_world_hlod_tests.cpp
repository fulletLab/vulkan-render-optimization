#include "ViewportRenderWorld.hpp"
#include "ViewportMeshLod.hpp"
#include "ViewportRenderWorldHlodPolicy.hpp"

#include <projectunity/assets/AssetManager.hpp>
#include <projectunity/renderer/RenderShadowSetup.hpp>
#include <projectunity/scene/Scene.hpp>

#include <algorithm>
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

void setEnv(const char* name, const char* value)
{
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void configureDeterministicHlodPolicy()
{
    setEnv("PROJECTUNITY_HLOD_SCREEN_SIZE_THRESHOLD", "2.0");
    setEnv("PROJECTUNITY_HLOD_DISTANCE_THRESHOLD", "8");
    setEnv("PROJECTUNITY_HLOD_CHUNK_COLLAPSE_DISTANCE", "8");
    setEnv("PROJECTUNITY_HLOD_MAX_VISIBLE_CHUNKS", "16");
    setEnv("PROJECTUNITY_HLOD_MAX_DRAW_PACKETS", "32");
    setEnv("PROJECTUNITY_HLOD_MAX_SHADOW_CASTERS", "32");
    setEnv("PROJECTUNITY_HLOD_MAX_DETAILED_TRIANGLES", "4000000");
    setEnv("PROJECTUNITY_HLOD_LOD_BIAS", "1");
    setEnv("PROJECTUNITY_LOD_HYSTERESIS_RATIO", "0.15");
    setEnv("PROJECTUNITY_HLOD_HYSTERESIS_RATIO", "0.15");
    setEnv("PROJECTUNITY_HLOD_DEBUG_OVERRIDE", "");
}

struct TestAssetManager final : projectunity::assets::IAssetManager {
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

projectunity::assets::MeshPrimitive heavySourcePrimitive()
{
    projectunity::assets::MeshPrimitive primitive;
    primitive.materialIndex = 0U;
    primitive.bounds.minimum = {-0.5F, -0.5F, -0.5F};
    primitive.bounds.maximum = {0.5F, 0.5F, 0.5F};
    primitive.bounds.center = {0.0F, 0.0F, 0.0F};
    primitive.bounds.radius = 0.8661F;
    primitive.vertices.push_back({{-0.5F, -0.5F, 0.0F}});
    primitive.vertices.push_back({{0.5F, -0.5F, 0.0F}});
    primitive.vertices.push_back({{0.0F, 0.5F, 0.0F}});
    constexpr std::uint32_t kSourceTriangles = 27'000U;
    primitive.indices.reserve(kSourceTriangles * 3U);
    for (std::uint32_t triangle = 0; triangle < kSourceTriangles; ++triangle) {
        primitive.indices.insert(primitive.indices.end(), {0U, 1U, 2U});
    }
    primitive.lods.push_back({{0U, 1U, 2U}, 0.02F});
    return primitive;
}

std::shared_ptr<projectunity::assets::ModelAsset> heavyChunkedModel(projectunity::assets::AssetId id)
{
    auto model = std::make_shared<projectunity::assets::ModelAsset>();
    model->id = id;
    model->materials.resize(1U);
    model->primitives.push_back(heavySourcePrimitive());
    constexpr std::uint32_t kInstanceCount = 320U;
    for (std::uint32_t index = 0; index < kInstanceCount; ++index) {
        projectunity::assets::MeshPrimitiveInstance instance;
        instance.primitiveIndex = 0U;
        instance.transform[12] = -39.0F + static_cast<float>(index % 40U) * 2.0F;
        instance.transform[13] = 0.0F;
        instance.transform[14] = 80.0F + static_cast<float>(index / 40U) * 2.0F;
        instance.bounds = model->primitives.front().bounds;
        model->primitiveInstances.push_back(instance);
    }
    return model;
}

std::shared_ptr<projectunity::assets::ModelAsset> incompleteOverviewModel(projectunity::assets::AssetId id)
{
    auto model = std::make_shared<projectunity::assets::ModelAsset>();
    model->id = id;
    model->materials.resize(1U);
    model->primitives.push_back(heavySourcePrimitive());
    auto invalidOverviewPrimitive = heavySourcePrimitive();
    invalidOverviewPrimitive.lods.front().indices = {0U, 1U, 99U};
    model->primitives.push_back(std::move(invalidOverviewPrimitive));

    constexpr std::uint32_t kInstanceCount = 320U;
    for (std::uint32_t index = 0; index < kInstanceCount; ++index) {
        projectunity::assets::MeshPrimitiveInstance instance;
        instance.primitiveIndex = index < 200U ? 0U : 1U;
        instance.transform[12] = -39.0F + static_cast<float>(index % 40U) * 2.0F;
        instance.transform[13] = 0.0F;
        instance.transform[14] = 80.0F + static_cast<float>(index / 40U) * 2.0F;
        instance.bounds = model->primitives[instance.primitiveIndex].bounds;
        model->primitiveInstances.push_back(instance);
    }
    return model;
}

std::shared_ptr<projectunity::assets::ModelAsset> singleRockModel(projectunity::assets::AssetId id)
{
    auto model = std::make_shared<projectunity::assets::ModelAsset>();
    model->id = id;
    model->materials.resize(1U);
    model->primitives.push_back(heavySourcePrimitive());
    return model;
}

projectunity::editor::ViewportRenderWorldCamera overviewCamera()
{
    return {
        {0.0F, 0.0F, -160.0F},
        {1.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F},
        {0.0F, 0.0F, 1.0F},
        1.04719755F,
        16.0F / 9.0F,
        0.05F,
        4000.0F,
    };
}

projectunity::editor::ViewportRenderWorldFrame buildFrame(
    projectunity::editor::ViewportRenderWorld& world,
    projectunity::scene::Scene& scene,
    const TestAssetManager& assets,
    std::vector<projectunity::renderer::RenderMeshDraw>& draws)
{
    std::vector<projectunity::renderer::RenderLight> lights;
    const auto settings = projectunity::editor::viewportAssetLodSettingsFromEnvironment();
    return world.buildFrame(&scene, &assets, {}, overviewCamera(), {}, 1080, settings, false, draws, lights);
}

projectunity::editor::ViewportRenderWorldFrame buildFrameWithCamera(
    projectunity::editor::ViewportRenderWorld& world,
    projectunity::scene::Scene& scene,
    const TestAssetManager& assets,
    const projectunity::editor::ViewportRenderWorldCamera& camera,
    std::vector<projectunity::renderer::RenderMeshDraw>& draws)
{
    std::vector<projectunity::renderer::RenderLight> lights;
    draws.clear();
    const auto settings = projectunity::editor::viewportAssetLodSettingsFromEnvironment();
    return world.buildFrame(&scene, &assets, {}, camera, {}, 1080, settings, false, draws, lights);
}

} // namespace

int main()
{
    configureDeterministicHlodPolicy();

    const std::array<projectunity::math::Vec3, 8> enclosingBounds {{
        {-10.0F, -4.0F, -20.0F}, {10.0F, -4.0F, -20.0F},
        {-10.0F, 4.0F, -20.0F}, {10.0F, 4.0F, -20.0F},
        {-10.0F, -4.0F, 20.0F}, {10.0F, -4.0F, 20.0F},
        {-10.0F, 4.0F, 20.0F}, {10.0F, 4.0F, 20.0F},
    }};
    const auto forwardDistance = projectunity::editor::viewportLodDistanceToBounds(
        {}, {0.0F, 0.0F, 1.0F}, enclosingBounds, 0.05F);
    const auto pitchedDistance = projectunity::editor::viewportLodDistanceToBounds(
        {}, {0.0F, -0.7071067F, 0.7071067F}, enclosingBounds, 0.05F);
    if (std::abs(forwardDistance - pitchedDistance) > 0.0001F) {
        return fail("LOD distance changed when only camera pitch changed inside the same bounds");
    }

    const auto hysteresisPrimitive = heavySourcePrimitive();
    const auto heldDetailed = projectunity::editor::evaluateViewportMeshLod(
        hysteresisPrimitive, hysteresisPrimitive.bounds.radius, 15.0F, 1.04719755F, 1080.0F, false, 1.0F, 0U, 0.15F);
    if (heldDetailed.lodIndex != 0U || !heldDetailed.hysteresisActive) {
        return fail("LOD hysteresis did not hold the previous detailed mesh near a transition");
    }

    projectunity::editor::ViewportWorldBounds enclosingWorldBounds;
    enclosingWorldBounds.center = {};
    enclosingWorldBounds.radius = 22.0F;
    enclosingWorldBounds.corners = enclosingBounds;
    auto frontCamera = overviewCamera();
    frontCamera.eye = {};
    auto downCamera = frontCamera;
    downCamera.forward = {0.0F, -0.7071067F, 0.7071067F};
    downCamera.up = {0.0F, 0.7071067F, 0.7071067F};
    const auto settings = projectunity::editor::viewportAssetLodSettingsFromEnvironment();
    for (const auto& camera : {frontCamera, downCamera}) {
        projectunity::editor::ViewportHlodReason reason = projectunity::editor::ViewportHlodReason::None;
        const auto evaluation = projectunity::editor::evaluateViewportHlod(enclosingWorldBounds, camera, 1080);
        if (!evaluation.insideBounds
            || projectunity::editor::viewportRootHlodEligible(evaluation, settings, true, true, false, reason)) {
            return fail("HLOD activated inside enclosing asset bounds after a camera pitch change");
        }
    }

    projectunity::editor::ViewportWorldBounds flatAssetBounds;
    flatAssetBounds.center = {0.0F, 0.0F, 150.0F};
    flatAssetBounds.radius = projectunity::math::Vec3 {100.0F, 1.0F, 50.0F}.length();
    flatAssetBounds.corners = {{
        {-100.0F, -1.0F, 100.0F}, {100.0F, -1.0F, 100.0F},
        {-100.0F, 1.0F, 100.0F}, {100.0F, 1.0F, 100.0F},
        {-100.0F, -1.0F, 200.0F}, {100.0F, -1.0F, 200.0F},
        {-100.0F, 1.0F, 200.0F}, {100.0F, 1.0F, 200.0F},
    }};
    auto overheadCamera = overviewCamera();
    overheadCamera.eye = {0.0F, 40.0F, 150.0F};
    overheadCamera.forward = {0.0F, -1.0F, 0.0F};
    overheadCamera.up = {0.0F, 0.0F, 1.0F};
    overheadCamera.right = {1.0F, 0.0F, 0.0F};
    const auto overheadEvaluation = projectunity::editor::evaluateViewportHlod(flatAssetBounds, overheadCamera, 1080);
    if (overheadEvaluation.insideBounds || std::abs(overheadEvaluation.distance - 39.0F) > 0.001F) {
        return fail("HLOD treated an overhead camera outside a flat asset AABB as inside the asset");
    }
    projectunity::editor::ViewportHlodReason overheadReason = projectunity::editor::ViewportHlodReason::None;
    if (!projectunity::editor::viewportRootHlodEligible(
            overheadEvaluation,
            settings,
            true,
            true,
            false,
            overheadReason)) {
        return fail("HLOD proxy did not stay eligible above a large flat asset");
    }

    TestAssetManager rockAssets;
    rockAssets.modelAsset = singleRockModel(projectunity::assets::AssetId(88000));
    projectunity::scene::Scene rockScene;
    auto& rock = rockScene.createEntity("Pitch Stable Rock");
    rock.transform.position = {0.0F, 0.0F, 40.0F};
    projectunity::scene::MeshRendererComponent rockRenderer;
    rockRenderer.modelAssetId = rockAssets.modelAsset->id;
    rock.meshRenderer = rockRenderer;
    projectunity::editor::ViewportRenderWorld rockWorld;
    std::vector<projectunity::renderer::RenderMeshDraw> rockDraws;
    frontCamera.eye = {};
    frontCamera.forward = {0.0F, 0.0F, 1.0F};
    const auto frontFrame = buildFrameWithCamera(rockWorld, rockScene, rockAssets, frontCamera, rockDraws);
    if (rockDraws.size() != 1U) {
        return fail("Front camera did not produce one rock draw");
    }
    const auto frontLod = rockDraws.front().lodIndex;
    downCamera.eye = {0.0F, 28.284271F, 11.715729F};
    downCamera.forward = {0.0F, -0.7071067F, 0.7071067F};
    downCamera.up = {0.0F, 0.7071067F, 0.7071067F};
    const auto downFrame = buildFrameWithCamera(rockWorld, rockScene, rockAssets, downCamera, rockDraws);
    if (rockDraws.size() != 1U || rockDraws.front().lodIndex != frontLod
        || frontFrame.stats.hlodMeshDrawCount != downFrame.stats.hlodMeshDrawCount) {
        return fail("Same-distance rock changed LOD/HLOD when camera pitch changed");
    }
    TestAssetManager assets;
    assets.modelAsset = heavyChunkedModel(projectunity::assets::AssetId(88001));
    projectunity::scene::Scene scene;
    auto& root = scene.createEntity("Large HLOD Source");
    projectunity::scene::MeshRendererComponent renderer;
    renderer.modelAssetId = assets.modelAsset->id;
    root.meshRenderer = renderer;

    projectunity::editor::ViewportRenderWorld world;
    std::vector<projectunity::renderer::RenderMeshDraw> draws;
    const auto frame = buildFrame(world, scene, assets, draws);
    if (frame.stats.hlodMeshDrawCount == 0U || draws.size() > 4U) {
        std::cerr << "draws=" << draws.size() << " hlod=" << frame.stats.hlodMeshDrawCount << '\n';
        return fail("Distant >8M-triangle source asset did not collapse to HLOD");
    }
    if (frame.stats.lodMeshDrawCount != 0U || frame.stats.hlodTriangleReductionCount == 0U) {
        return fail("HLOD reduction was mixed into ordinary LOD counters");
    }
    if (frame.stats.finalDrawPacketCount >= frame.stats.renderInstanceCount
        || frame.stats.finalVisibleChunkCount >= frame.stats.visibleRenderChunkCount) {
        std::cerr << "packets=" << frame.stats.finalDrawPacketCount
                  << " instances=" << frame.stats.renderInstanceCount
                  << " finalChunks=" << frame.stats.finalVisibleChunkCount
                  << " visibleChunks=" << frame.stats.visibleRenderChunkCount << '\n';
        return fail("HLOD did not reduce final draw packets or visible chunks");
    }

    TestAssetManager incompleteAssets;
    incompleteAssets.modelAsset = incompleteOverviewModel(projectunity::assets::AssetId(88002));
    projectunity::scene::Scene incompleteScene;
    auto& incompleteRoot = incompleteScene.createEntity("Incomplete HLOD Source");
    projectunity::scene::MeshRendererComponent incompleteRenderer;
    incompleteRenderer.modelAssetId = incompleteAssets.modelAsset->id;
    incompleteRoot.meshRenderer = incompleteRenderer;
    projectunity::editor::ViewportRenderWorld incompleteWorld;
    draws.clear();
    const auto incompleteFrame = buildFrame(incompleteWorld, incompleteScene, incompleteAssets, draws);
    if (incompleteFrame.stats.hlodMeshDrawCount != 0U
        || draws.size() != incompleteAssets.modelAsset->primitiveInstances.size()) {
        std::cerr << "draws=" << draws.size()
                  << " sourceInstances=" << incompleteAssets.modelAsset->primitiveInstances.size()
                  << " hlod=" << incompleteFrame.stats.hlodMeshDrawCount << '\n';
        return fail("Incomplete HLOD overview replaced source instances and hid asset parts");
    }

    projectunity::scene::Scene duplicateScene;
    auto& left = duplicateScene.createEntity("Left HLOD Source");
    left.transform.position = {-45.0F, 0.0F, 0.0F};
    left.meshRenderer = renderer;
    auto& right = duplicateScene.createEntity("Right HLOD Source");
    right.transform.position = {45.0F, 0.0F, 0.0F};
    right.meshRenderer = renderer;
    projectunity::editor::ViewportRenderWorld duplicateWorld;
    draws.clear();
    (void)buildFrame(duplicateWorld, duplicateScene, assets, draws);
    if (draws.size() != 2U || draws[0].renderInstanceId == draws[1].renderInstanceId) {
        return fail("Duplicated HLOD roots shared draw identity");
    }
    const auto x0 = draws[0].worldBoundsCenter[0];
    const auto x1 = draws[1].worldBoundsCenter[0];
    if (std::abs(x0 - x1) < 40.0F) {
        return fail("Duplicated HLOD roots did not keep independent transforms");
    }

    std::vector<projectunity::renderer::RenderLight> lights {
        projectunity::renderer::RenderLight {},
    };
    const auto shadowSelection = projectunity::renderer::chooseShadowMap(lights, {0.0F, 0.0F, 90.0F}, 180.0F);
    std::vector<projectunity::renderer::RenderMeshDraw> shadowDraws;
    projectunity::editor::ViewportRenderWorldStats shadowStats;
    world.collectShadowCasters(shadowSelection, &lights.front(), overviewCamera(), 1080, settings, {}, shadowDraws, shadowStats);
    if (shadowStats.shadowHlodProxyDrawCount == 0U || shadowDraws.size() > 4U) {
        std::cerr << "shadowDraws=" << shadowDraws.size() << " proxy=" << shadowStats.shadowHlodProxyDrawCount << '\n';
        return fail("Distant shadow casters did not use HLOD proxy draws");
    }

    return EXIT_SUCCESS;
}
