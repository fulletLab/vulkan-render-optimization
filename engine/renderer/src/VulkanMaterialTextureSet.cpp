#include "VulkanMaterialTextureSet.hpp"

namespace projectunity::renderer {

bool VulkanMaterialTextureSet::complete() const noexcept
{
    return baseColor != nullptr
        && normal != nullptr
        && metallicRoughness != nullptr
        && occlusion != nullptr
        && emissive != nullptr
        && brdfLut != nullptr
        && irradianceCube != nullptr
        && prefilteredEnvironment != nullptr;
}

VulkanMaterialTextureSet uploadMaterialTextureSet(
    VulkanResourceContext context,
    VulkanUploadContext& uploads,
    VulkanTextureCache& textureCache,
    const RenderMeshDraw& draw,
    const RenderEnvironmentSettings& environment,
    std::string* errorMessage)
{
    VulkanMaterialTextureSet textures;
    textures.baseColor = textureCache.ensureSrgbUploaded(
        context,
        uploads,
        draw.baseColorTexture,
        errorMessage,
        VulkanTextureRole::BaseColor);
    textures.normal = textureCache.ensureNormalUploaded(context, uploads, draw.normalTexture, errorMessage);
    textures.metallicRoughness = textureCache.ensureUploaded(
        context,
        uploads,
        draw.metallicRoughnessTexture,
        errorMessage,
        VulkanTextureRole::MetallicRoughness);
    textures.occlusion = textureCache.ensureUploaded(
        context,
        uploads,
        draw.occlusionTexture,
        errorMessage,
        VulkanTextureRole::Occlusion);
    textures.emissive = textureCache.ensureSrgbUploaded(
        context,
        uploads,
        draw.emissiveTexture,
        errorMessage,
        VulkanTextureRole::Emissive);
    textures.brdfLut = textureCache.ensureBrdfLutUploaded(context, uploads, errorMessage);
    textures.irradianceCube = textureCache.ensureIrradianceCubeUploaded(context, uploads, environment, errorMessage);
    textures.prefilteredEnvironment = textureCache.ensurePrefilteredEnvironmentCubeUploaded(context, uploads, environment, errorMessage);
    return textures;
}

} // namespace projectunity::renderer
