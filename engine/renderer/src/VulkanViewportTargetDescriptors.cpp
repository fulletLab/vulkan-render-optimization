#include "VulkanViewportTarget.hpp"

#include <array>

namespace projectunity::renderer {

std::size_t VulkanMaterialTextureKeyHash::operator()(const VulkanMaterialTextureKey& key) const noexcept
{
    auto hash = static_cast<std::size_t>(key.baseColor);
    const auto mix = [&hash](std::uint64_t value) {
        hash ^= static_cast<std::size_t>(value) + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    };
    mix(key.normal);
    mix(key.metallicRoughness);
    mix(key.occlusion);
    mix(key.emissive);
    mix(key.brdfLut);
    mix(key.irradianceCube);
    mix(key.prefilteredEnvironment);
    return hash;
}

VkDescriptorSet VulkanViewportTarget::textureDescriptor(
    const VulkanTextureHandle& baseColor,
    const VulkanTextureHandle& normal,
    const VulkanTextureHandle& metallicRoughness,
    const VulkanTextureHandle& occlusion,
    const VulkanTextureHandle& emissive,
    const VulkanTextureHandle& brdfLut,
    const VulkanTextureHandle& irradianceCube,
    const VulkanTextureHandle& prefilteredEnvironment,
    std::string* errorMessage)
{
    const VulkanMaterialTextureKey key {
        baseColor.key,
        normal.key,
        metallicRoughness.key,
        occlusion.key,
        emissive.key,
        brdfLut.key,
        irradianceCube.key,
        prefilteredEnvironment.key,
    };
    if (const auto existing = textureDescriptors_.find(key); existing != textureDescriptors_.end()) {
        return existing->second;
    }
    VkDescriptorSetAllocateInfo allocInfo {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = 1;
    const auto layout = meshPipeline_->textureLayout();
    allocInfo.pSetLayouts = &layout;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(context_.device, &allocInfo, &descriptor) != VK_SUCCESS) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to allocate Vulkan texture descriptor set";
        }
        return VK_NULL_HANDLE;
    }
    VkDescriptorBufferInfo frameInfo {};
    frameInfo.buffer = frameData_.buffer();
    frameInfo.range = sizeof(VulkanFrameUniforms);
    const std::array<VkDescriptorImageInfo, 9> imageInfos {{
        {baseColor.sampler, baseColor.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {normal.sampler, normal.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {metallicRoughness.sampler, metallicRoughness.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {occlusion.sampler, occlusion.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {emissive.sampler, emissive.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {shadowPipeline_->sampler(), shadowPipeline_->imageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {brdfLut.sampler, brdfLut.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {irradianceCube.sampler, irradianceCube.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {prefilteredEnvironment.sampler, prefilteredEnvironment.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
    }};
    std::array<VkWriteDescriptorSet, imageInfos.size() + 1U> writes {};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = descriptor;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &frameInfo;
    for (std::uint32_t index = 0; index < imageInfos.size(); ++index) {
        auto& write = writes[index + 1U];
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptor;
        write.dstBinding = index + 1U;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfos[index];
    }
    vkUpdateDescriptorSets(context_.device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
    textureDescriptors_.emplace(key, descriptor);
    return descriptor;
}

} // namespace projectunity::renderer
