#include "VulkanShadowPipeline.hpp"

#include "ShadowDepthFragmentSpv.hpp"
#include "ShadowDepthVertexSpv.hpp"
#include "VulkanMeshPipeline.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>

namespace projectunity::renderer {
namespace {

[[nodiscard]] VkShaderModule createShaderModule(VkDevice device, const auto& words)
{
    VkShaderModuleCreateInfo info {};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = words.size() * sizeof(std::uint32_t);
    info.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan shadow shader module");
    }
    return module;
}

} // namespace

VulkanShadowPipeline::VulkanShadowPipeline(
    VulkanResourceContext context,
    VkDescriptorSetLayout materialLayout,
    VkFormat depthFormat,
    std::uint32_t mapSize)
    : context_(context)
    , depthFormat_(depthFormat)
    , extent_ {mapSize, mapSize}
    , pointCubeExtent_ {std::min(1024U, mapSize), std::min(1024U, mapSize)}
{
    try {
        createRenderPass();
        createShadowTarget();
        createPipeline(materialLayout);
    } catch (...) {
        destroy();
        throw;
    }
}

VulkanShadowPipeline::~VulkanShadowPipeline()
{
    destroy();
}

VkRenderPass VulkanShadowPipeline::renderPass() const noexcept
{
    return renderPass_;
}

VkFramebuffer VulkanShadowPipeline::framebuffer() const noexcept
{
    return framebuffer_;
}

VkPipeline VulkanShadowPipeline::pipeline() const noexcept
{
    return pipeline_;
}

VkPipelineLayout VulkanShadowPipeline::layout() const noexcept
{
    return layout_;
}

VkImageView VulkanShadowPipeline::imageView() const noexcept
{
    return imageView_;
}

VkSampler VulkanShadowPipeline::sampler() const noexcept
{
    return sampler_;
}

VkExtent2D VulkanShadowPipeline::extent() const noexcept
{
    return extent_;
}

VkImage VulkanShadowPipeline::pointCubeImage() const noexcept
{
    return pointCubeImage_;
}

VkImageView VulkanShadowPipeline::pointCubeImageView() const noexcept
{
    return pointCubeImageView_;
}

VkSampler VulkanShadowPipeline::pointCubeSampler() const noexcept
{
    return pointCubeSampler_;
}

VkFramebuffer VulkanShadowPipeline::pointCubeFramebuffer(std::uint32_t faceIndex) const noexcept
{
    return faceIndex < pointCubeFramebuffers_.size() ? pointCubeFramebuffers_[faceIndex] : VK_NULL_HANDLE;
}

VkExtent2D VulkanShadowPipeline::pointCubeExtent() const noexcept
{
    return pointCubeExtent_;
}

void VulkanShadowPipeline::createRenderPass()
{
    VkAttachmentDescription depth {};
    depth.format = depthFormat_;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference depthRef {};
    depthRef.attachment = 0;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkSubpassDescription subpass {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.pDepthStencilAttachment = &depthRef;
    VkSubpassDependency dependency {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    VkSubpassDependency sampleDependency {};
    sampleDependency.srcSubpass = 0;
    sampleDependency.dstSubpass = VK_SUBPASS_EXTERNAL;
    sampleDependency.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    sampleDependency.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    sampleDependency.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    sampleDependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    const std::array<VkSubpassDependency, 2> dependencies {dependency, sampleDependency};

    VkRenderPassCreateInfo info {};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments = &depth;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = static_cast<std::uint32_t>(dependencies.size());
    info.pDependencies = dependencies.data();
    if (vkCreateRenderPass(context_.device, &info, nullptr, &renderPass_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan shadow render pass");
    }
}

void VulkanShadowPipeline::createShadowTarget()
{
    VkImageCreateInfo imageInfo {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = depthFormat_;
    imageInfo.extent = {extent_.width, extent_.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo allocationInfo {};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (vmaCreateImage(context_.allocator, &imageInfo, &allocationInfo, &image_, &allocation_, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan shadow map image");
    }

    VkImageViewCreateInfo viewInfo {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat_;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(context_.device, &viewInfo, nullptr, &imageView_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan shadow map view");
    }

    VkSamplerCreateInfo samplerInfo {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.compareEnable = VK_TRUE;
    samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    samplerInfo.maxLod = 1.0F;
    if (vkCreateSampler(context_.device, &samplerInfo, nullptr, &sampler_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan shadow map sampler");
    }

    VkFramebufferCreateInfo framebufferInfo {};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = renderPass_;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &imageView_;
    framebufferInfo.width = extent_.width;
    framebufferInfo.height = extent_.height;
    framebufferInfo.layers = 1;
    if (vkCreateFramebuffer(context_.device, &framebufferInfo, nullptr, &framebuffer_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan shadow framebuffer");
    }

    VkImageCreateInfo cubeInfo {};
    cubeInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    cubeInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    cubeInfo.imageType = VK_IMAGE_TYPE_2D;
    cubeInfo.format = depthFormat_;
    cubeInfo.extent = {pointCubeExtent_.width, pointCubeExtent_.height, 1};
    cubeInfo.mipLevels = 1;
    cubeInfo.arrayLayers = static_cast<std::uint32_t>(pointCubeFaceViews_.size());
    cubeInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    cubeInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    cubeInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    cubeInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vmaCreateImage(context_.allocator, &cubeInfo, &allocationInfo, &pointCubeImage_, &pointCubeAllocation_, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan point shadow cube image");
    }

    VkImageViewCreateInfo cubeViewInfo {};
    cubeViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    cubeViewInfo.image = pointCubeImage_;
    cubeViewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    cubeViewInfo.format = depthFormat_;
    cubeViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    cubeViewInfo.subresourceRange.levelCount = 1;
    cubeViewInfo.subresourceRange.layerCount = static_cast<std::uint32_t>(pointCubeFaceViews_.size());
    if (vkCreateImageView(context_.device, &cubeViewInfo, nullptr, &pointCubeImageView_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan point shadow cube view");
    }

    for (std::uint32_t face = 0; face < pointCubeFaceViews_.size(); ++face) {
        VkImageViewCreateInfo faceViewInfo {};
        faceViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        faceViewInfo.image = pointCubeImage_;
        faceViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        faceViewInfo.format = depthFormat_;
        faceViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        faceViewInfo.subresourceRange.baseArrayLayer = face;
        faceViewInfo.subresourceRange.levelCount = 1;
        faceViewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(context_.device, &faceViewInfo, nullptr, &pointCubeFaceViews_[face]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan point shadow cube face view");
        }
    }

    VkSamplerCreateInfo cubeSamplerInfo = samplerInfo;
    if (vkCreateSampler(context_.device, &cubeSamplerInfo, nullptr, &pointCubeSampler_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan point shadow cube sampler");
    }

    for (std::uint32_t face = 0; face < pointCubeFramebuffers_.size(); ++face) {
        VkFramebufferCreateInfo cubeFramebufferInfo {};
        cubeFramebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        cubeFramebufferInfo.renderPass = renderPass_;
        cubeFramebufferInfo.attachmentCount = 1;
        cubeFramebufferInfo.pAttachments = &pointCubeFaceViews_[face];
        cubeFramebufferInfo.width = pointCubeExtent_.width;
        cubeFramebufferInfo.height = pointCubeExtent_.height;
        cubeFramebufferInfo.layers = 1;
        if (vkCreateFramebuffer(context_.device, &cubeFramebufferInfo, nullptr, &pointCubeFramebuffers_[face]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan point shadow cube framebuffer");
        }
    }
}

void VulkanShadowPipeline::createPipeline(VkDescriptorSetLayout materialLayout)
{
    const auto vertexModule = createShaderModule(context_.device, shaders::kShadowDepthVertexSpirv);
    const auto fragmentModule = createShaderModule(context_.device, shaders::kShadowDepthFragmentSpirv);
    try {
        std::array<VkPipelineShaderStageCreateInfo, 2> stages {};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertexModule;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragmentModule;
        stages[1].pName = "main";
        std::array<VkVertexInputBindingDescription, 2> bindings {};
        bindings[0].binding = 0;
        bindings[0].stride = sizeof(VulkanGpuVertex);
        bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        bindings[1].binding = 1;
        bindings[1].stride = sizeof(VulkanGpuInstance);
        bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
        std::array<VkVertexInputAttributeDescription, 6> attributes {};
        attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VulkanGpuVertex, position)};
        attributes[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(VulkanGpuVertex, texCoord)};
        attributes[2] = {6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VulkanGpuInstance, model)};
        attributes[3] = {7, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VulkanGpuInstance, model) + sizeof(float) * 4U};
        attributes[4] = {8, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VulkanGpuInstance, model) + sizeof(float) * 8U};
        attributes[5] = {9, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VulkanGpuInstance, model) + sizeof(float) * 12U};
        VkPipelineVertexInputStateCreateInfo vertexInput {};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = static_cast<std::uint32_t>(bindings.size());
        vertexInput.pVertexBindingDescriptions = bindings.data();
        vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
        vertexInput.pVertexAttributeDescriptions = attributes.data();
        VkPipelineInputAssemblyStateCreateInfo inputAssembly {};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport {};
        viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster {};
        raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.depthBiasEnable = VK_TRUE;
        raster.depthBiasConstantFactor = 1.25F;
        raster.depthBiasSlopeFactor = 1.75F;
        raster.lineWidth = 1.0F;
        VkPipelineMultisampleStateCreateInfo multisample {};
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth {};
        depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth.depthTestEnable = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        VkPipelineColorBlendStateCreateInfo blend {};
        blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        constexpr std::array<VkDynamicState, 2> dynamicStates {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic {};
        dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
        dynamic.pDynamicStates = dynamicStates.data();
        VkPushConstantRange push {};
        push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        push.size = sizeof(VulkanDrawPushConstants);
        VkPipelineLayoutCreateInfo layoutInfo {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &materialLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &push;
        if (vkCreatePipelineLayout(context_.device, &layoutInfo, nullptr, &layout_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan shadow pipeline layout");
        }

        VkGraphicsPipelineCreateInfo info {};
        info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        info.stageCount = static_cast<std::uint32_t>(stages.size());
        info.pStages = stages.data();
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &inputAssembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &multisample;
        info.pDepthStencilState = &depth;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic;
        info.layout = layout_;
        info.renderPass = renderPass_;
        if (vkCreateGraphicsPipelines(context_.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan shadow graphics pipeline");
        }
    } catch (...) {
        vkDestroyShaderModule(context_.device, fragmentModule, nullptr);
        vkDestroyShaderModule(context_.device, vertexModule, nullptr);
        throw;
    }
    vkDestroyShaderModule(context_.device, fragmentModule, nullptr);
    vkDestroyShaderModule(context_.device, vertexModule, nullptr);
}

void VulkanShadowPipeline::destroy() noexcept
{
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(context_.device, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(context_.device, layout_, nullptr);
        layout_ = VK_NULL_HANDLE;
    }
    if (framebuffer_ != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(context_.device, framebuffer_, nullptr);
        framebuffer_ = VK_NULL_HANDLE;
    }
    for (auto& framebuffer : pointCubeFramebuffers_) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(context_.device, framebuffer, nullptr);
            framebuffer = VK_NULL_HANDLE;
        }
    }
    if (sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(context_.device, sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
    if (pointCubeSampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(context_.device, pointCubeSampler_, nullptr);
        pointCubeSampler_ = VK_NULL_HANDLE;
    }
    if (imageView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(context_.device, imageView_, nullptr);
        imageView_ = VK_NULL_HANDLE;
    }
    for (auto& view : pointCubeFaceViews_) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(context_.device, view, nullptr);
            view = VK_NULL_HANDLE;
        }
    }
    if (pointCubeImageView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(context_.device, pointCubeImageView_, nullptr);
        pointCubeImageView_ = VK_NULL_HANDLE;
    }
    if (image_ != VK_NULL_HANDLE) {
        vmaDestroyImage(context_.allocator, image_, allocation_);
        image_ = VK_NULL_HANDLE;
        allocation_ = VK_NULL_HANDLE;
    }
    if (pointCubeImage_ != VK_NULL_HANDLE) {
        vmaDestroyImage(context_.allocator, pointCubeImage_, pointCubeAllocation_);
        pointCubeImage_ = VK_NULL_HANDLE;
        pointCubeAllocation_ = VK_NULL_HANDLE;
    }
    if (renderPass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(context_.device, renderPass_, nullptr);
        renderPass_ = VK_NULL_HANDLE;
    }
}

} // namespace projectunity::renderer
