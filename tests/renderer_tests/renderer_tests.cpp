#include <projectunity/renderer/VulkanRenderer.hpp>
#include <projectunity/renderer/RenderBrdfLut.hpp>
#include <projectunity/renderer/RenderDrawOrdering.hpp>
#include <projectunity/renderer/RenderEnvironmentMap.hpp>
#include <projectunity/renderer/RenderShadowSetup.hpp>

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

int fail(const char* message)
{
    std::cerr << message << '\n';
    return EXIT_FAILURE;
}

bool matrixIsFinite(const projectunity::renderer::RenderMatrix4& matrix)
{
    for (const auto value : matrix.values) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return true;
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

    std::array<RenderLight, 3> mixedLights {};
    mixedLights[0].type = RenderLightType::Point;
    mixedLights[0].position = {2.0F, 3.0F, 4.0F};
    mixedLights[1].type = RenderLightType::Spot;
    mixedLights[1].position = {0.0F, 5.0F, -2.0F};
    mixedLights[1].direction = {0.0F, -0.8F, 0.2F};
    mixedLights[2].type = RenderLightType::Directional;
    mixedLights[2].direction = {0.35F, -0.82F, 0.45F};
    const auto directionalShadow = chooseShadowMap(mixedLights, {0.0F, 0.0F, 0.0F}, 4.0F);
    if (!directionalShadow.enabled
        || directionalShadow.lightIndex != 2U
        || directionalShadow.lightType != RenderLightType::Directional
        || !matrixIsFinite(directionalShadow.viewProjection)) {
        return fail("Renderer shadow selection did not prefer a finite directional shadow map");
    }

    const std::array<RenderLight, 2> spotOnlyLights {mixedLights[0], mixedLights[1]};
    const auto spotShadow = chooseShadowMap(spotOnlyLights, {0.0F, 0.0F, 0.0F}, 4.0F);
    if (!spotShadow.enabled
        || spotShadow.lightIndex != 1U
        || spotShadow.lightType != RenderLightType::Spot
        || !matrixIsFinite(spotShadow.viewProjection)) {
        return fail("Renderer shadow selection did not fall back to a finite spot shadow map");
    }

    const std::array<RenderLight, 1> pointOnlyLights {mixedLights[0]};
    if (chooseShadowMap(pointOnlyLights, {0.0F, 0.0F, 0.0F}, 4.0F).enabled) {
        return fail("Renderer shadow selection incorrectly enabled a 2D shadow map for point-only lights");
    }

    const auto brdfLut = generateBrdfIntegrationLut(16U, 32U);
    if (brdfLut.width != 16U
        || brdfLut.height != 16U
        || brdfLut.rgba8.size() != 16U * 16U * 4U) {
        return fail("Renderer BRDF integration LUT did not produce the requested RGBA8 texture");
    }
    if (brdfLut.rgba8[3] != 255U || brdfLut.rgba8[brdfLut.rgba8.size() - 1U] != 255U) {
        return fail("Renderer BRDF integration LUT did not preserve opaque alpha");
    }
    if (brdfLut.rgba8[0] == 0U && brdfLut.rgba8[1] == 0U) {
        return fail("Renderer BRDF integration LUT contains no low-roughness response");
    }

    const auto irradianceCube = generateProceduralIrradianceCube(8U);
    if (irradianceCube.mips.size() != 1U
        || irradianceCube.mips.front().faceSize != 8U
        || irradianceCube.mips.front().rgba8.size() != 8U * 8U * 6U * 4U) {
        return fail("Renderer irradiance cube generator produced invalid face data");
    }
    const auto prefilteredCube = generateProceduralPrefilteredCube(16U, 4U);
    if (prefilteredCube.mips.size() != 4U
        || prefilteredCube.mips[0].faceSize != 16U
        || prefilteredCube.mips[3].faceSize != 2U
        || prefilteredCube.mips[3].rgba8.size() != 2U * 2U * 6U * 4U) {
        return fail("Renderer prefiltered environment cube generator produced invalid mip data");
    }
    RenderEnvironmentSettings warmEnvironment;
    warmEnvironment.skyColor = {0.80F, 0.35F, 0.12F};
    warmEnvironment.groundColor = {0.20F, 0.06F, 0.03F};
    warmEnvironment.intensity = 1.4F;
    const auto warmCube = generateProceduralIrradianceCube(8U, warmEnvironment);
    if (warmCube.mips.front().rgba8 == irradianceCube.mips.front().rgba8) {
        return fail("Renderer environment cube generator ignored RenderEnvironmentSettings");
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
    if (stats.lastFrameLightCount != 0
        || stats.lastFrameShadowCasterCount != 0
        || stats.lastFrameCandidateMeshDrawCount != 0
        || stats.lastFrameCulledMeshDrawCount != 0
        || stats.lastFrameCandidateTriangleCount != 0
        || stats.lastFrameCulledTriangleCount != 0
        || stats.lastFrameVisibleTriangleCount != 0
        || stats.lastFrameRenderCpuTimeUs != 0
        || stats.averageRenderCpuTimeUs != 0
        || stats.lastFrameMeshUploadCount != 0
        || stats.lastFrameTextureUploadCount != 0
        || stats.lastFrameStaticUploadBytes != 0
        || stats.lastFrameColorUploadBytes != 0
        || stats.residentMeshCount != 0
        || stats.residentTextureCount != 0
        || stats.totalMeshUploadCount != 0
        || stats.totalTextureUploadCount != 0
        || stats.totalStaticUploadBytes != 0
        || stats.totalColorUploadBytes != 0
        || stats.shadowFramesPresented != 0
        || stats.shadowCasterDrawsPresented != 0) {
        return fail("Vulkan renderer frame lighting/shadow stats were not initialized to zero");
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
