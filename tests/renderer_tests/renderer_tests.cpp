#include <projectunity/renderer/VulkanRenderer.hpp>

#include <cstdlib>
#include <iostream>

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
