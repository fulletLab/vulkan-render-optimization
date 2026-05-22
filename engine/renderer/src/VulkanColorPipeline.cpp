#include "VulkanColorPipeline.hpp"

#include <projectunity/renderer/RendererTypes.hpp>

#include "EditorColorFragmentSpv.hpp"
#include "EditorColorVertexSpv.hpp"

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
        throw std::runtime_error("Failed to create Vulkan editor color shader module");
    }
    return module;
}

} // namespace

VulkanColorPipeline::VulkanColorPipeline(VulkanResourceContext context, VkRenderPass renderPass)
    : context_(context)
{
    try {
        create(renderPass);
    } catch (...) {
        destroy();
        throw;
    }
}

VulkanColorPipeline::~VulkanColorPipeline()
{
    destroy();
}

VkPipeline VulkanColorPipeline::pipeline() const noexcept
{
    return pipeline_;
}

VkPipelineLayout VulkanColorPipeline::layout() const noexcept
{
    return layout_;
}

void VulkanColorPipeline::create(VkRenderPass renderPass)
{
    const auto vertexModule = createShaderModule(context_.device, shaders::kEditorColorVertexSpirv);
    const auto fragmentModule = createShaderModule(context_.device, shaders::kEditorColorFragmentSpirv);
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

        VkVertexInputBindingDescription binding {};
        binding.stride = sizeof(RenderColorVertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        std::array<VkVertexInputAttributeDescription, 2> attributes {};
        attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(RenderColorVertex, position)};
        attributes[1] = {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(RenderColorVertex, color)};
        VkPipelineVertexInputStateCreateInfo vertexInput {};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
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
        raster.lineWidth = 1.0F;
        VkPipelineMultisampleStateCreateInfo multisample {};
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth {};
        depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
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

        VkPushConstantRange push {};
        push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        push.size = sizeof(VulkanColorPushConstants);
        VkPipelineLayoutCreateInfo layoutInfo {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &push;
        if (vkCreatePipelineLayout(context_.device, &layoutInfo, nullptr, &layout_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan editor color pipeline layout");
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
        info.renderPass = renderPass;
        if (vkCreateGraphicsPipelines(context_.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan editor color graphics pipeline");
        }
    } catch (...) {
        vkDestroyShaderModule(context_.device, fragmentModule, nullptr);
        vkDestroyShaderModule(context_.device, vertexModule, nullptr);
        throw;
    }
    vkDestroyShaderModule(context_.device, fragmentModule, nullptr);
    vkDestroyShaderModule(context_.device, vertexModule, nullptr);
}

void VulkanColorPipeline::destroy() noexcept
{
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(context_.device, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(context_.device, layout_, nullptr);
        layout_ = VK_NULL_HANDLE;
    }
}

} // namespace projectunity::renderer
