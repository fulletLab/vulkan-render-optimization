#include "VulkanViewportTarget.hpp"
#include "VulkanMaterialTextureSet.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace projectunity::renderer {
namespace {

constexpr std::uint64_t kStaticUploadBudgetBytes = 24ULL * 1024ULL * 1024ULL;
constexpr std::uint32_t kStaticUploadBudgetBatches = 12U;

[[nodiscard]] std::uint64_t estimatedMaterialUploadBytes(
    VulkanTextureCache& textureCache,
    const RenderMeshDraw& draw) noexcept
{
    return textureCache.estimatedUploadBytes(draw.baseColorTexture, VulkanTextureColorSpace::Srgb)
        + textureCache.estimatedNormalUploadBytes(draw.normalTexture)
        + textureCache.estimatedUploadBytes(draw.metallicRoughnessTexture, VulkanTextureColorSpace::Linear)
        + textureCache.estimatedUploadBytes(draw.occlusionTexture, VulkanTextureColorSpace::Linear)
        + textureCache.estimatedUploadBytes(draw.emissiveTexture, VulkanTextureColorSpace::Srgb)
        + textureCache.estimatedBrdfLutUploadBytes();
}

} // namespace

bool VulkanViewportTarget::prepareMeshBatchResources(
    const RenderFrame& frame,
    VulkanUploadContext& uploads,
    VulkanMeshCache& meshCache,
    VulkanTextureCache& textureCache,
    std::string* errorMessage)
{
    std::vector<VulkanMeshDrawBatch> preparedBatches;
    preparedBatches.reserve(meshBatches_.size());
    std::uint64_t budgetBytes = 0;
    std::uint32_t budgetBatches = 0;

    for (auto& batch : meshBatches_) {
        const auto& draw = *batch.draw;
        const VulkanMeshKey meshKey {draw.modelAssetId.value(), draw.primitiveIndex, draw.lodIndex};
        const auto pendingBytes = meshCache.estimatedUploadBytes(meshKey, *draw.primitive)
            + estimatedMaterialUploadBytes(textureCache, draw);
        const auto hasStaticUploadWork = pendingBytes > 0U;
        if (hasStaticUploadWork) {
            const auto nextBytes = budgetBytes + pendingBytes;
            const auto budgetIsFull = budgetBatches >= kStaticUploadBudgetBatches
                || (budgetBytes > 0U && nextBytes > kStaticUploadBudgetBytes);
            if (budgetIsFull) {
                continue;
            }
            budgetBytes = std::max<std::uint64_t>(nextBytes, 1U);
            ++budgetBatches;
        }
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
        preparedBatches.push_back(batch);
    }
    meshBatches_ = std::move(preparedBatches);
    return true;
}

} // namespace projectunity::renderer
