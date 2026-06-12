#include "ViewportMeshLod.hpp"
#include "ViewportRenderWorldOcclusion.hpp"

#include <projectunity/assets/AssetManager.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>

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

projectunity::assets::MeshPrimitive mocOccluderPrimitive()
{
    projectunity::assets::MeshPrimitive primitive;
    primitive.vertices = {
        {{-2.0F, -2.0F, 5.0F}},
        {{2.0F, -2.0F, 5.0F}},
        {{2.0F, 2.0F, 5.0F}},
        {{-2.0F, 2.0F, 5.0F}},
    };
    primitive.indices = {0U, 1U, 2U, 0U, 2U, 3U};
    return primitive;
}

} // namespace

int main()
{
    using projectunity::editor::indexCountForViewportLod;
    using projectunity::editor::selectViewportMeshLod;

    const auto primitive = testPrimitive();
    constexpr auto fov = 1.04719755F;
    constexpr auto viewportHeight = 1080.0F;

    if (selectViewportMeshLod(primitive, 20.0F, 100.0F, fov, viewportHeight, false) != 1U) {
        return fail("Viewport LOD did not preserve sub-pixel error for a moderately distant mesh");
    }
    if (selectViewportMeshLod(primitive, 20.0F, 500.0F, fov, viewportHeight, false) != 2U) {
        return fail("Viewport LOD did not choose a coarser sub-pixel LOD at greater distance");
    }
    if (selectViewportMeshLod(primitive, 20.0F, 1'200.0F, fov, viewportHeight, false) != 3U) {
        return fail("Viewport LOD did not choose the coarsest valid sub-pixel LOD when far away");
    }
    if (selectViewportMeshLod(primitive, 20.0F, 500.0F, fov, viewportHeight, true) != 0U) {
        return fail("Viewport LOD changed a force-full-resolution mesh");
    }
    if (selectViewportMeshLod(primitive, 20.0F, 20.0F, fov, viewportHeight, false) != 0U) {
        return fail("Viewport LOD simplified a mesh while the camera intersected its bounds");
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
    projectunity::editor::ViewportOcclusionBuffer defaultOcclusion(camera, 1080);
    if (defaultOcclusion.backend() != projectunity::editor::ViewportOcclusionBackend::Coarse) {
        return fail("Viewport automatic occlusion backend did not preserve the low-cost coarse default");
    }

    projectunity::editor::ViewportOcclusionBuffer occlusion(
        camera,
        1080,
        projectunity::editor::ViewportOcclusionBackend::Coarse);
    if (!occlusion.addOccluder(testBounds({-2.0F, -2.0F, 4.8F}, {2.0F, 2.0F, 5.2F}))) {
        return fail("Viewport occlusion buffer did not accept a large opaque occluder");
    }
    if (!occlusion.isOccluded(testBounds({-0.5F, -0.5F, 9.8F}, {0.5F, 0.5F, 10.2F}))) {
        return fail("Viewport occlusion did not reject a fully covered chunk behind an occluder");
    }
    if (occlusion.isOccluded(testBounds({-0.35F, -0.35F, 5.7F}, {0.35F, 0.35F, 6.1F}))) {
        return fail("Viewport occlusion rejected a close chunk behind an occluder");
    }
    if (occlusion.isOccluded(testBounds({1.6F, -0.5F, 9.8F}, {3.0F, 0.5F, 10.2F}))) {
        return fail("Viewport occlusion rejected a partially protruding chunk");
    }
    if (occlusion.isOccluded(testBounds({-0.5F, -0.5F, 2.8F}, {0.5F, 0.5F, 3.2F}))) {
        return fail("Viewport occlusion rejected a chunk in front of the occluder");
    }

    projectunity::editor::ViewportOcclusionBuffer nearOcclusion(
        camera,
        1080,
        projectunity::editor::ViewportOcclusionBackend::Coarse);
    if (nearOcclusion.addOccluder(testBounds({-3.0F, -3.0F, -0.2F}, {3.0F, 3.0F, 1.2F}))) {
        return fail("Viewport occlusion accepted an occluder crossing the near plane");
    }
    if (nearOcclusion.isOccluded(testBounds({-0.4F, -0.4F, 4.8F}, {0.4F, 0.4F, 5.2F}))) {
        return fail("Viewport near-plane occlusion rejected a chunk without a safe occluder");
    }

    projectunity::editor::ViewportOcclusionBuffer mocOcclusion(
        camera,
        1080,
        projectunity::editor::ViewportOcclusionBackend::MaskedOcclusionCulling);
    const auto mocPrimitive = mocOccluderPrimitive();
    if (mocOcclusion.backend() != projectunity::editor::ViewportOcclusionBackend::MaskedOcclusionCulling) {
        return fail("Viewport MOC backend did not initialize");
    }
    if (!mocOcclusion.addOccluderTriangles(mocPrimitive, {}, true)) {
        return fail("Viewport MOC backend did not accept a real triangle occluder");
    }
    if (mocOcclusion.occluderTriangleCount() != 2U) {
        return fail("Viewport MOC backend did not count submitted occluder triangles");
    }
    if (!mocOcclusion.isOccluded(testBounds({-0.35F, -0.35F, 9.8F}, {0.35F, 0.35F, 10.2F}))) {
        return fail("Viewport MOC backend did not reject a covered chunk behind real triangles");
    }
    if (mocOcclusion.isOccluded(testBounds({4.4F, -0.5F, 9.8F}, {5.6F, 0.5F, 10.2F}))) {
        return fail("Viewport MOC backend rejected a protruding chunk");
    }

    return EXIT_SUCCESS;
}
