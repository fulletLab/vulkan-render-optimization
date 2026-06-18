#include <projectunity/renderer/VulkanRenderer.hpp>
#include <projectunity/renderer/RenderBrdfLut.hpp>
#include <projectunity/renderer/RenderDrawOrdering.hpp>
#include <projectunity/renderer/RenderEnvironmentMap.hpp>
#include <projectunity/renderer/RenderShadowCache.hpp>
#include <projectunity/renderer/RenderShadowSetup.hpp>

#include "VulkanTextureSamplerPolicy.hpp"

#include <algorithm>
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

    std::array<RenderMeshDraw, 1> shadowDraws {};
    shadowDraws[0].modelAssetId = projectunity::assets::AssetId(17);
    shadowDraws[0].primitiveIndex = 3;
    shadowDraws[0].renderInstanceId = 99;
    shadowDraws[0].sceneNodeId = 7;
    shadowDraws[0].material = &opaqueMaterial;
    RenderFrame shadowFrame;
    shadowFrame.shadowsEnabled = true;
    shadowFrame.shadowMode = RenderShadowMode::DirectionalCascades;
    shadowFrame.shadowViewCount = 1;
    shadowFrame.shadowCascadeCount = 1;
    shadowFrame.shadowMeshDraws = shadowDraws;
    const auto initialShadowSignature = renderShadowContentSignature(shadowFrame);
    if (initialShadowSignature != renderShadowContentSignature(shadowFrame)) {
        return fail("Renderer shadow content signature was not stable for an unchanged frame");
    }
    shadowDraws[0].modelMatrix.values[12] = 2.0F;
    if (initialShadowSignature == renderShadowContentSignature(shadowFrame)) {
        return fail("Renderer shadow content signature ignored a caster transform change");
    }
    shadowDraws[0].modelMatrix.values[12] = 0.0F;
    shadowFrame.shadowViewProjection.values[0] = 0.5F;
    if (initialShadowSignature == renderShadowContentSignature(shadowFrame)) {
        return fail("Renderer shadow content signature ignored a shadow projection change");
    }
    std::array<RenderMeshDraw, 5> batchableDraws {};
    batchableDraws[0].modelAssetId = projectunity::assets::AssetId(7);
    batchableDraws[0].primitiveIndex = 2;
    batchableDraws[0].material = &opaqueMaterial;
    batchableDraws[1].modelAssetId = projectunity::assets::AssetId(8);
    batchableDraws[1].primitiveIndex = 1;
    batchableDraws[1].material = &opaqueMaterial;
    batchableDraws[2] = batchableDraws[0];
    batchableDraws[3] = batchableDraws[1];
    batchableDraws[4] = batchableDraws[0];
    orderMeshDraws(batchableDraws, orderedDraws);
    if (orderedDraws.size() != batchableDraws.size()
        || orderedDraws[0]->modelAssetId != orderedDraws[1]->modelAssetId
        || orderedDraws[1]->modelAssetId != orderedDraws[2]->modelAssetId
        || orderedDraws[3]->modelAssetId != orderedDraws[4]->modelAssetId) {
        return fail("Renderer mesh draw ordering did not group compatible opaque draws for instancing");
    }
    std::array<RenderMeshDraw, 3> lodDraws {};
    for (auto& draw : lodDraws) {
        draw.modelAssetId = projectunity::assets::AssetId(9);
        draw.primitiveIndex = 4;
        draw.material = &opaqueMaterial;
    }
    lodDraws[0].lodIndex = 1;
    lodDraws[1].lodIndex = 0;
    lodDraws[2].lodIndex = 1;
    orderMeshDraws(lodDraws, orderedDraws);
    if (orderedDraws.size() != lodDraws.size()
        || orderedDraws[1]->lodIndex != 1U
        || orderedDraws[2]->lodIndex != 1U) {
        return fail("Renderer mesh draw ordering did not keep matching runtime LODs batchable");
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
        || directionalShadow.mode != RenderShadowMode::DirectionalCascades
        || directionalShadow.viewCount != kMaxShadowCascades
        || directionalShadow.cascadeCount != kMaxShadowCascades
        || !matrixIsFinite(directionalShadow.viewProjection)) {
        return fail("Renderer shadow selection did not prefer a finite directional shadow map");
    }
    for (std::size_t index = 0; index < kMaxShadowCascades; ++index) {
        if (!matrixIsFinite(directionalShadow.viewProjections[index])
            || directionalShadow.cascadeSplits[index] <= 0.0F
            || (index > 0U && directionalShadow.cascadeSplits[index] <= directionalShadow.cascadeSplits[index - 1U])) {
            return fail("Renderer directional shadow selection did not create finite ordered cascades");
        }
    }

    const std::array<RenderLight, 2> spotOnlyLights {mixedLights[0], mixedLights[1]};
    const auto spotShadow = chooseShadowMap(spotOnlyLights, {0.0F, 0.0F, 0.0F}, 4.0F);
    if (!spotShadow.enabled
        || spotShadow.lightIndex != 1U
        || spotShadow.lightType != RenderLightType::Spot
        || spotShadow.mode != RenderShadowMode::Spot2D
        || spotShadow.viewCount != 1U
        || !matrixIsFinite(spotShadow.viewProjection)) {
        return fail("Renderer shadow selection did not fall back to a finite spot shadow map");
    }

    const std::array<RenderLight, 1> pointOnlyLights {mixedLights[0]};
    const auto pointShadow = chooseShadowMap(pointOnlyLights, {0.0F, 0.0F, 0.0F}, 4.0F);
    if (!pointShadow.enabled
        || pointShadow.lightIndex != 0U
        || pointShadow.lightType != RenderLightType::Point
        || pointShadow.mode != RenderShadowMode::PointCubemap
        || pointShadow.viewCount != 6U
        || pointShadow.depthFarPlane <= 0.0F
        || !matrixIsFinite(pointShadow.viewProjection)) {
        return fail("Renderer shadow selection did not create a finite point-light cubemap shadow map");
    }
    for (std::uint32_t face = 0; face < pointShadow.viewCount; ++face) {
        if (!matrixIsFinite(pointShadow.viewProjections[face])) {
            return fail("Renderer point-light cubemap shadow selection produced an invalid face matrix");
        }
    }
    if (!shadowSphereIntersects(directionalShadow.viewProjection, {0.0F, 0.0F, 0.0F}, 1.0F)) {
        return fail("Renderer shadow sphere culling rejected the selected visible bounds center");
    }
    if (!shadowSphereIntersects(directionalShadow.viewProjections[kMaxShadowCascades - 1U], {0.0F, 0.0F, 0.0F}, 1.0F)) {
        return fail("Renderer shadow sphere culling rejected the widest directional cascade center");
    }
    if (shadowSphereIntersects(directionalShadow.viewProjection, {5000.0F, 5000.0F, 5000.0F}, 1.0F)) {
        return fail("Renderer shadow sphere culling accepted a distant off-map caster");
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

    projectunity::assets::TextureSamplerAsset defaultSurfaceSampler;
    const auto highSamplerState = buildTextureSamplerState({true, 12.0F}, defaultSurfaceSampler, 9U);
    if (highSamplerState.magFilter != VK_FILTER_LINEAR
        || highSamplerState.minFilter != VK_FILTER_LINEAR
        || highSamplerState.mipmapMode != VK_SAMPLER_MIPMAP_MODE_LINEAR
        || highSamplerState.anisotropyEnable != VK_TRUE
        || highSamplerState.maxAnisotropy != 12.0F
        || highSamplerState.mipLodBias != 0.0F
        || highSamplerState.minLod != 0.0F
        || highSamplerState.maxLod != 8.0F) {
        return fail("Default terrain/material sampler policy is not trilinear anisotropic with a valid mip LOD range");
    }
    if (renderTextureQualityMaxAnisotropy(RenderTextureQuality::High) != 16.0F) {
        return fail("High texture quality should request 16x anisotropy before device-limit clamping");
    }
    const auto unsupportedAnisoState = buildTextureSamplerState({false, 16.0F}, defaultSurfaceSampler, 9U);
    if (unsupportedAnisoState.anisotropyEnable != VK_FALSE
        || unsupportedAnisoState.mipmapMode != VK_SAMPLER_MIPMAP_MODE_LINEAR
        || unsupportedAnisoState.maxLod != 8.0F) {
        return fail("Sampler policy did not fall back to trilinear when anisotropy is unsupported");
    }
    projectunity::assets::TextureSamplerAsset nearestSampler;
    nearestSampler.magnificationFilter = projectunity::assets::TextureFilterMode::Nearest;
    nearestSampler.minificationFilter = projectunity::assets::TextureFilterMode::Nearest;
    nearestSampler.wrapU = projectunity::assets::TextureWrapMode::ClampToEdge;
    nearestSampler.wrapV = projectunity::assets::TextureWrapMode::MirroredRepeat;
    const auto nearestState = buildTextureSamplerState({true, 16.0F}, nearestSampler, 5U);
    if (nearestState.magFilter != VK_FILTER_NEAREST
        || nearestState.minFilter != VK_FILTER_NEAREST
        || nearestState.anisotropyEnable != VK_FALSE
        || nearestState.addressModeU != VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
        || nearestState.addressModeV != VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT
        || nearestState.maxLod != 4.0F) {
        return fail("Sampler policy did not preserve nearest/wrap state for imported glTF samplers");
    }
    projectunity::assets::TextureSamplerAsset noMipSampler;
    noMipSampler.useMipmaps = false;
    const auto noMipState = buildTextureSamplerState({true, 16.0F}, noMipSampler, 7U);
    if (noMipState.anisotropyEnable != VK_FALSE
        || noMipState.mipmapMode != VK_SAMPLER_MIPMAP_MODE_NEAREST
        || noMipState.maxLod != 0.0F) {
        return fail("Sampler policy did not preserve imported no-mipmap usage");
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
    projectunity::assets::TextureAsset environmentTexture;
    environmentTexture.id = projectunity::assets::AssetId(42);
    environmentTexture.width = 4;
    environmentTexture.height = 2;
    environmentTexture.rgba8 = {
        255U, 0U, 0U, 255U, 0U, 255U, 0U, 255U, 0U, 0U, 255U, 255U, 255U, 255U, 0U, 255U,
        16U, 32U, 48U, 255U, 64U, 80U, 96U, 255U, 128U, 144U, 160U, 255U, 192U, 208U, 224U, 255U,
    };
    RenderEnvironmentSettings texturedEnvironment;
    texturedEnvironment.sourceTexture = &environmentTexture;
    const auto texturedCube = generateProceduralIrradianceCube(8U, texturedEnvironment);
    if (texturedCube.mips.front().rgba8 == irradianceCube.mips.front().rgba8
        || texturedCube.mips.front().rgba8 == warmCube.mips.front().rgba8) {
        return fail("Renderer environment cube generator ignored source texture data");
    }
    projectunity::assets::TextureAsset hdrEnvironmentTexture;
    hdrEnvironmentTexture.id = projectunity::assets::AssetId(43);
    hdrEnvironmentTexture.width = 4;
    hdrEnvironmentTexture.height = 2;
    hdrEnvironmentTexture.rgba32f.assign(4U * 2U * 4U, 1.0F);
    for (std::size_t pixel = 0; pixel < 8U; ++pixel) {
        hdrEnvironmentTexture.rgba32f[pixel * 4U] = 4.0F;
        hdrEnvironmentTexture.rgba32f[pixel * 4U + 1U] = 2.0F;
        hdrEnvironmentTexture.rgba32f[pixel * 4U + 2U] = 1.0F;
    }
    RenderEnvironmentSettings hdrTexturedEnvironment;
    hdrTexturedEnvironment.sourceTexture = &hdrEnvironmentTexture;
    const auto hdrCube = generateProceduralIrradianceCube(8U, hdrTexturedEnvironment);
    if (hdrCube.mips.front().rgba32f.size() != 8U * 8U * 6U * 4U
        || *std::max_element(hdrCube.mips.front().rgba32f.begin(), hdrCube.mips.front().rgba32f.end()) < 3.5F
        || hdrCube.mips.front().rgba8.empty()) {
        return fail("Renderer environment cube generator did not preserve HDR source radiance");
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
    if (stats.textureQuality != RenderTextureQuality::High) {
        return fail("Vulkan renderer did not default to High texture quality");
    }
    if (stats.samplerAnisotropySupported && !stats.samplerAnisotropyEnabled) {
        return fail("Vulkan renderer did not enable samplerAnisotropy when the physical device supports it");
    }
    if (stats.deviceMaxSamplerAnisotropy < 1.0F
        || stats.activeMaxSamplerAnisotropy < 1.0F
        || stats.activeMaxSamplerAnisotropy > stats.deviceMaxSamplerAnisotropy
        || stats.activeMaxSamplerAnisotropy > renderTextureQualityMaxAnisotropy(stats.textureQuality)) {
        return fail("Vulkan renderer sampler anisotropy limit is outside the device/quality bounds");
    }
    if (stats.lastFrameLightCount != 0
        || stats.lastFrameShadowCasterCount != 0
        || stats.lastFrameShadowViewCount != 0
        || stats.lastFrameCandidateMeshDrawCount != 0
        || stats.lastFrameCulledMeshDrawCount != 0
        || stats.lastFrameMeshBatchCount != 0
        || stats.lastFrameCandidateTriangleCount != 0
        || stats.lastFrameCulledTriangleCount != 0
        || stats.lastFrameVisibleTriangleCount != 0
        || stats.lastFrameLodMeshDrawCount != 0
        || stats.lastFrameLodTriangleReductionCount != 0
        || stats.lastFrameShadowBatchCount != 0
        || stats.lastFrameShadowCulledBatchCount != 0
        || stats.objectsConsidered != 0
        || stats.passedFrustum != 0
        || stats.visibleBatches != 0
        || stats.resourcePrepared != 0
        || stats.shadowCastersSubmitted != 0
        || stats.shadowCandidateInstances != 0
        || stats.shadowPolicyRejectedInstances != 0
        || stats.shadowCandidates != 0
        || stats.shadowSubmitted != 0
        || stats.shadowTriangles != 0
        || stats.shadowRejectedByPolicy != 0
        || stats.shadowRejectedByCasterCull != 0
        || stats.shadowCpuMs != 0.0
        || stats.shadowGpuMs != 0.0
        || stats.shadowBatchesSubmitted != 0
        || stats.shadowInstancesSubmitted != 0
        || stats.shadowTrianglesSubmitted != 0
        || stats.lastFrameShadowMapUpdated
        || stats.vkBindVertex != 0
        || stats.vkBindIndex != 0
        || stats.vkBindDescriptors != 0
        || stats.vkDrawIndexed != 0
        || stats.trianglesSubmitted != 0
        || stats.commandRecordingMs != 0.0
        || stats.resourcePrepareMs != 0.0
        || stats.FPS != 0.0
        || stats.lastFrameRenderCpuTimeUs != 0
        || stats.averageRenderCpuTimeUs != 0
        || stats.lastFrameResourcePrepareCpuTimeUs != 0
        || stats.lastFrameCommandRecordCpuTimeUs != 0
        || stats.lastFrameShadowRecordCpuTimeUs != 0
        || stats.lastFrameMeshRecordCpuTimeUs != 0
        || stats.lastFrameColorRecordCpuTimeUs != 0
        || stats.lastFrameGpuTimestampsValid
        || stats.lastFrameGpuTimeUs != 0
        || stats.lastFrameShadowGpuTimeUs != 0
        || stats.lastFrameMeshGpuTimeUs != 0
        || stats.lastFrameColorGpuTimeUs != 0
        || stats.lastFrameMeshUploadCount != 0
        || stats.lastFrameTextureUploadCount != 0
        || stats.lastFrameStaticUploadBytes != 0
        || stats.lastFrameColorUploadBytes != 0
        || stats.residentMeshCount != 0
        || stats.residentTextureCount != 0
        || stats.textureSamplerCreateCount != 0
        || stats.anisotropicTextureSamplerCount != 0
        || stats.trilinearTextureSamplerCount != 0
        || !stats.lastTextureSamplerName.empty()
        || !stats.lastTextureSamplerRole.empty()
        || stats.lastTextureSamplerWidth != 0
        || stats.lastTextureSamplerHeight != 0
        || stats.lastTextureSamplerMipLevels != 0
        || stats.lastTextureSamplerAnisotropyEnabled
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
    if (frame.meshWireOverlayEnabled
        || frame.selectedMeshWireOverlayEnabled
        || frame.selectedMeshWireOverlaySceneNodeId != 0U) {
        return fail("Renderer mesh wire overlay defaults were not disabled");
    }
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
