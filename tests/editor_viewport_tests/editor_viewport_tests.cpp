#include "ViewportMeshLod.hpp"
#include "ViewportRenderWorld.hpp"
#include "ViewportRenderWorldOcclusion.hpp"
#include "ViewportRenderWorldOcclusionPolicy.hpp"
#include "ViewportShadowFocus.hpp"

#include <projectunity/assets/AssetManager.hpp>
#include <projectunity/renderer/RenderShadowSetup.hpp>
#include <projectunity/scene/Scene.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

int fail(const char* message)
{
    std::cerr << message << '\n';
    return EXIT_FAILURE;
}

projectunity::assets::MeshPrimitive testPrimitive()
{
    projectunity::assets::MeshPrimitive primitive;
    primitive.indices.resize(3'000U);
    primitive.bounds.radius = 10.0F;
    primitive.lods.push_back({std::vector<std::uint32_t>(1'500U), 0.01F});
    primitive.lods.push_back({std::vector<std::uint32_t>(750U), 0.05F});
    primitive.lods.push_back({std::vector<std::uint32_t>(300U), 0.20F});
    return primitive;
}

projectunity::editor::ViewportWorldBounds testBounds(
    projectunity::math::Vec3 minimum,
    projectunity::math::Vec3 maximum)
{
    projectunity::assets::MeshBounds bounds;
    bounds.minimum = minimum;
    bounds.maximum = maximum;
    bounds.center = (minimum + maximum) * 0.5F;
    bounds.radius = (maximum - bounds.center).length();
    return projectunity::editor::transformViewportBounds({}, bounds);
}

struct TestOccluderInstance {
    std::uint32_t primitiveIndex {0};
    std::shared_ptr<const projectunity::assets::ModelAsset> model;
    projectunity::editor::ViewportWorldBounds worldBounds;
    projectunity::scene::EntityId sceneNodeId;
    projectunity::assets::AssetId modelAssetId;
    std::uint32_t primitiveInstanceIndex {UINT32_MAX};
};

struct TestOccluderChunk {
    projectunity::editor::ViewportWorldBounds worldBounds;
    std::vector<std::size_t> instanceIndices;
    std::uint64_t triangleCount {0};
    projectunity::scene::EntityId sceneNodeId;
    std::uint64_t renderChunkId {0};
};

struct TestOccluderRecord {
    std::vector<TestOccluderInstance> instances;
};

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

std::shared_ptr<projectunity::assets::ModelAsset> testOccluderModel()
{
    auto model = std::make_shared<projectunity::assets::ModelAsset>();
    model->materials.resize(1U);
    model->primitives.resize(1U);
    model->primitives.front().materialIndex = 0U;
    model->primitives.front().indices.resize(36'000U);
    return model;
}

std::shared_ptr<projectunity::assets::ModelAsset> testShadowCasterModel()
{
    auto model = std::make_shared<projectunity::assets::ModelAsset>();
    model->id = projectunity::assets::AssetId(991);
    model->materials.resize(1U);
    projectunity::assets::MeshPrimitive primitive;
    primitive.vertices.resize(3U);
    primitive.indices = {0U, 1U, 2U};
    primitive.bounds.minimum = {-1.0F, -1.0F, -1.0F};
    primitive.bounds.maximum = {1.0F, 1.0F, 1.0F};
    primitive.bounds.center = {0.0F, 0.0F, 0.0F};
    primitive.bounds.radius = 1.8F;
    model->primitives.push_back(std::move(primitive));
    return model;
}

std::shared_ptr<projectunity::assets::ModelAsset> testRuntimeProxyModel()
{
    auto model = std::make_shared<projectunity::assets::ModelAsset>();
    model->id = projectunity::assets::AssetId(774);
    model->materials.resize(1U);
    projectunity::assets::MeshPrimitive primitive;
    primitive.vertices.resize(3U);
    primitive.indices = {0U, 1U, 2U};
    primitive.bounds.minimum = {-1.0F, -1.0F, -1.0F};
    primitive.bounds.maximum = {1.0F, 1.0F, 1.0F};
    primitive.bounds.center = {0.0F, 0.0F, 0.0F};
    primitive.bounds.radius = 1.8F;
    model->primitives.push_back(std::move(primitive));

    projectunity::assets::MeshPrimitiveInstance instance;
    instance.primitiveIndex = 0U;
    instance.transform[12] = 0.0F;
    instance.transform[13] = 0.0F;
    instance.transform[14] = 10.0F;
    instance.bounds.minimum = {-1.0F, -1.0F, 9.0F};
    instance.bounds.maximum = {1.0F, 1.0F, 11.0F};
    instance.bounds.center = {0.0F, 0.0F, 10.0F};
    instance.bounds.radius = 1.8F;
    model->primitiveInstances.push_back(instance);
    return model;
}

std::shared_ptr<projectunity::assets::ModelAsset> testNoLodRockFieldModel()
{
    auto model = std::make_shared<projectunity::assets::ModelAsset>();
    model->id = projectunity::assets::AssetId(1774);
    model->materials.resize(1U);
    projectunity::assets::MeshPrimitive primitive;
    primitive.materialIndex = 0U;
    primitive.bounds.minimum = {-0.5F, -0.5F, -0.5F};
    primitive.bounds.maximum = {0.5F, 0.5F, 0.5F};
    primitive.bounds.center = {0.0F, 0.0F, 0.0F};
    primitive.bounds.radius = 0.8661F;
    for (std::uint32_t triangle = 0; triangle < 120U; ++triangle) {
        const auto base = static_cast<std::uint32_t>(primitive.vertices.size());
        const auto offset = static_cast<float>(triangle % 8U) * 0.02F;
        primitive.vertices.push_back({{-0.35F + offset, -0.25F, -0.35F}});
        primitive.vertices.push_back({{0.35F, -0.25F + offset, -0.25F}});
        primitive.vertices.push_back({{-0.25F, 0.35F, 0.35F - offset}});
        primitive.indices.push_back(base);
        primitive.indices.push_back(base + 1U);
        primitive.indices.push_back(base + 2U);
    }
    model->primitives.push_back(std::move(primitive));
    for (std::uint32_t index = 0; index < 640U; ++index) {
        projectunity::assets::MeshPrimitiveInstance instance;
        instance.primitiveIndex = 0U;
        instance.transform[12] = -39.0F + static_cast<float>(index % 40U) * 2.0F;
        instance.transform[13] = 0.0F;
        instance.transform[14] = 80.0F + static_cast<float>(index / 40U) * 2.0F;
        instance.bounds = model->primitives.front().bounds;
        model->primitiveInstances.push_back(instance);
    }
    for (std::uint32_t clusterY = 0; clusterY < 4U; ++clusterY) {
        for (std::uint32_t clusterX = 0; clusterX < 8U; ++clusterX) {
            projectunity::assets::MeshPrimitiveCluster cluster;
            auto initialized = false;
            for (std::uint32_t localY = 0; localY < 4U; ++localY) {
                for (std::uint32_t localX = 0; localX < 5U; ++localX) {
                    const auto column = clusterX * 5U + localX;
                    const auto row = clusterY * 4U + localY;
                    const auto instanceIndex = row * 40U + column;
                    if (instanceIndex >= model->primitiveInstances.size()) {
                        continue;
                    }
                    cluster.primitiveInstanceIndices.push_back(instanceIndex);
                    const auto& instance = model->primitiveInstances[instanceIndex];
                    const auto offset = projectunity::math::Vec3 {
                        instance.transform[12],
                        instance.transform[13],
                        instance.transform[14],
                    };
                    const projectunity::assets::MeshBounds bounds {
                        model->primitives.front().bounds.minimum + offset,
                        model->primitives.front().bounds.maximum + offset,
                        model->primitives.front().bounds.center + offset,
                        model->primitives.front().bounds.radius,
                    };
                    if (!initialized) {
                        cluster.bounds = bounds;
                        initialized = true;
                    } else {
                        cluster.bounds.minimum.x = std::min(cluster.bounds.minimum.x, bounds.minimum.x);
                        cluster.bounds.minimum.y = std::min(cluster.bounds.minimum.y, bounds.minimum.y);
                        cluster.bounds.minimum.z = std::min(cluster.bounds.minimum.z, bounds.minimum.z);
                        cluster.bounds.maximum.x = std::max(cluster.bounds.maximum.x, bounds.maximum.x);
                        cluster.bounds.maximum.y = std::max(cluster.bounds.maximum.y, bounds.maximum.y);
                        cluster.bounds.maximum.z = std::max(cluster.bounds.maximum.z, bounds.maximum.z);
                    }
                }
            }
            if (!cluster.primitiveInstanceIndices.empty()) {
                cluster.bounds.center = (cluster.bounds.minimum + cluster.bounds.maximum) * 0.5F;
                cluster.bounds.radius = (cluster.bounds.maximum - cluster.bounds.center).length();
                model->primitiveClusters.push_back(std::move(cluster));
            }
        }
    }
    return model;
}

} // namespace

int main()
{
    using projectunity::editor::indexCountForViewportLod;
    using projectunity::editor::selectViewportMeshLod;
    using projectunity::editor::viewportLodDistanceToBounds;

    const auto primitive = testPrimitive();
    constexpr auto fov = 1.04719755F;
    constexpr auto viewportHeight = 1080.0F;

    const std::array<projectunity::math::Vec3, 8> flatTerrainCorners {{
        {-100.0F, -1.0F, 100.0F},
        {100.0F, -1.0F, 100.0F},
        {-100.0F, 1.0F, 100.0F},
        {100.0F, 1.0F, 100.0F},
        {-100.0F, -1.0F, 200.0F},
        {100.0F, -1.0F, 200.0F},
        {-100.0F, 1.0F, 200.0F},
        {100.0F, 1.0F, 200.0F},
    }};
    const auto flatTerrainDistance = viewportLodDistanceToBounds(
        {0.0F, 0.0F, 0.0F},
        {0.0F, 0.0F, 1.0F},
        flatTerrainCorners,
        0.05F);
    if (std::abs(flatTerrainDistance - 100.0F) > 0.001F) {
        return fail("Viewport LOD distance treated a distant flat terrain bound as nearby");
    }
    const std::array<projectunity::math::Vec3, 8> surroundingCorners {{
        {-10.0F, -10.0F, -10.0F},
        {10.0F, -10.0F, -10.0F},
        {-10.0F, 10.0F, -10.0F},
        {10.0F, 10.0F, -10.0F},
        {-10.0F, -10.0F, 10.0F},
        {10.0F, -10.0F, 10.0F},
        {-10.0F, 10.0F, 10.0F},
        {10.0F, 10.0F, 10.0F},
    }};
    const auto insideBoundsDistance = viewportLodDistanceToBounds(
        {0.0F, 0.0F, 0.0F},
        {0.0F, 0.0F, 1.0F},
        surroundingCorners,
        0.05F);
    if (std::abs(insideBoundsDistance - 10.0F) > 0.001F) {
        return fail("Viewport LOD distance forced an enclosing bound to near-plane distance");
    }
    if (selectViewportMeshLod(primitive, 20.0F, insideBoundsDistance, fov, viewportHeight, false) != 0U) {
        return fail("Viewport LOD did not preserve full detail inside a close bound");
    }

    if (selectViewportMeshLod(primitive, 20.0F, 100.0F, fov, viewportHeight, false) != 2U) {
        return fail("Viewport LOD did not use a low-error coarse LOD for a moderately distant mesh");
    }
    if (selectViewportMeshLod(primitive, 20.0F, 500.0F, fov, viewportHeight, false) != 3U) {
        return fail("Viewport LOD did not choose a coarser sub-pixel LOD at greater distance");
    }
    if (selectViewportMeshLod(primitive, 20.0F, 1'200.0F, fov, viewportHeight, false) != 3U) {
        return fail("Viewport LOD did not choose the coarsest valid sub-pixel LOD when far away");
    }
    if (selectViewportMeshLod(primitive, 20.0F, 500.0F, fov, viewportHeight, true) != 0U) {
        return fail("Viewport LOD changed a force-full-resolution mesh");
    }
    if (selectViewportMeshLod(primitive, 20.0F, 5.0F, fov, viewportHeight, false) != 0U) {
        return fail("Viewport LOD simplified a mesh at a very close screen size");
    }
    if (indexCountForViewportLod(primitive, 2U) != 750U
        || indexCountForViewportLod(primitive, 99U) != primitive.indices.size()) {
        return fail("Viewport LOD index count lookup returned the wrong index buffer size");
    }

    const projectunity::editor::ViewportRenderWorldCamera camera {
        {0.0F, 0.0F, 0.0F},
        {1.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F},
        {0.0F, 0.0F, 1.0F},
        1.57079637F,
        1.0F,
        0.05F,
        100.0F,
    };

    TestAssetManager runtimeProxyAssets;
    runtimeProxyAssets.modelAsset = testRuntimeProxyModel();
    projectunity::scene::Scene runtimeProxyScene;
    auto& runtimeRoot = runtimeProxyScene.createEntity("Runtime Asset");
    projectunity::scene::MeshRendererComponent runtimeRootRenderer;
    runtimeRootRenderer.modelAssetId = runtimeProxyAssets.modelAsset->id;
    runtimeRoot.meshRenderer = runtimeRootRenderer;
    auto& movedProxy = runtimeProxyScene.createEntity("Moved Runtime Primitive", runtimeRoot.id);
    movedProxy.transform.position = {5.0F, 0.0F, 10.0F};
    projectunity::scene::MeshRendererComponent movedProxyRenderer;
    movedProxyRenderer.modelAssetId = runtimeProxyAssets.modelAsset->id;
    movedProxyRenderer.primitiveInstanceIndex = 0U;
    movedProxyRenderer.renderable = false;
    movedProxy.meshRenderer = movedProxyRenderer;

    projectunity::editor::ViewportRenderWorld runtimeProxyWorld;
    std::vector<projectunity::renderer::RenderMeshDraw> runtimeProxyDraws;
    std::vector<projectunity::renderer::RenderLight> runtimeProxyLights;
    (void)runtimeProxyWorld.buildFrame(
        &runtimeProxyScene,
        &runtimeProxyAssets,
        {},
        camera,
        {},
        1080,
        true,
        runtimeProxyDraws,
        runtimeProxyLights);
    if (runtimeProxyDraws.size() != 1U) {
        return fail("Runtime snapshot did not render the asset primitive through its root entity");
    }
    if (runtimeProxyDraws.front().sceneNodeId != movedProxy.id.value()) {
        return fail("Runtime snapshot did not bind the moved non-renderable proxy as the primitive scene node");
    }
    if (std::fabs(runtimeProxyDraws.front().worldBoundsCenter[0] - 5.0F) > 0.001F
        || std::fabs(runtimeProxyDraws.front().worldBoundsCenter[2] - 10.0F) > 0.001F) {
        return fail("Runtime snapshot ignored the moved primitive proxy transform");
    }

    TestAssetManager noLodRockFieldAssets;
    noLodRockFieldAssets.modelAsset = testNoLodRockFieldModel();
    projectunity::scene::Scene noLodRockFieldScene;
    auto& noLodRockFieldRoot = noLodRockFieldScene.createEntity("No LOD Rock Field");
    projectunity::scene::MeshRendererComponent noLodRockFieldRenderer;
    noLodRockFieldRenderer.modelAssetId = noLodRockFieldAssets.modelAsset->id;
    noLodRockFieldRoot.meshRenderer = noLodRockFieldRenderer;
    auto overviewCamera = camera;
    overviewCamera.eye = {0.0F, 0.0F, -160.0F};
    overviewCamera.farPlane = 4000.0F;
    projectunity::editor::ViewportRenderWorld noLodRockFieldWorld;
    std::vector<projectunity::renderer::RenderMeshDraw> noLodRockFieldDraws;
    std::vector<projectunity::renderer::RenderLight> noLodRockFieldLights;
    const auto noLodRockFieldFrame = noLodRockFieldWorld.buildFrame(
        &noLodRockFieldScene,
        &noLodRockFieldAssets,
        {},
        overviewCamera,
        {},
        1080,
        false,
        noLodRockFieldDraws,
        noLodRockFieldLights);
    if (noLodRockFieldFrame.stats.hlodMeshDrawCount == 0U || noLodRockFieldDraws.size() > 8U) {
        std::cerr << "noLodRockFieldDraws=" << noLodRockFieldDraws.size()
                  << " hlod=" << noLodRockFieldFrame.stats.hlodMeshDrawCount << '\n';
        return fail("Viewport overview did not merge a distant no-LOD rock field");
    }
    if ((noLodRockFieldDraws.front().primitiveIndex & 0x80000000U) == 0U) {
        return fail("Viewport overview did not tag the no-LOD rock field as HLOD");
    }
    overviewCamera.eye = {0.0F, 0.0F, 65.0F};
    projectunity::editor::ViewportRenderWorld mixedRockFieldWorld;
    noLodRockFieldDraws.clear();
    noLodRockFieldLights.clear();
    (void)mixedRockFieldWorld.buildFrame(
        &noLodRockFieldScene,
        &noLodRockFieldAssets,
        {},
        overviewCamera,
        {},
        1080,
        false,
        noLodRockFieldDraws,
        noLodRockFieldLights);
    auto hasMixedHlod = false;
    auto hasMixedDirect = false;
    for (const auto& draw : noLodRockFieldDraws) {
        if ((draw.primitiveIndex & 0x80000000U) != 0U) {
            hasMixedHlod = true;
        } else {
            hasMixedDirect = true;
        }
    }
    if (!hasMixedHlod || !hasMixedDirect) {
        std::cerr << "mixedRockFieldDraws=" << noLodRockFieldDraws.size()
                  << " hasHlod=" << hasMixedHlod
                  << " hasDirect=" << hasMixedDirect << '\n';
        return fail("Viewport overview did not mix far cluster HLOD with nearby direct rock draws");
    }

    projectunity::editor::ViewportOcclusionBuffer occlusion(camera, 1080);
    if (!occlusion.addOccluder(testBounds({-2.0F, -2.0F, 4.8F}, {2.0F, 2.0F, 5.2F}))) {
        return fail("Viewport occlusion buffer did not accept a large opaque occluder");
    }
    if (!occlusion.isOccluded(testBounds({-0.5F, -0.5F, 9.8F}, {0.5F, 0.5F, 10.2F}))) {
        return fail("Viewport occlusion did not reject a fully covered chunk behind an occluder");
    }
    if (occlusion.isOccluded(testBounds({1.6F, -0.5F, 9.8F}, {3.0F, 0.5F, 10.2F}))) {
        return fail("Viewport occlusion rejected a partially protruding chunk");
    }
    if (occlusion.isOccluded(testBounds({-0.5F, -0.5F, 2.8F}, {0.5F, 0.5F, 3.2F}))) {
        return fail("Viewport occlusion rejected a chunk in front of the occluder");
    }
    if (occlusion.isOccluded(testBounds({-0.4F, -0.4F, 5.8F}, {0.4F, 0.4F, 6.2F}))) {
        return fail("Viewport occlusion rejected near-camera content");
    }

    projectunity::editor::ViewportOcclusionBuffer nearOcclusion(camera, 1080);
    if (nearOcclusion.addOccluder(testBounds({-3.0F, -3.0F, -0.2F}, {3.0F, 3.0F, 1.2F}))) {
        return fail("Viewport occlusion accepted an occluder crossing the near plane");
    }
    if (nearOcclusion.isOccluded(testBounds({-0.4F, -2.8F, 4.8F}, {0.4F, -2.0F, 5.2F}))) {
        return fail("Viewport near-plane occlusion rejected visible content below the camera");
    }

    const auto occluderModel = testOccluderModel();
    TestOccluderRecord denseRecord;
    denseRecord.instances = {
        {0U, occluderModel, testBounds({-2.0F, -2.0F, 4.8F}, {0.0F, 0.0F, 5.2F})},
        {0U, occluderModel, testBounds({0.0F, -2.0F, 4.8F}, {2.0F, 0.0F, 5.2F})},
        {0U, occluderModel, testBounds({-2.0F, 0.0F, 4.8F}, {0.0F, 2.0F, 5.2F})},
        {0U, occluderModel, testBounds({0.0F, 0.0F, 4.8F}, {2.0F, 2.0F, 5.2F})},
    };
    const TestOccluderChunk denseChunk {
        testBounds({-2.0F, -2.0F, 4.8F}, {2.0F, 2.0F, 5.2F}),
        {0U, 1U, 2U, 3U},
        48'000U,
    };
    if (!projectunity::editor::viewportChunkCanOcclude(denseRecord, denseChunk, 20.0F)) {
        return fail("Viewport occlusion rejected a dense multi-instance occluder");
    }
    auto maskedOccluderModel = testOccluderModel();
    maskedOccluderModel->materials.front().alphaMode = projectunity::assets::MaterialAlphaMode::Mask;
    TestOccluderRecord maskedRecord;
    maskedRecord.instances = {
        {0U, maskedOccluderModel, testBounds({-2.0F, -2.0F, 4.8F}, {0.0F, 0.0F, 5.2F})},
        {0U, maskedOccluderModel, testBounds({0.0F, -2.0F, 4.8F}, {2.0F, 0.0F, 5.2F})},
        {0U, maskedOccluderModel, testBounds({-2.0F, 0.0F, 4.8F}, {0.0F, 2.0F, 5.2F})},
        {0U, maskedOccluderModel, testBounds({0.0F, 0.0F, 4.8F}, {2.0F, 2.0F, 5.2F})},
    };
    if (projectunity::editor::viewportChunkCanOcclude(maskedRecord, denseChunk, 20.0F)) {
        return fail("Viewport occlusion accepted an alpha-mask chunk as a solid occluder");
    }
    auto maskedCandidateChunk = denseChunk;
    maskedCandidateChunk.worldBounds = testBounds({-0.5F, -0.5F, 9.8F}, {0.5F, 0.5F, 10.2F});
    maskedCandidateChunk.renderChunkId = 99U;
    projectunity::editor::ViewportRenderWorldStats maskedStats;
    std::unordered_set<std::uint64_t> emptyOccluderIds;
    if (projectunity::editor::viewportChunkRejectedByOcclusion(
            maskedRecord,
            maskedCandidateChunk,
            {},
            {},
            0U,
            occlusion,
            emptyOccluderIds,
            maskedStats)) {
        return fail("Viewport occlusion rejected an alpha-mask chunk that must stay conservative");
    }

    TestOccluderRecord sparseRecord;
    sparseRecord.instances = {
        {0U, occluderModel, testBounds({-10.0F, -0.5F, 5.0F}, {-9.0F, 0.5F, 6.0F})},
        {0U, occluderModel, testBounds({-4.0F, -0.5F, 5.0F}, {-3.0F, 0.5F, 6.0F})},
        {0U, occluderModel, testBounds({3.0F, -0.5F, 5.0F}, {4.0F, 0.5F, 6.0F})},
        {0U, occluderModel, testBounds({9.0F, -0.5F, 5.0F}, {10.0F, 0.5F, 6.0F})},
        {0U, occluderModel, testBounds({-10.0F, -0.5F, 24.0F}, {-9.0F, 0.5F, 25.0F})},
        {0U, occluderModel, testBounds({-4.0F, -0.5F, 24.0F}, {-3.0F, 0.5F, 25.0F})},
        {0U, occluderModel, testBounds({3.0F, -0.5F, 24.0F}, {4.0F, 0.5F, 25.0F})},
        {0U, occluderModel, testBounds({9.0F, -0.5F, 24.0F}, {10.0F, 0.5F, 25.0F})},
    };
    const TestOccluderChunk sparseChunk {
        testBounds({-10.0F, -0.5F, 5.0F}, {10.0F, 0.5F, 25.0F}),
        {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U},
        96'000U,
    };
    if (projectunity::editor::viewportChunkCanOcclude(sparseRecord, sparseChunk, 40.0F)) {
        return fail("Viewport occlusion accepted a sparse multi-instance rock field as a solid occluder");
    }

    const auto sceneShadowFocusForward = projectunity::editor::stableViewportShadowFocus(
        projectunity::editor::ViewportMode::Scene,
        {0.0F, 12.0F, -18.0F},
        {0.0F, -0.2F, 1.0F},
        {0.0F, 0.0F, 0.0F},
        24.0F,
        4000.0F,
        true,
        600.0F);
    const auto sceneShadowFocusUp = projectunity::editor::stableViewportShadowFocus(
        projectunity::editor::ViewportMode::Scene,
        {0.0F, 12.0F, -18.0F},
        {0.0F, 0.72F, 0.69F},
        {0.0F, 0.0F, 0.0F},
        24.0F,
        4000.0F,
        true,
        600.0F);
    if ((sceneShadowFocusForward.center - sceneShadowFocusUp.center).length() > 0.001F
        || std::fabs(sceneShadowFocusForward.radius - sceneShadowFocusUp.radius) > 0.001F) {
        return fail("Viewport shadow focus moved when only the Scene View pitch changed");
    }
    if (sceneShadowFocusForward.radius > 220.0F) {
        return fail("Viewport shadow focus accepted an oversized visible bounds radius");
    }

    TestAssetManager shadowAssets;
    shadowAssets.modelAsset = testShadowCasterModel();
    projectunity::scene::Scene shadowScene;
    auto& offscreenCaster = shadowScene.createEntity("Offscreen Caster");
    offscreenCaster.transform.position = {0.0F, 20.0F, 10.0F};
    projectunity::scene::MeshRendererComponent meshRenderer;
    meshRenderer.modelAssetId = shadowAssets.modelAsset->id;
    offscreenCaster.meshRenderer = meshRenderer;
    projectunity::editor::ViewportRenderWorld shadowWorld;
    std::vector<projectunity::renderer::RenderMeshDraw> colorDraws;
    std::vector<projectunity::renderer::RenderMeshDraw> shadowDraws;
    std::vector<projectunity::renderer::RenderLight> sceneLights;
    (void)shadowWorld.buildFrame(
        &shadowScene,
        &shadowAssets,
        {},
        camera,
        {},
        1080,
        false,
        colorDraws,
        sceneLights);
    if (!colorDraws.empty()) {
        return fail("Viewport color culling rendered an offscreen shadow caster");
    }
    projectunity::renderer::RenderLight sun;
    sun.type = projectunity::renderer::RenderLightType::Directional;
    sun.direction = {0.0F, -1.0F, 0.0F};
    const std::array<projectunity::renderer::RenderLight, 1> sunLights {sun};
    const auto shadowSelection = projectunity::renderer::chooseShadowMap(sunLights, {0.0F, 0.0F, 10.0F}, 40.0F);
    projectunity::editor::ViewportRenderWorldStats shadowCasterStats;
    shadowWorld.collectShadowCasters(
        shadowSelection,
        &sun,
        camera,
        1080,
        {},
        shadowDraws,
        shadowCasterStats);
    if (shadowDraws.empty() || shadowCasterStats.shadowCandidateInstances == 0U) {
        return fail("Viewport shadow caster collection ignored an offscreen caster whose projected shadow reaches the camera");
    }
    if (shadowDraws.front().renderChunkId == 0U) {
        return fail("Viewport shadow caster collection did not preserve the caster render chunk id");
    }
    if (shadowCasterStats.shadowOnlyCandidateInstances == 0U || shadowCasterStats.shadowOnlyRejectedInstances != 0U) {
        return fail("Viewport shadow caster collection did not track accepted offscreen shadow casters");
    }

    projectunity::scene::Scene rejectedShadowScene;
    auto& rejectedCaster = rejectedShadowScene.createEntity("Rejected Offscreen Caster");
    rejectedCaster.transform.position = {20.0F, 0.0F, 10.0F};
    rejectedCaster.meshRenderer = meshRenderer;
    projectunity::editor::ViewportRenderWorld rejectedShadowWorld;
    std::vector<projectunity::renderer::RenderMeshDraw> rejectedColorDraws;
    std::vector<projectunity::renderer::RenderMeshDraw> rejectedShadowDraws;
    std::vector<projectunity::renderer::RenderLight> rejectedLights;
    (void)rejectedShadowWorld.buildFrame(&rejectedShadowScene, &shadowAssets, {}, camera, {}, 1080, false, rejectedColorDraws, rejectedLights);
    projectunity::editor::ViewportRenderWorldStats rejectedShadowStats;
    rejectedShadowWorld.collectShadowCasters(shadowSelection, &sun, camera, 1080, {}, rejectedShadowDraws, rejectedShadowStats);
    if (!rejectedShadowDraws.empty() || rejectedShadowStats.shadowOnlyRejectedInstances == 0U) {
        return fail("Viewport shadow caster collection kept an offscreen caster whose projected shadow misses the camera");
    }

    projectunity::scene::Scene shadowBudgetScene;
    auto& importantCaster = shadowBudgetScene.createEntity("Important Shadow Caster");
    importantCaster.transform.position = {0.0F, 20.0F, 10.0F};
    importantCaster.transform.scale = {8.0F, 8.0F, 8.0F};
    importantCaster.meshRenderer = meshRenderer;
    const auto importantCasterId = importantCaster.id.value();
    for (int index = 0; index < 620; ++index) {
        auto& filler = shadowBudgetScene.createEntity("Shadow Budget Filler");
        filler.transform.position = {
            -28.0F + static_cast<float>(index % 29) * 2.0F,
            10.0F,
            8.0F + static_cast<float>(index / 29) * 2.0F,
        };
        filler.meshRenderer = meshRenderer;
    }
    projectunity::editor::ViewportRenderWorld shadowBudgetWorld;
    std::vector<projectunity::renderer::RenderMeshDraw> shadowBudgetColorDraws;
    std::vector<projectunity::renderer::RenderLight> shadowBudgetLights;
    (void)shadowBudgetWorld.buildFrame(
        &shadowBudgetScene,
        &shadowAssets,
        {},
        camera,
        {},
        1080,
        false,
        shadowBudgetColorDraws,
        shadowBudgetLights);
    const auto shadowBudgetSelection = projectunity::renderer::chooseShadowMap(sunLights, {0.0F, 0.0F, 18.0F}, 120.0F);
    auto upwardCamera = camera;
    upwardCamera.forward = {0.0F, 1.0F, 0.0F};
    upwardCamera.up = {0.0F, 0.0F, -1.0F};
    std::vector<projectunity::renderer::RenderMeshDraw> forwardShadowDraws;
    std::vector<projectunity::renderer::RenderMeshDraw> upwardShadowDraws;
    projectunity::editor::ViewportRenderWorldStats forwardShadowStats;
    projectunity::editor::ViewportRenderWorldStats upwardShadowStats;
    shadowBudgetWorld.collectShadowCasters(
        shadowBudgetSelection,
        &sun,
        camera,
        1080,
        {},
        forwardShadowDraws,
        forwardShadowStats);
    shadowBudgetWorld.collectShadowCasters(
        shadowBudgetSelection,
        &sun,
        upwardCamera,
        1080,
        {},
        upwardShadowDraws,
        upwardShadowStats);
    const auto importantCastsShadow = [importantCasterId](
        const std::vector<projectunity::renderer::RenderMeshDraw>& draws) {
        for (const auto& draw : draws) {
            if (draw.sceneNodeId == importantCasterId && draw.castsShadow) {
                return true;
            }
        }
        return false;
    };
    if (!importantCastsShadow(forwardShadowDraws) || !importantCastsShadow(upwardShadowDraws)) {
        return fail("Viewport shadow budget changed an important caster when only camera direction changed");
    }

    return EXIT_SUCCESS;
}
