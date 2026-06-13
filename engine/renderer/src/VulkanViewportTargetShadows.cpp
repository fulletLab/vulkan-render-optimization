#include "VulkanViewportTarget.hpp"
#include "VulkanDebugLabels.hpp"
#include "VulkanMaterialTextureSet.hpp"

#include <projectunity/renderer/RenderDrawOrdering.hpp>
#include <projectunity/renderer/RenderShadowCache.hpp>
#include <projectunity/renderer/RenderShadowSetup.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <string>

namespace projectunity::renderer {
namespace {

[[nodiscard]] std::uint64_t elapsedUs(std::chrono::steady_clock::time_point start) noexcept
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count());
}

[[nodiscard]] std::uint32_t renderableShadowViewCount(const RenderFrame& frame) noexcept
{
    if (!frame.shadowsEnabled) {
        return 0;
    }
    if (frame.shadowMode == RenderShadowMode::DirectionalCascades) {
        return std::clamp<std::uint32_t>(
            frame.shadowViewCount == 0U ? 1U : frame.shadowViewCount,
            1U,
            static_cast<std::uint32_t>(kMaxShadowCascades));
    }
    if (frame.shadowMode == RenderShadowMode::PointCubemap) {
        return 6U;
    }
    return 1U;
}

[[nodiscard]] const RenderMatrix4& shadowViewProjectionFor(const RenderFrame& frame, std::uint32_t viewIndex) noexcept
{
    if (viewIndex < frame.shadowViewCount) {
        return frame.shadowViewProjections[viewIndex];
    }
    return frame.shadowViewProjection;
}

[[nodiscard]] VkRect2D shadowRenderArea(VkExtent2D extent, std::uint32_t viewIndex, std::uint32_t viewCount) noexcept
{
    VkRect2D area {};
    if (viewCount <= 1U) {
        area.extent = extent;
        return area;
    }
    const auto tileWidth = extent.width / 2U;
    const auto tileHeight = extent.height / 2U;
    area.offset.x = static_cast<std::int32_t>((viewIndex % 2U) * tileWidth);
    area.offset.y = static_cast<std::int32_t>((viewIndex / 2U) * tileHeight);
    area.extent = {tileWidth, tileHeight};
    return area;
}

void setShadowViewport(VkCommandBuffer commandBuffer, VkRect2D area) noexcept
{
    VkViewport viewport {};
    viewport.x = static_cast<float>(area.offset.x);
    viewport.y = static_cast<float>(area.offset.y);
    viewport.width = static_cast<float>(area.extent.width);
    viewport.height = static_cast<float>(area.extent.height);
    viewport.maxDepth = 1.0F;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &area);
}

void transitionPointCubeToReadable(VkCommandBuffer commandBuffer, VkImage image) noexcept
{
    VkImageMemoryBarrier barrier {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 6;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier);
}

} // namespace

bool VulkanViewportTarget::recordShadowPass(
    const RenderFrame& frame,
    VulkanUploadContext& uploads,
    VulkanTextureCache& textureCache,
    std::string* errorMessage)
{
    const auto passStart = std::chrono::steady_clock::now();
    const auto shadowViewCount = renderableShadowViewCount(frame);
    lastFrameProfile_.shadowViewCount = shadowViewCount;
    lastFrameProfile_.shadowUpdateMode = frame.shadowUpdateMode;
    lastFrameProfile_.shadowCandidateInstances = frame.shadowCandidateInstances;
    lastFrameProfile_.shadowPolicyRejectedInstances = frame.shadowPolicyRejectedInstances;
    lastFrameProfile_.shadowVisibleInstances = frame.shadowVisibleInstances;
    lastFrameProfile_.shadowOnlyCandidateInstances = frame.shadowOnlyCandidateInstances;
    lastFrameProfile_.shadowOnlyRejectedInstances = frame.shadowOnlyRejectedInstances;
    const auto hasShadowCaster = std::any_of(shadowMeshBatches_.begin(), shadowMeshBatches_.end(), [](const VulkanMeshDrawBatch& batch) {
        return batch.draw != nullptr
            && batch.draw->castsShadow
            && !isTransparentMeshDraw(*batch.draw);
    });
    gpuProfiler_.write(commandBuffer_, VulkanGpuFrameTimestamp::ShadowStart, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    if (shadowViewCount == 0U || !hasShadowCaster) {
        gpuProfiler_.write(commandBuffer_, VulkanGpuFrameTimestamp::ShadowEnd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
        if (shadowViewCount == 0U) {
            lastFrameProfile_.shadowGpuTimeUs = 0;
        }
        lastFrameProfile_.shadowRecordCpuTimeUs += elapsedUs(passStart);
        return true;
    }
    const auto reuseFrozenMap = frame.shadowUpdateMode == RenderShadowUpdateMode::Frozen && shadowMapValid_;
    if (reuseFrozenMap) {
        gpuProfiler_.write(commandBuffer_, VulkanGpuFrameTimestamp::ShadowEnd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
        lastFrameProfile_.shadowRecordCpuTimeUs += elapsedUs(passStart);
        return true;
    }
    const auto contentSignature = renderShadowContentSignature(frame);
    const auto reuseUnchangedLiveMap = frame.shadowUpdateMode == RenderShadowUpdateMode::Live
        && shadowMapValid_
        && shadowContentSignature_ == contentSignature;
    if (reuseUnchangedLiveMap) {
        gpuProfiler_.write(commandBuffer_, VulkanGpuFrameTimestamp::ShadowEnd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
        lastFrameProfile_.shadowRecordCpuTimeUs += elapsedUs(passStart);
        return true;
    }
    VkClearValue clear {};
    clear.depthStencil = {1.0F, 0};
    const auto beginShadowRenderPass = [&](VkFramebuffer framebuffer, VkExtent2D extent) {
        VkRenderPassBeginInfo renderPass {};
        renderPass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPass.renderPass = shadowPipeline_->renderPass();
        renderPass.framebuffer = framebuffer;
        renderPass.renderArea.extent = extent;
        renderPass.clearValueCount = 1;
        renderPass.pClearValues = &clear;
        vkCmdBeginRenderPass(commandBuffer_, &renderPass, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline_->pipeline());
    };
    beginShadowRenderPass(shadowPipeline_->framebuffer(), shadowPipeline_->extent());
    VulkanScopedLabel shadowLabel(
        beginDebugLabel_,
        endDebugLabel_,
        commandBuffer_,
        "ProjectUnity Shadow Pass",
        {0.22F, 0.26F, 0.92F, 1.0F});

    VkDescriptorSet opaqueShadowDescriptor = VK_NULL_HANDLE;
    const auto getOpaqueShadowDescriptor = [&]() -> VkDescriptorSet {
        if (opaqueShadowDescriptor != VK_NULL_HANDLE) {
            return opaqueShadowDescriptor;
        }
        const RenderMeshDraw defaultDraw {};
        const auto textures = uploadMaterialTextureSet(
            context_.resources(),
            uploads,
            textureCache,
            defaultDraw,
            frame.environment,
            errorMessage);
        if (!textures.complete()) {
            return VK_NULL_HANDLE;
        }
        opaqueShadowDescriptor = textureDescriptor(
            *textures.baseColor,
            *textures.normal,
            *textures.metallicRoughness,
            *textures.occlusion,
            *textures.emissive,
            *textures.brdfLut,
            *textures.irradianceCube,
            *textures.prefilteredEnvironment,
            errorMessage);
        return opaqueShadowDescriptor;
    };

    const auto drawShadowView = [&](std::uint32_t viewIndex, std::uint32_t viewCount, VkExtent2D extent) {
        const auto viewLabelName = frame.shadowMode == RenderShadowMode::PointCubemap
            ? std::string("ProjectUnity Shadow Cube Face ") + std::to_string(viewIndex)
            : std::string("ProjectUnity Shadow Cascade ") + std::to_string(viewIndex);
        VulkanScopedLabel shadowViewLabel(
            beginDebugLabel_,
            endDebugLabel_,
            commandBuffer_,
            viewLabelName.c_str(),
            {0.32F, 0.34F, 0.95F, 1.0F});
        setShadowViewport(
            commandBuffer_,
            shadowRenderArea(extent, viewIndex, viewCount));
        vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline_->pipeline());
        auto boundShadowPipeline = shadowPipeline_->pipeline();
        const auto& shadowViewProjection = shadowViewProjectionFor(frame, viewIndex);
        for (const auto& batch : shadowMeshBatches_) {
            const auto& draw = *batch.draw;
            if (!draw.castsShadow || isTransparentMeshDraw(draw)) {
                continue;
            }
            if (!shadowSphereIntersects(shadowViewProjection, batch.worldBoundsCenter, batch.worldBoundsRadius)) {
                ++lastFrameProfile_.shadowCulledBatchCount;
                continue;
            }
            const auto* mesh = batch.mesh;
            if (mesh == nullptr) {
                return false;
            }
            const auto needsAlphaTexture = draw.material != nullptr
                && draw.material->alphaMode == assets::MaterialAlphaMode::Mask;
            const auto descriptor = needsAlphaTexture ? batch.materialDescriptor : getOpaqueShadowDescriptor();
            if (descriptor == VK_NULL_HANDLE) {
                return false;
            }
            const auto doubleSided = draw.material != nullptr && draw.material->doubleSided;
            const auto shadowPipeline = (doubleSided || draw.flipsWinding)
                ? shadowPipeline_->doubleSidedPipeline()
                : shadowPipeline_->pipeline();
            if (shadowPipeline != boundShadowPipeline) {
                vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline);
                boundShadowPipeline = shadowPipeline;
            }
            VulkanDrawPushConstants push;
            push.modelMatrix = draw.modelMatrix.values;
            if (draw.material != nullptr) {
                push.baseColor = draw.material->baseColor;
                push.pbrFactors = {
                    draw.material->metallicFactor,
                    draw.material->roughnessFactor,
                    draw.material->normalScale,
                    static_cast<float>(draw.material->alphaMode),
                };
                push.emissiveColor = {
                    draw.material->emissiveColor[0],
                    draw.material->emissiveColor[1],
                    draw.material->emissiveColor[2],
                    draw.material->alphaCutoff,
                };
            }
            push.materialExtras[3] = static_cast<float>(viewIndex);
            const VkDeviceSize vertexOffset = 0;
            const VkDeviceSize instanceOffset = static_cast<VkDeviceSize>(batch.firstInstance) * sizeof(VulkanGpuInstance);
            const std::array<VkBuffer, 2> vertexBuffers {mesh->vertices, shadowMeshInstanceBuffer_.buffer()};
            const std::array<VkDeviceSize, 2> vertexOffsets {vertexOffset, instanceOffset};
            vkCmdBindVertexBuffers(
                commandBuffer_,
                0,
                static_cast<std::uint32_t>(vertexBuffers.size()),
                vertexBuffers.data(),
                vertexOffsets.data());
            ++lastFrameProfile_.vkBindVertex;
            vkCmdBindIndexBuffer(commandBuffer_, mesh->indices.buffer(), 0, VK_INDEX_TYPE_UINT32);
            ++lastFrameProfile_.vkBindIndex;
            vkCmdBindDescriptorSets(
                commandBuffer_,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                shadowPipeline_->layout(),
                0,
                1,
                &descriptor,
                0,
                nullptr);
            ++lastFrameProfile_.vkBindDescriptors;
            vkCmdPushConstants(
                commandBuffer_,
                shadowPipeline_->layout(),
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(VulkanDrawPushConstants),
                &push);
            vkCmdDrawIndexed(commandBuffer_, mesh->indexCount, batch.instanceCount, 0, 0, 0);
            ++lastFrameProfile_.vkDrawIndexed;
            ++lastFrameProfile_.shadowBatchCount;
            ++lastFrameProfile_.shadowCastersSubmitted;
            ++lastFrameProfile_.shadowBatchesSubmitted;
            lastFrameProfile_.shadowInstancesSubmitted += batch.instanceCount;
            const auto submittedTriangles = static_cast<std::uint64_t>(mesh->indexCount / 3U) * batch.instanceCount;
            lastFrameProfile_.shadowTrianglesSubmitted += submittedTriangles;
            lastFrameProfile_.trianglesSubmitted += submittedTriangles;
        }
        return true;
    };

    if (frame.shadowMode == RenderShadowMode::PointCubemap) {
        vkCmdEndRenderPass(commandBuffer_);
        for (std::uint32_t face = 0; face < shadowViewCount; ++face) {
            const auto framebuffer = shadowPipeline_->pointCubeFramebuffer(face);
            if (framebuffer == VK_NULL_HANDLE) {
                return false;
            }
            beginShadowRenderPass(framebuffer, shadowPipeline_->pointCubeExtent());
            if (!drawShadowView(face, 1U, shadowPipeline_->pointCubeExtent())) {
                vkCmdEndRenderPass(commandBuffer_);
                return false;
            }
            vkCmdEndRenderPass(commandBuffer_);
        }
        pointShadowCubeReadable_ = true;
    } else {
        for (std::uint32_t viewIndex = 0; viewIndex < shadowViewCount; ++viewIndex) {
            if (!drawShadowView(viewIndex, shadowViewCount, shadowPipeline_->extent())) {
                vkCmdEndRenderPass(commandBuffer_);
                return false;
            }
        }
        vkCmdEndRenderPass(commandBuffer_);
        if (!pointShadowCubeReadable_) {
            transitionPointCubeToReadable(commandBuffer_, shadowPipeline_->pointCubeImage());
            pointShadowCubeReadable_ = true;
        }
    }
    shadowMapValid_ = true;
    shadowContentSignature_ = contentSignature;
    lastFrameProfile_.shadowMapUpdated = true;
    gpuProfiler_.write(commandBuffer_, VulkanGpuFrameTimestamp::ShadowEnd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    lastFrameProfile_.shadowRecordCpuTimeUs += elapsedUs(passStart);
    return true;
}

} // namespace projectunity::renderer
