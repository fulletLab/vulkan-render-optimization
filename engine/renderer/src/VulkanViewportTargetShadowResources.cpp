#include "VulkanViewportTarget.hpp"
#include "VulkanMaterialTextureSet.hpp"

#include <projectunity/renderer/RenderDrawOrdering.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <span>
#include <vector>

namespace projectunity::renderer {
namespace {

constexpr std::uint64_t kShadowUploadBudgetBytes = 12ULL * 1024ULL * 1024ULL;
constexpr std::uint32_t kShadowUploadBudgetBatches = 8U;

[[nodiscard]] std::span<const std::byte> instanceBytes(const std::vector<VulkanGpuInstance>& instances)
{
    return {
        reinterpret_cast<const std::byte*>(instances.data()),
        instances.size() * sizeof(VulkanGpuInstance),
    };
}

[[nodiscard]] bool canBatchShadow(const RenderMeshDraw& lhs, const RenderMeshDraw& rhs) noexcept
{
    return lhs.modelAssetId == rhs.modelAssetId
        && lhs.primitiveIndex == rhs.primitiveIndex
        && lhs.lodIndex == rhs.lodIndex
        && lhs.renderChunkId == rhs.renderChunkId
        && lhs.material == rhs.material
        && lhs.baseColorTexture == rhs.baseColorTexture
        && lhs.flipsWinding == rhs.flipsWinding
        && lhs.castsShadow == rhs.castsShadow
        && isTransparentMeshDraw(lhs) == isTransparentMeshDraw(rhs);
}

[[nodiscard]] float distance(std::array<float, 3> lhs, std::array<float, 3> rhs) noexcept
{
    const auto dx = lhs[0] - rhs[0];
    const auto dy = lhs[1] - rhs[1];
    const auto dz = lhs[2] - rhs[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

void resetBatchBounds(VulkanMeshDrawBatch& batch, const RenderMeshDraw& draw) noexcept
{
    batch.worldBoundsCenter = draw.worldBoundsCenter;
    batch.worldBoundsRadius = std::max(draw.worldBoundsRadius, 0.0F);
}

void includeBatchBounds(VulkanMeshDrawBatch& batch, const RenderMeshDraw& draw) noexcept
{
    const auto drawRadius = std::max(draw.worldBoundsRadius, 0.0F);
    if (drawRadius <= 0.0F) {
        return;
    }
    if (batch.worldBoundsRadius <= 0.0F) {
        resetBatchBounds(batch, draw);
        return;
    }

    const auto centerDistance = distance(batch.worldBoundsCenter, draw.worldBoundsCenter);
    if (batch.worldBoundsRadius >= centerDistance + drawRadius) {
        return;
    }
    if (drawRadius >= centerDistance + batch.worldBoundsRadius) {
        resetBatchBounds(batch, draw);
        return;
    }
    if (centerDistance <= 0.00001F) {
        batch.worldBoundsRadius = std::max(batch.worldBoundsRadius, drawRadius);
        return;
    }

    const auto newRadius = (centerDistance + batch.worldBoundsRadius + drawRadius) * 0.5F;
    const auto centerShift = (newRadius - batch.worldBoundsRadius) / centerDistance;
    batch.worldBoundsCenter = {
        batch.worldBoundsCenter[0] + (draw.worldBoundsCenter[0] - batch.worldBoundsCenter[0]) * centerShift,
        batch.worldBoundsCenter[1] + (draw.worldBoundsCenter[1] - batch.worldBoundsCenter[1]) * centerShift,
        batch.worldBoundsCenter[2] + (draw.worldBoundsCenter[2] - batch.worldBoundsCenter[2]) * centerShift,
    };
    batch.worldBoundsRadius = newRadius;
}

[[nodiscard]] bool needsAlphaShadowDescriptor(const RenderMeshDraw& draw) noexcept
{
    return draw.material != nullptr && draw.material->alphaMode == assets::MaterialAlphaMode::Mask;
}

[[nodiscard]] std::uint64_t estimatedShadowMaterialUploadBytes(
    VulkanTextureCache& textureCache,
    const RenderMeshDraw& draw) noexcept
{
    if (!needsAlphaShadowDescriptor(draw)) {
        return 0;
    }
    return textureCache.estimatedUploadBytes(draw.baseColorTexture, VulkanTextureColorSpace::Srgb)
        + textureCache.estimatedNormalUploadBytes(draw.normalTexture)
        + textureCache.estimatedUploadBytes(draw.metallicRoughnessTexture, VulkanTextureColorSpace::Linear)
        + textureCache.estimatedUploadBytes(draw.occlusionTexture, VulkanTextureColorSpace::Linear)
        + textureCache.estimatedUploadBytes(draw.emissiveTexture, VulkanTextureColorSpace::Srgb)
        + textureCache.estimatedBrdfLutUploadBytes();
}

} // namespace

bool VulkanViewportTarget::buildShadowMeshBatches(
    std::span<const RenderMeshDraw> draws,
    std::string* errorMessage)
{
    orderMeshDraws(draws, orderedShadowMeshDraws_);
    shadowMeshInstances_.clear();
    shadowMeshBatches_.clear();
    shadowMeshInstances_.reserve(orderedShadowMeshDraws_.size());
    shadowMeshBatches_.reserve(orderedShadowMeshDraws_.size());
    for (const auto* draw : orderedShadowMeshDraws_) {
        if (draw == nullptr || !draw->castsShadow || isTransparentMeshDraw(*draw)) {
            continue;
        }
        VulkanGpuInstance instance;
        std::copy(draw->modelMatrix.values.begin(), draw->modelMatrix.values.end(), instance.model);
        const auto instanceIndex = static_cast<std::uint32_t>(shadowMeshInstances_.size());
        shadowMeshInstances_.push_back(instance);
        if (!shadowMeshBatches_.empty() && canBatchShadow(*shadowMeshBatches_.back().draw, *draw)) {
            ++shadowMeshBatches_.back().instanceCount;
            includeBatchBounds(shadowMeshBatches_.back(), *draw);
        } else {
            VulkanMeshDrawBatch batch;
            batch.draw = draw;
            batch.firstInstance = instanceIndex;
            batch.instanceCount = 1U;
            resetBatchBounds(batch, *draw);
            shadowMeshBatches_.push_back(batch);
        }
    }
    if (shadowMeshInstances_.empty()) {
        shadowMeshInstanceBuffer_.destroy();
        return true;
    }
    return shadowMeshInstanceBuffer_.writeMapped(
        context_.resources(),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        instanceBytes(shadowMeshInstances_),
        errorMessage);
}

bool VulkanViewportTarget::prepareShadowMeshBatchResources(
    const RenderFrame& frame,
    VulkanUploadContext& uploads,
    VulkanMeshCache& meshCache,
    VulkanTextureCache& textureCache,
    std::string* errorMessage)
{
    std::vector<VulkanMeshDrawBatch> preparedBatches;
    preparedBatches.reserve(shadowMeshBatches_.size());
    std::uint64_t budgetBytes = 0;
    std::uint32_t budgetBatches = 0;

    for (auto& batch : shadowMeshBatches_) {
        const auto& draw = *batch.draw;
        const VulkanMeshKey meshKey {draw.modelAssetId.value(), draw.primitiveIndex, draw.lodIndex};
        const auto pendingBytes = meshCache.estimatedUploadBytes(meshKey, *draw.primitive)
            + estimatedShadowMaterialUploadBytes(textureCache, draw);
        if (pendingBytes > 0U) {
            const auto nextBytes = budgetBytes + pendingBytes;
            const auto budgetIsFull = budgetBatches >= kShadowUploadBudgetBatches
                || (budgetBytes > 0U && nextBytes > kShadowUploadBudgetBytes);
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
        if (needsAlphaShadowDescriptor(draw)) {
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
        preparedBatches.push_back(batch);
        ++lastFrameProfile_.resourcePrepared;
    }
    shadowMeshBatches_ = std::move(preparedBatches);
    return true;
}

} // namespace projectunity::renderer
