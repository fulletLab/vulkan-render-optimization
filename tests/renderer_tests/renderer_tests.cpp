#include <projectunity/renderer/VulkanRenderer.hpp>
#include <projectunity/renderer/RenderDrawOrdering.hpp>

#include <array>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

int fail(const char* message)
{
    std::cerr << message << '\n';
    return EXIT_FAILURE;
}

} // namespace

int main()
{
    using namespace projectunity::renderer;

    projectunity::assets::MaterialAsset opaqueMaterial;
    projectunity::assets::MaterialAsset maskMaterial;
    maskMaterial.alphaMode = projectunity::assets::MaterialAlphaMode::Mask;
    projectunity::assets::MaterialAsset blendMaterial;
    blendMaterial.alphaMode = projectunity::assets::MaterialAlphaMode::Blend;
    std::array<RenderMeshDraw, 5> unsortedDraws {};
    unsortedDraws[0].primitiveIndex = 10;
    unsortedDraws[0].material = &blendMaterial;
    unsortedDraws[0].sortDepth = 2.0F;
    unsortedDraws[1].primitiveIndex = 20;
    unsortedDraws[1].material = &opaqueMaterial;
    unsortedDraws[2].primitiveIndex = 30;
    unsortedDraws[2].material = &blendMaterial;
    unsortedDraws[2].sortDepth = 8.0F;
    unsortedDraws[3].primitiveIndex = 40;
    unsortedDraws[3].material = &maskMaterial;
    unsortedDraws[4].primitiveIndex = 50;
    unsortedDraws[4].material = &blendMaterial;
    unsortedDraws[4].sortDepth = 5.0F;
    std::vector<const RenderMeshDraw*> orderedDraws;
    orderMeshDraws(unsortedDraws, orderedDraws);
    if (orderedDraws.size() != unsortedDraws.size()
        || orderedDraws[0]->primitiveIndex != 20
        || orderedDraws[1]->primitiveIndex != 40
        || orderedDraws[2]->primitiveIndex != 30
        || orderedDraws[3]->primitiveIndex != 50
        || orderedDraws[4]->primitiveIndex != 10) {
        return fail("Renderer mesh draw ordering did not keep opaque draws before back-to-front blended draws");
    }

    std::string error;
    RendererConfig config;
    config.applicationName = "ProjectUnity Renderer Tests";
    config.enableValidation = true;
    config.enableRenderDocMarkers = true;

    auto renderer = createVulkanRenderer(config, &error);
    if (renderer == nullptr) {
        std::cerr << error << '\n';
        return fail("Vulkan renderer creation failed");
    }

    if (!renderer->isReady()) {
        return fail("Vulkan renderer did not report ready state");
    }

    const auto& stats = renderer->stats();
    if (stats.gpuName.empty()) {
        return fail("Vulkan renderer did not expose a GPU name");
    }
    if (!stats.vmaAllocatorReady) {
        return fail("Vulkan renderer did not create VMA allocator");
    }

    ViewportRenderSurfaceDesc invalidSurface;
    if (renderer->supportsSurface(invalidSurface)) {
        return fail("Renderer accepted an invalid viewport surface");
    }
    if (renderer->prepareSurface(invalidSurface, &error)) {
        return fail("Renderer prepared an invalid viewport surface");
    }
    if (error.empty()) {
        return fail("Renderer did not report an error for invalid viewport surface");
    }
    error.clear();
    RenderFrame frame;
    if (renderer->renderSurfaceFrame(invalidSurface, frame, &error)) {
        return fail("Renderer rendered an invalid viewport surface");
    }
    if (error.empty()) {
        return fail("Renderer did not report an error for invalid viewport render");
    }

    ViewportRenderSurfaceDesc validSurface;
    validSurface.nativeWindowHandle = reinterpret_cast<void*>(0x1);
    validSurface.width = 1280;
    validSurface.height = 720;
    if (!renderer->supportsSurface(validSurface)) {
        return fail("Renderer rejected a valid viewport surface description");
    }

    return EXIT_SUCCESS;
}
