#include "VulkanViewportTarget.hpp"
#include "VulkanMaterialTextureSet.hpp"

namespace projectunity::renderer {

bool VulkanViewportTarget::prepareMeshBatchResources(
    const RenderFrame& frame,
    VulkanUploadContext& uploads,
    VulkanMeshCache& meshCache,
    VulkanTextureCache& textureCache,
    std::string* errorMessage)
{
    for (auto& batch : meshBatches_) {
        const auto& draw = *batch.draw;
        const VulkanMeshKey meshKey {draw.modelAssetId.value(), draw.primitiveIndex, draw.lodIndex};
        batch.mesh = meshCache.ensureUploaded(context_.resources(), uploads, meshKey, *draw.primitive, errorMessage);
        if (batch.mesh == nullptr) {
            return false;
        }
        const auto textures = uploadMaterialTextureSet(
            context_.resources(),
            uploads,
            textureCache,
            draw,
            frame.environment,
            errorMessage);
        if (!textures.complete()) {
            return false;
        }
        batch.materialDescriptor = textureDescriptor(
            *textures.baseColor,
            *textures.normal,
            *textures.metallicRoughness,
            *textures.occlusion,
            *textures.emissive,
            *textures.brdfLut,
            *textures.irradianceCube,
            *textures.prefilteredEnvironment,
            errorMessage);
        if (batch.materialDescriptor == VK_NULL_HANDLE) {
            return false;
        }
    }
    return true;
}

} // namespace projectunity::renderer
