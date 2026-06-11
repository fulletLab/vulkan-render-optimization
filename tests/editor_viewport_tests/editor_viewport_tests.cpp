#include "ViewportMeshLod.hpp"

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

    return EXIT_SUCCESS;
}
