#include "VulkanMeshPipeline.hpp"

#include "TexturedMeshFragmentSpv.hpp"
#include "TexturedMeshVertexSpv.hpp"

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
        throw std::runtime_error("Failed to create Vulkan mesh shader module");
    }
    return module;
}

} // namespace

VulkanMeshPipeline::VulkanMeshPipeline(VulkanResourceContext context, VkFormat colorFormat)
    : context_(context)
    , depthFormat_(chooseDepthFormat())
{
    try {
        createTextureLayout();
        createRenderPass(colorFormat);
        createPipeline();
    } catch (...) {
        destroy();
        throw;
    }
}

VulkanMeshPipeline::~VulkanMeshPipeline()
{
    destroy();
}

VkRenderPass VulkanMeshPipeline::renderPass() const noexcept
{
    return renderPass_;
}

VkPipeline VulkanMeshPipeline::pipeline() const noexcept
{
    return pipeline_;
}

VkPipeline VulkanMeshPipeline::doubleSidedPipeline() const noexcept
{
    return doubleSidedPipeline_;
}

VkPipeline VulkanMeshPipeline::transparentPipeline() const noexcept
{
    return transparentPipeline_;
}

VkPipeline VulkanMeshPipeline::transparentDoubleSidedPipeline() const noexcept
{
    return transparentDoubleSidedPipeline_;
}

VkPipelineLayout VulkanMeshPipeline::layout() const noexcept
{
    return layout_;
}

VkDescriptorSetLayout VulkanMeshPipeline::textureLayout() const noexcept
{
    return textureLayout_;
}

VkFormat VulkanMeshPipeline::depthFormat() const noexcept
{
    return depthFormat_;
}

VkFormat VulkanMeshPipeline::chooseDepthFormat() const
{
    constexpr std::array<VkFormat, 2> formats {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D24_UNORM_S8_UINT,
    };
    for (const auto format : formats) {
        VkFormatProperties properties {};
        vkGetPhysicalDeviceFormatProperties(context_.physicalDevice, format, &properties);
        if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0U) {
            return format;
        }
    }
    throw std::runtime_error("No supported Vulkan depth format was found");
}

void VulkanMeshPipeline::createTextureLayout()
{
    std::array<VkDescriptorSetLayoutBinding, 11> bindings {};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    for (std::uint32_t index = 1; index < bindings.size(); ++index) {
        bindings[index].binding = index;
        bindings[index].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[index].descriptorCount = 1;
        bindings[index].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo info {};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = static_cast<std::uint32_t>(bindings.size());
    info.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(context_.device, &info, nullptr, &textureLayout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan texture descriptor layout");
    }
}

void VulkanMeshPipeline::createRenderPass(VkFormat colorFormat)
{
    std::array<VkAttachmentDescription, 2> attachments {};
    attachments[0].format = colorFormat;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    attachments[1].format = depthFormat_;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef {};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference depthRef {};
    depthRef.attachment = 1;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkSubpassDescription subpass {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;
    VkSubpassDependency dependency {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = dependency.srcStageMask;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo info {};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = static_cast<std::uint32_t>(attachments.size());
    info.pAttachments = attachments.data();
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dependency;
    if (vkCreateRenderPass(context_.device, &info, nullptr, &renderPass_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan mesh render pass");
    }
}

void VulkanMeshPipeline::createPipeline()
{
    const auto vertexModule = createShaderModule(context_.device, shaders::kTexturedMeshVertexSpirv);
    const auto fragmentModule = createShaderModule(context_.device, shaders::kTexturedMeshFragmentSpirv);
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
        std::array<VkVertexInputAttributeDescription, 10> attributes {};
        attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VulkanGpuVertex, position)};
        attributes[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VulkanGpuVertex, normal)};
        attributes[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(VulkanGpuVertex, texCoord)};
        attributes[3] = {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VulkanGpuVertex, color)};
        attributes[4] = {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VulkanGpuVertex, tangent)};
        attributes[5] = {5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VulkanGpuVertex, materialFactors)};
        attributes[6] = {6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VulkanGpuInstance, model)};
        attributes[7] = {7, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VulkanGpuInstance, model) + sizeof(float) * 4U};
        attributes[8] = {8, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VulkanGpuInstance, model) + sizeof(float) * 8U};
        attributes[9] = {9, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VulkanGpuInstance, model) + sizeof(float) * 12U};
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
        raster.lineWidth = 1.0F;
        raster.cullMode = VK_CULL_MODE_BACK_BIT;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        VkPipelineMultisampleStateCreateInfo multisample {};
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth {};
        depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth.depthTestEnable = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS;
        VkPipelineColorBlendAttachmentState blendAttachment {};
        blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
            | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blendAttachment.blendEnable = VK_TRUE;
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        VkPipelineColorBlendStateCreateInfo blend {};
        blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.attachmentCount = 1;
        blend.pAttachments = &blendAttachment;
        constexpr std::array<VkDynamicState, 2> dynamicStates {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic {};
        dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
        dynamic.pDynamicStates = dynamicStates.data();

        VkPushConstantRange pushRange {};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.size = sizeof(VulkanDrawPushConstants);
        VkPipelineLayoutCreateInfo layoutInfo {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &textureLayout_;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        if (vkCreatePipelineLayout(context_.device, &layoutInfo, nullptr, &layout_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan mesh pipeline layout");
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
        info.subpass = 0;
        if (vkCreateGraphicsPipelines(context_.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan mesh graphics pipeline");
        }
        raster.cullMode = VK_CULL_MODE_NONE;
        depth.depthWriteEnable = VK_TRUE;
        if (vkCreateGraphicsPipelines(context_.device, VK_NULL_HANDLE, 1, &info, nullptr, &doubleSidedPipeline_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan double-sided mesh graphics pipeline");
        }
        raster.cullMode = VK_CULL_MODE_BACK_BIT;
        depth.depthWriteEnable = VK_FALSE;
        if (vkCreateGraphicsPipelines(context_.device, VK_NULL_HANDLE, 1, &info, nullptr, &transparentPipeline_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan transparent mesh graphics pipeline");
        }
        raster.cullMode = VK_CULL_MODE_NONE;
        if (vkCreateGraphicsPipelines(context_.device, VK_NULL_HANDLE, 1, &info, nullptr, &transparentDoubleSidedPipeline_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan transparent double-sided mesh graphics pipeline");
        }
    } catch (...) {
        vkDestroyShaderModule(context_.device, fragmentModule, nullptr);
        vkDestroyShaderModule(context_.device, vertexModule, nullptr);
        throw;
    }
    vkDestroyShaderModule(context_.device, fragmentModule, nullptr);
    vkDestroyShaderModule(context_.device, vertexModule, nullptr);
}

void VulkanMeshPipeline::destroy() noexcept
{
    if (transparentDoubleSidedPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(context_.device, transparentDoubleSidedPipeline_, nullptr);
        transparentDoubleSidedPipeline_ = VK_NULL_HANDLE;
    }
    if (transparentPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(context_.device, transparentPipeline_, nullptr);
        transparentPipeline_ = VK_NULL_HANDLE;
    }
    if (doubleSidedPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(context_.device, doubleSidedPipeline_, nullptr);
        doubleSidedPipeline_ = VK_NULL_HANDLE;
    }
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(context_.device, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(context_.device, layout_, nullptr);
        layout_ = VK_NULL_HANDLE;
    }
    if (renderPass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(context_.device, renderPass_, nullptr);
        renderPass_ = VK_NULL_HANDLE;
    }
    if (textureLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(context_.device, textureLayout_, nullptr);
        textureLayout_ = VK_NULL_HANDLE;
    }
}

} // namespace projectunity::renderer
