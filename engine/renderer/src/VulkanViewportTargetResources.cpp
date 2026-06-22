#include "VulkanViewportTarget.hpp"
#include "VulkanMaterialTextureSet.hpp"

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <utility>
#include <vector>

namespace projectunity::renderer {
namespace {

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

struct UploadedMeshFallback {
    const VulkanMeshBuffers* mesh {nullptr};
    std::uint32_t lodIndex {0};
};

[[nodiscard]] UploadedMeshFallback uploadedMeshFallback(
    const VulkanMeshCache& meshCache,
    const RenderMeshDraw& draw) noexcept
{
    if (draw.primitive == nullptr) {
        return {};
    }
    const auto maxLod = static_cast<std::uint32_t>(draw.primitive->lods.size());
    for (std::uint32_t offset = 1U; offset <= maxLod; ++offset) {
        if (draw.lodIndex >= offset) {
            const auto candidate = draw.lodIndex - offset;
            if (const auto* mesh = meshCache.uploaded({draw.modelAssetId.value(), draw.primitiveIndex, candidate})) {
                return {mesh, candidate};
            }
        }
        if (draw.lodIndex <= maxLod && offset <= maxLod - draw.lodIndex) {
            const auto candidate = draw.lodIndex + offset;
            if (candidate <= maxLod) {
                if (const auto* mesh = meshCache.uploaded({draw.modelAssetId.value(), draw.primitiveIndex, candidate})) {
                    return {mesh, candidate};
                }
            }
        }
    }
    return {};
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
    const auto resourceContext = context_.resources(frame.textureDebug);
    {
        std::ostringstream line;
        line << "renderPath=" << renderFramePathName(frame.renderPath)
             << " samplerPolicyRevision=" << resourceContext.samplerPolicyRevision
             << " forceMaxLod0=" << (resourceContext.forceSamplerMaxLodZero ? "yes" : "no")
             << " anisotropyOverride="
             << renderTextureDebugAnisotropyOverrideName(frame.textureDebug.anisotropyOverride)
             << " mipLodBias=" << resourceContext.textureMipLodBias
             << " mipBiasOverride=" << (frame.textureDebug.overrideMipLodBias ? "yes" : "no");
        lastFrameProfile_.textureDebugLine = line.str();
    }
    if (!rebuildMaterialDescriptorsIfSamplerChanged(resourceContext.samplerPolicyRevision, errorMessage)) {
        return false;
    }

    for (auto& batch : meshBatches_) {
        const auto& draw = *batch.draw;
        const VulkanMeshKey meshKey {draw.modelAssetId.value(), draw.primitiveIndex, draw.lodIndex};
        const auto pendingMeshBytes = meshCache.estimatedUploadBytes(meshKey, *draw.primitive);
        const auto pendingMaterialBytes = estimatedMaterialUploadBytes(textureCache, draw);
        const auto pendingBytes = pendingMeshBytes + pendingMaterialBytes;
        const auto hasStaticUploadWork = pendingBytes > 0U;
        auto preparedLodIndex = draw.lodIndex;
        if (hasStaticUploadWork && !frame.unlimitedStaticUploads) {
            const auto nextBytes = budgetBytes + pendingBytes;
            const auto budgetIsFull = budgetBatches >= std::max(frame.staticUploadBatchBudget, 1U)
                || (budgetBytes > 0U && nextBytes > std::max(frame.staticUploadBudgetBytes, 1ULL));
            if (budgetIsFull) {
                ++lastFrameProfile_.resourceDeferred;
                lastFrameProfile_.resourceDeferredBytes += pendingBytes;
                if (pendingMaterialBytes == 0U) {
                    const auto fallback = uploadedMeshFallback(meshCache, draw);
                    batch.mesh = fallback.mesh;
                    preparedLodIndex = fallback.lodIndex;
                }
                if (batch.mesh == nullptr) {
                    continue;
                }
                ++lastFrameProfile_.resourceFallback;
            } else {
                budgetBytes = std::max<std::uint64_t>(nextBytes, 1U);
                ++budgetBatches;
            }
        }
        if (batch.mesh == nullptr) {
            batch.mesh = meshCache.ensureUploaded(resourceContext, uploads, meshKey, *draw.primitive, errorMessage);
        }
        if (batch.mesh == nullptr) {
            return false;
        }
        const auto textures = uploadMaterialTextureSet(
            resourceContext,
            uploads,
            textureCache,
            draw,
            frame.environment,
            errorMessage);
        if (!textures.complete()) {
            return false;
        }
        batch.materialDescriptor = textureDescriptor(
            frame,
            draw,
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
        auto preparedDraw = draw;
        preparedDraw.lodIndex = preparedLodIndex;
        accumulateRenderLodBreakdown(
            lastFrameProfile_.resourcePreparedLod,
            preparedDraw,
            batch.instanceCount,
            batch.mesh == nullptr ? renderMeshDrawIndexCount(draw) : batch.mesh->indexCount);
        preparedBatches.push_back(batch);
        ++lastFrameProfile_.resourcePrepared;
    }
    meshBatches_ = std::move(preparedBatches);
    return true;
}

} // namespace projectunity::renderer
