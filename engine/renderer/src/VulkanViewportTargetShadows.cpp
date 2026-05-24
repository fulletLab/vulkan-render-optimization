#include "VulkanViewportTarget.hpp"
#include "VulkanDebugLabels.hpp"
#include "VulkanMaterialTextureSet.hpp"

#include <projectunity/renderer/RenderDrawOrdering.hpp>
#include <projectunity/renderer/RenderShadowSetup.hpp>

#include <algorithm>
#include <array>
#include <chrono>

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
    VkClearValue clear {};
    clear.depthStencil = {1.0F, 0};
    gpuProfiler_.write(commandBuffer_, VulkanGpuFrameTimestamp::ShadowStart, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
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
    const auto shadowViewCount = renderableShadowViewCount(frame);
    lastFrameProfile_.shadowViewCount = shadowViewCount;
    if (shadowViewCount == 0U) {
        vkCmdEndRenderPass(commandBuffer_);
        if (!pointShadowCubeReadable_) {
            transitionPointCubeToReadable(commandBuffer_, shadowPipeline_->pointCubeImage());
            pointShadowCubeReadable_ = true;
        }
        gpuProfiler_.write(commandBuffer_, VulkanGpuFrameTimestamp::ShadowEnd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
        lastFrameProfile_.shadowRecordCpuTimeUs += elapsedUs(passStart);
        return true;
    }

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
        setShadowViewport(
            commandBuffer_,
            shadowRenderArea(extent, viewIndex, viewCount));
        const auto& shadowViewProjection = shadowViewProjectionFor(frame, viewIndex);
        for (const auto& batch : meshBatches_) {
            const auto& draw = *batch.draw;
            if (isTransparentMeshDraw(draw)) {
                continue;
            }
            if (!shadowSphereIntersects(shadowViewProjection, draw.worldBoundsCenter, draw.worldBoundsRadius)) {
                ++lastFrameProfile_.shadowCulledBatchCount;
                continue;
            }
            ++lastFrameProfile_.shadowBatchCount;
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
            const std::array<VkBuffer, 2> vertexBuffers {mesh->vertices, meshInstanceBuffer_.buffer()};
            const std::array<VkDeviceSize, 2> vertexOffsets {vertexOffset, instanceOffset};
            vkCmdBindVertexBuffers(
                commandBuffer_,
                0,
                static_cast<std::uint32_t>(vertexBuffers.size()),
                vertexBuffers.data(),
                vertexOffsets.data());
            vkCmdBindIndexBuffer(commandBuffer_, mesh->indices.buffer(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdBindDescriptorSets(
                commandBuffer_,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                shadowPipeline_->layout(),
                0,
                1,
                &descriptor,
                0,
                nullptr);
            vkCmdPushConstants(
                commandBuffer_,
                shadowPipeline_->layout(),
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(VulkanDrawPushConstants),
                &push);
            vkCmdDrawIndexed(commandBuffer_, mesh->indexCount, batch.instanceCount, 0, 0, 0);
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
    gpuProfiler_.write(commandBuffer_, VulkanGpuFrameTimestamp::ShadowEnd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    lastFrameProfile_.shadowRecordCpuTimeUs += elapsedUs(passStart);
    return true;
}

} // namespace projectunity::renderer
