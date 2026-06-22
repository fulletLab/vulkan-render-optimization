#include "ViewportMeshLod.hpp"
#include "ViewportRenderWorld.hpp"
#include "ViewportRenderWorldOcclusion.hpp"
#include "ViewportRenderWorldOcclusionPolicy.hpp"
#include "ViewportRendererCulling.hpp"
#include "ViewportShadowFocus.hpp"

#include <projectunity/assets/AssetManager.hpp>
#include <projectunity/renderer/RenderShadowSetup.hpp>
#include <projectunity/scene/Scene.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
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

projectunity::assets::MeshPrimitive testBoundedLodPrimitive()
{
    auto primitive = testPrimitive();
    primitive.materialIndex = 0U;
    primitive.bounds.minimum = {-10.0F, -10.0F, -10.0F};
    primitive.bounds.maximum = {10.0F, 10.0F, 10.0F};
    primitive.bounds.center = {0.0F, 0.0F, 0.0F};
    primitive.bounds.radius = 17.3206F;
    return primitive;
}

projectunity::assets::MeshBounds translatedBounds(
    const projectunity::assets::MeshBounds& bounds,
    projectunity::math::Vec3 offset)
{
    return {
        bounds.minimum + offset,
        bounds.maximum + offset,
        bounds.center + offset,
        bounds.radius,
    };
}

std::shared_ptr<projectunity::assets::ModelAsset> testSinglePrimitiveLodModel(projectunity::assets::AssetId id)
{
    auto model = std::make_shared<projectunity::assets::ModelAsset>();
    model->id = id;
    model->materials.resize(1U);
    model->primitives.push_back(testBoundedLodPrimitive());
    return model;
}

std::shared_ptr<projectunity::assets::ModelAsset> testBroadPrimitiveLodModel(projectunity::assets::AssetId id)
{
    auto model = std::make_shared<projectunity::assets::ModelAsset>();
    model->id = id;
    model->materials.resize(1U);
    projectunity::assets::MeshPrimitive primitive;
    primitive.materialIndex = 0U;
    primitive.bounds.minimum = {-500.0F, -1.0F, 100.0F};
    primitive.bounds.maximum = {500.0F, 1.0F, 200.0F};
    primitive.bounds.center = {0.0F, 0.0F, 150.0F};
    primitive.bounds.radius = (primitive.bounds.maximum - primitive.bounds.center).length();
    primitive.indices.resize(3'000U);
    primitive.lods.push_back({std::vector<std::uint32_t>(1'500U), 0.10F});
    primitive.lods.push_back({std::vector<std::uint32_t>(750U), 0.50F});
    primitive.lods.push_back({std::vector<std::uint32_t>(300U), 1.00F});
    model->primitives.push_back(std::move(primitive));
    return model;
}

std::shared_ptr<projectunity::assets::ModelAsset> testChunkedLodFieldModel()
{
    auto model = testSinglePrimitiveLodModel(projectunity::assets::AssetId(2774));
    const std::array<projectunity::math::Vec3, 4> offsets {{
        {0.0F, 0.0F, -40.0F},
        {0.0F, 0.0F, 4.0F},
        {0.0F, 0.0F, 140.0F},
        {0.0F, 0.0F, 620.0F},
    }};
    for (const auto offset : offsets) {
        projectunity::assets::MeshPrimitiveInstance instance;
        instance.primitiveIndex = 0U;
        instance.transform[12] = offset.x;
        instance.transform[13] = offset.y;
        instance.transform[14] = offset.z;
        instance.bounds = translatedBounds(model->primitives.front().bounds, offset);
        model->primitiveInstances.push_back(instance);
    }
    return model;
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

int runViewportAssetProbe(const std::filesystem::path& sourcePath)
{
    using projectunity::math::Vec3;

    projectunity::assets::AssetManager manager(std::filesystem::current_path() / "Cache" / "Assets");
    const auto result = manager.importModel(sourcePath, [](const projectunity::assets::AssetImportProgress& progress) {
        if (progress.percent == 100 || progress.percent % 20 == 0) {
            std::cout << progress.percent << "% " << progress.stage << '\n';
        }
    });
    if (!result.success) {
        std::cerr << result.error << '\n';
        return EXIT_FAILURE;
    }
    const auto model = manager.model(result.record.id);
    if (model == nullptr || model->primitiveInstances.empty()) {
        return fail("Viewport asset probe could not load model instances");
    }

    auto minimum = model->primitiveInstances.front().bounds.minimum;
    auto maximum = model->primitiveInstances.front().bounds.maximum;
    std::uint64_t doubleSidedMaterials = 0;
    for (const auto& material : model->materials) {
        if (material.doubleSided) {
            ++doubleSidedMaterials;
        }
    }
    for (const auto& instance : model->primitiveInstances) {
        minimum.x = std::min(minimum.x, instance.bounds.minimum.x);
        minimum.y = std::min(minimum.y, instance.bounds.minimum.y);
        minimum.z = std::min(minimum.z, instance.bounds.minimum.z);
        maximum.x = std::max(maximum.x, instance.bounds.maximum.x);
        maximum.y = std::max(maximum.y, instance.bounds.maximum.y);
        maximum.z = std::max(maximum.z, instance.bounds.maximum.z);
    }
    const auto center = (minimum + maximum) * 0.5F;
    const auto extent = maximum - minimum;
    const auto radius = extent.length() * 0.5F;
    std::cout << "model=" << model->name
              << " id=" << result.record.id.value()
              << " primitives=" << model->primitives.size()
              << " instances=" << model->primitiveInstances.size()
              << " clusters=" << model->primitiveClusters.size()
              << " materials=" << model->materials.size()
              << " doubleSided=" << doubleSidedMaterials
              << " boundsMin=(" << minimum.x << "," << minimum.y << "," << minimum.z << ")"
              << " boundsMax=(" << maximum.x << "," << maximum.y << "," << maximum.z << ")"
              << '\n';

    projectunity::scene::Scene scene;
    auto& entity = scene.createEntity("Probe Asset");
    projectunity::scene::MeshRendererComponent renderer;
    renderer.modelAssetId = result.record.id;
    entity.meshRenderer = renderer;
    projectunity::editor::ViewportRenderWorld world;
    const auto lodSettings = projectunity::editor::viewportAssetLodSettingsFromEnvironment();

    const auto runCamera = [&](const char* label, Vec3 eye, Vec3 forward, Vec3 upHint) {
        forward = forward.normalized();
        auto right = projectunity::math::cross(upHint, forward).normalized();
        if (right.lengthSquared() <= 0.00001F) {
            right = {1.0F, 0.0F, 0.0F};
        }
        const auto up = projectunity::math::cross(forward, right).normalized();
        const projectunity::editor::ViewportRenderWorldCamera camera {
            eye,
            right,
            up,
            forward,
            1.04719755F,
            16.0F / 9.0F,
            0.05F,
            std::max(radius * 6.0F, 4000.0F),
        };
        std::vector<projectunity::renderer::RenderMeshDraw> draws;
        std::vector<projectunity::renderer::RenderLight> lights;
        const auto frame = world.buildFrame(&scene, &manager, {}, camera, {}, 1080, lodSettings, false, draws, lights);
        std::cout << label
                  << " eye=(" << eye.x << "," << eye.y << "," << eye.z << ")"
                  << " forward=(" << forward.x << "," << forward.y << "," << forward.z << ")"
                  << " draws=" << draws.size()
                  << " chunks=" << frame.stats.visibleRenderChunkCount
                  << " finalChunks=" << frame.stats.finalVisibleChunkCount
                  << " instances=" << frame.stats.visibleRenderInstanceCount
                  << " culledDraws=" << frame.stats.culledMeshDrawCount
                  << " occlusionTested=" << frame.stats.occlusionTestedChunkCount
                  << " occlusionRejected=" << frame.stats.occlusionRejectedChunkCount
                  << " hlod=" << frame.stats.hlodMeshDrawCount
                  << " triangles=" << frame.stats.finalTriangleCount
                  << '\n';
    };

    const auto distance = std::max(radius * 1.35F, 20.0F);
    runCamera("+Z_to_center", center + Vec3 {0.0F, 0.0F, -distance}, {0.0F, 0.0F, 1.0F}, {0.0F, 1.0F, 0.0F});
    runCamera("-Z_to_center", center + Vec3 {0.0F, 0.0F, distance}, {0.0F, 0.0F, -1.0F}, {0.0F, 1.0F, 0.0F});
    runCamera("+X_to_center", center + Vec3 {-distance, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F});
    runCamera("-X_to_center", center + Vec3 {distance, 0.0F, 0.0F}, {-1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F});
    runCamera("top_down", center + Vec3 {0.0F, distance, 0.0F}, {0.0F, -1.0F, 0.0F}, {0.0F, 0.0F, 1.0F});
    runCamera("bottom_up", center + Vec3 {0.0F, -distance, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, -1.0F});
    runCamera("near_top_down", {center.x, maximum.y + 40.0F, center.z}, {0.0F, -1.0F, 0.0F}, {0.0F, 0.0F, 1.0F});
    runCamera("near_+X_to_center", {maximum.x + 40.0F, center.y, center.z}, {-1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F});
    runCamera("inside_forward", center, {0.0F, 0.0F, 1.0F}, {0.0F, 1.0F, 0.0F});
    return EXIT_SUCCESS;
}

int main(int argc, char** argv)
{
    if (argc > 1 && argv[1] != nullptr) {
        return runViewportAssetProbe(argv[1]);
    }

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
    const auto lodSettings = projectunity::editor::viewportAssetLodSettingsFromEnvironment();
    const auto visibleFrom = [](
                                 const projectunity::editor::ViewportWorldBounds& bounds,
                                 const projectunity::editor::ViewportRenderWorldCamera& view) {
        return projectunity::editor::viewportBoundsVisible(
            bounds,
            view.eye,
            view.right,
            view.up,
            view.forward,
            view.verticalFovRadians,
            view.aspectRatio,
            view.nearPlane,
            view.farPlane);
    };
    const auto broadImportedFacingBounds = testBounds({-120.0F, -4.0F, 12.0F}, {120.0F, 4.0F, 18.0F});
    if (!visibleFrom(broadImportedFacingBounds, camera)) {
        return fail("Viewport culling rejected a broad imported bound while facing it");
    }
    auto awayCamera = camera;
    awayCamera.right = {-1.0F, 0.0F, 0.0F};
    awayCamera.forward = {0.0F, 0.0F, -1.0F};
    if (visibleFrom(broadImportedFacingBounds, awayCamera)) {
        return fail("Viewport culling kept a broad imported bound behind the camera");
    }
    const auto largeBehindBounds = testBounds({-100.0F, -100.0F, -150.0F}, {100.0F, 100.0F, -90.0F});
    if (visibleFrom(largeBehindBounds, camera)) {
        return fail("Viewport culling kept a fully behind large imported bound");
    }
    const auto cameraIntersectingFlatBounds = testBounds({-8.0F, -0.02F, -0.02F}, {8.0F, 0.02F, 0.02F});
    if (!visibleFrom(cameraIntersectingFlatBounds, camera)) {
        return fail("Viewport culling rejected a flat bound intersecting the camera near plane");
    }

    auto lodCamera = camera;
    lodCamera.farPlane = 2'000.0F;
    TestAssetManager duplicateLodAssets;
    duplicateLodAssets.modelAsset = testSinglePrimitiveLodModel(projectunity::assets::AssetId(1888));
    projectunity::scene::Scene duplicateLodScene;
    auto& nearCopy = duplicateLodScene.createEntity("Near LOD Copy");
    nearCopy.transform.position = {0.0F, 0.0F, 11.0F};
    projectunity::scene::MeshRendererComponent duplicateRenderer;
    duplicateRenderer.modelAssetId = duplicateLodAssets.modelAsset->id;
    nearCopy.meshRenderer = duplicateRenderer;
    const auto nearCopyId = nearCopy.id.value();
    auto& farCopy = duplicateLodScene.createEntity("Far LOD Copy");
    farCopy.transform.position = {0.0F, 0.0F, 620.0F};
    farCopy.meshRenderer = duplicateRenderer;
    const auto farCopyId = farCopy.id.value();
    projectunity::editor::ViewportRenderWorld duplicateLodWorld;
    std::vector<projectunity::renderer::RenderMeshDraw> duplicateLodDraws;
    std::vector<projectunity::renderer::RenderLight> duplicateLodLights;
    (void)duplicateLodWorld.buildFrame(
        &duplicateLodScene,
        &duplicateLodAssets,
        {},
        lodCamera,
        {},
        1080,
        lodSettings,
        false,
        duplicateLodDraws,
        duplicateLodLights);
    const auto nearDraw = std::find_if(duplicateLodDraws.begin(), duplicateLodDraws.end(), [nearCopyId](const auto& draw) {
        return draw.sceneNodeId == nearCopyId;
    });
    const auto farDraw = std::find_if(duplicateLodDraws.begin(), duplicateLodDraws.end(), [farCopyId](const auto& draw) {
        return draw.sceneNodeId == farCopyId;
    });
    if (nearDraw == duplicateLodDraws.end() || farDraw == duplicateLodDraws.end()) {
        std::cerr << "duplicateLodDraws=" << duplicateLodDraws.size()
                  << " nearId=" << nearCopyId
                  << " farId=" << farCopyId;
        for (const auto& draw : duplicateLodDraws) {
            std::cerr << " [node=" << draw.sceneNodeId
                      << " lod=" << draw.lodIndex
                      << " depth=" << draw.sortDepth
                      << " centerZ=" << draw.worldBoundsCenter[2]
                      << "]";
        }
        std::cerr << '\n';
        return fail("Viewport did not render both duplicate LOD asset instances");
    }
    if (farDraw->lodIndex == 0U) {
        return fail("Viewport shared the near duplicate asset LOD with the far duplicate");
    }
    if (nearDraw->lodIndex >= farDraw->lodIndex) {
        return fail("Viewport did not choose a finer LOD for the near duplicate asset");
    }

    auto completeAssetSettings = lodSettings;
    completeAssetSettings.debugOverride = projectunity::editor::ViewportHlodDebugOverride::ForceDetailed;
    completeAssetSettings.forceLod0 = true;
    completeAssetSettings.frustumCullingEnabled = false;
    completeAssetSettings.instanceCullingEnabled = false;
    completeAssetSettings.occlusionCullingEnabled = false;
    completeAssetSettings.spatialCellCullingEnabled = false;
    completeAssetSettings.triangleBudgetEnabled = false;
    projectunity::editor::ViewportRenderWorld completeAssetWorld;
    std::vector<projectunity::renderer::RenderMeshDraw> completeAssetDraws;
    auto reverseLodCamera = lodCamera;
    reverseLodCamera.right = {-1.0F, 0.0F, 0.0F};
    reverseLodCamera.forward = {0.0F, 0.0F, -1.0F};
    (void)completeAssetWorld.buildFrame(
        &duplicateLodScene,
        &duplicateLodAssets,
        {},
        reverseLodCamera,
        {},
        1080,
        completeAssetSettings,
        false,
        completeAssetDraws,
        duplicateLodLights);
    if (completeAssetDraws.size() != 2U
        || std::any_of(completeAssetDraws.begin(), completeAssetDraws.end(), [](const auto& draw) {
            return draw.lodIndex != 0U;
        })) {
        return fail("Complete asset mode did not preserve every source draw at LOD0 with culling disabled");
    }

    TestAssetManager broadLodAssets;
    broadLodAssets.modelAsset = testBroadPrimitiveLodModel(projectunity::assets::AssetId(1889));
    projectunity::scene::Scene broadLodScene;
    auto& broadEntity = broadLodScene.createEntity("Broad Near Edge LOD");
    projectunity::scene::MeshRendererComponent broadRenderer;
    broadRenderer.modelAssetId = broadLodAssets.modelAsset->id;
    broadEntity.meshRenderer = broadRenderer;
    projectunity::editor::ViewportRenderWorld broadLodWorld;
    std::vector<projectunity::renderer::RenderMeshDraw> broadLodDraws;
    std::vector<projectunity::renderer::RenderLight> broadLodLights;
    auto broadCamera = lodCamera;
    broadCamera.eye = {540.0F, 0.0F, 150.0F};
    broadCamera.right = {0.0F, 0.0F, -1.0F};
    broadCamera.up = {0.0F, 1.0F, 0.0F};
    broadCamera.forward = {-1.0F, 0.0F, 0.0F};
    (void)broadLodWorld.buildFrame(
        &broadLodScene,
        &broadLodAssets,
        {},
        broadCamera,
        {},
        1080,
        lodSettings,
        false,
        broadLodDraws,
        broadLodLights);
    if (broadLodDraws.empty()) {
        return fail("Viewport culled a broad primitive while the camera was near its visible edge");
    }
    if (broadLodDraws.front().lodIndex != 0U) {
        return fail("Viewport LOD used center distance instead of bounds distance near a broad primitive");
    }

    TestAssetManager chunkedLodAssets;
    chunkedLodAssets.modelAsset = testChunkedLodFieldModel();
    projectunity::scene::Scene chunkedLodScene;
    auto& chunkedRoot = chunkedLodScene.createEntity("Chunked LOD Field");
    projectunity::scene::MeshRendererComponent chunkedRenderer;
    chunkedRenderer.modelAssetId = chunkedLodAssets.modelAsset->id;
    chunkedRoot.meshRenderer = chunkedRenderer;
    projectunity::editor::ViewportRenderWorld chunkedLodWorld;
    std::vector<projectunity::renderer::RenderMeshDraw> chunkedLodDraws;
    std::vector<projectunity::renderer::RenderLight> chunkedLodLights;
    (void)chunkedLodWorld.buildFrame(
        &chunkedLodScene,
        &chunkedLodAssets,
        {},
        lodCamera,
        {},
        1080,
        lodSettings,
        false,
        chunkedLodDraws,
        chunkedLodLights);
    auto chunkedHasLod0 = false;
    auto chunkedHasCoarseLod = false;
    for (const auto& draw : chunkedLodDraws) {
        chunkedHasLod0 = chunkedHasLod0 || draw.lodIndex == 0U;
        chunkedHasCoarseLod = chunkedHasCoarseLod || draw.lodIndex > 0U;
    }
    if (!chunkedHasLod0 || !chunkedHasCoarseLod) {
        std::cerr << "chunkedLodDraws=" << chunkedLodDraws.size();
        for (const auto& draw : chunkedLodDraws) {
            std::cerr << " [node=" << draw.sceneNodeId
                      << " lod=" << draw.lodIndex
                      << " depth=" << draw.sortDepth
                      << " centerZ=" << draw.worldBoundsCenter[2]
                      << " boundsDistance=" << draw.distanceToCameraBounds
                      << "]";
        }
        std::cerr << '\n';
        return fail("Viewport forced one LOD across a chunked asset while the camera was inside its root bounds");
    }

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
        lodSettings,
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
        lodSettings,
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
        lodSettings,
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
        lodSettings,
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
        lodSettings,
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
    (void)rejectedShadowWorld.buildFrame(&rejectedShadowScene, &shadowAssets, {}, camera, {}, 1080, lodSettings, false, rejectedColorDraws, rejectedLights);
    projectunity::editor::ViewportRenderWorldStats rejectedShadowStats;
    rejectedShadowWorld.collectShadowCasters(shadowSelection, &sun, camera, 1080, lodSettings, {}, rejectedShadowDraws, rejectedShadowStats);
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
        lodSettings,
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
        lodSettings,
        {},
        forwardShadowDraws,
        forwardShadowStats);
    shadowBudgetWorld.collectShadowCasters(
        shadowBudgetSelection,
        &sun,
        upwardCamera,
        1080,
        lodSettings,
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
