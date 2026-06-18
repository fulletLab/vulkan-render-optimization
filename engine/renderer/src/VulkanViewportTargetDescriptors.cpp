#include "VulkanViewportTarget.hpp"

#include <projectunity/core/Log.hpp>

#include <array>
#include <cstdint>
#include <sstream>
#include <string>

namespace projectunity::renderer {
namespace {

[[nodiscard]] const char* filterName(VkFilter filter) noexcept
{
    return filter == VK_FILTER_NEAREST ? "VK_FILTER_NEAREST" : "VK_FILTER_LINEAR";
}

[[nodiscard]] const char* mipmapModeName(VkSamplerMipmapMode mode) noexcept
{
    return mode == VK_SAMPLER_MIPMAP_MODE_NEAREST
        ? "VK_SAMPLER_MIPMAP_MODE_NEAREST"
        : "VK_SAMPLER_MIPMAP_MODE_LINEAR";
}

[[nodiscard]] const char* addressModeName(VkSamplerAddressMode mode) noexcept
{
    switch (mode) {
    case VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT: return "VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT";
    case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE: return "VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE";
    case VK_SAMPLER_ADDRESS_MODE_REPEAT: return "VK_SAMPLER_ADDRESS_MODE_REPEAT";
    default: return "VK_SAMPLER_ADDRESS_MODE_REPEAT";
    }
}

[[nodiscard]] const char* colorSpaceName(VulkanTextureColorSpace colorSpace) noexcept
{
    return colorSpace == VulkanTextureColorSpace::Srgb ? "sRGB" : "linear";
}

[[nodiscard]] std::uint64_t mixHash(std::uint64_t seed, std::uint64_t value) noexcept
{
    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U));
}

[[nodiscard]] std::string textureName(const assets::TextureAsset* texture)
{
    if (texture == nullptr) {
        return "<fallback>";
    }
    if (!texture->name.empty()) {
        return texture->name;
    }
    return texture->id.isValid() ? std::to_string(texture->id.value()) : "<unnamed>";
}

[[nodiscard]] std::string samplerDebugLine(
    const RenderFrame& frame,
    const RenderMeshDraw& draw,
    const VulkanTextureHandle& handle,
    VkDescriptorSet descriptor)
{
    const auto& sampler = handle.samplerState;
    std::ostringstream line;
    line << "texture=" << textureName(draw.baseColorTexture)
         << " role=" << (draw.generatedTerrainModel ? "terrain/baseColor" : "baseColor")
         << " resolution=" << handle.width << "x" << handle.height
         << " mipLevels=" << handle.mipLevels
         << " textureHandleKey=" << handle.key
         << " sampler=" << reinterpret_cast<std::uintptr_t>(handle.sampler)
         << " descriptor=" << reinterpret_cast<std::uintptr_t>(descriptor)
         << " magFilter=" << filterName(sampler.magFilter)
         << " minFilter=" << filterName(sampler.minFilter)
         << " mipmapMode=" << mipmapModeName(sampler.mipmapMode)
         << " anisotropy=" << (sampler.anisotropyEnable == VK_TRUE ? "on" : "off")
         << " maxAnisotropy=" << sampler.maxAnisotropy
         << " minLod=" << sampler.minLod
         << " maxLod=" << sampler.maxLod
         << " mipLodBias=" << sampler.mipLodBias
         << " wrapU=" << addressModeName(sampler.addressModeU)
         << " wrapV=" << addressModeName(sampler.addressModeV)
         << " colorSpace=" << colorSpaceName(handle.colorSpace)
         << " renderPath=" << renderFramePathName(frame.renderPath)
         << " materialIndex=" << draw.materialIndex
         << " material=" << reinterpret_cast<std::uintptr_t>(draw.material)
         << " modelAssetId=" << (draw.modelAssetId.isValid() ? draw.modelAssetId.value() : 0ULL)
         << " generatedTerrain=" << (draw.generatedTerrainModel ? "yes" : "no")
         << " samplerPolicyRevision=" << handle.samplerPolicyRevision
         << " samplerRecreatedAfterSettingsChange="
         << (handle.samplerRecreatedAfterSettingsChange ? "yes" : "no")
         << " debugForceMaxLod0=" << (frame.textureDebug.forceMaxLodZero ? "yes" : "no")
         << " debugAnisotropyOverride="
         << renderTextureDebugAnisotropyOverrideName(frame.textureDebug.anisotropyOverride)
         << " debugMipBiasOverride=" << (frame.textureDebug.overrideMipLodBias ? "yes" : "no");
    return line.str();
}

} // namespace

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

bool VulkanViewportTarget::rebuildMaterialDescriptorsIfSamplerChanged(
    std::uint64_t samplerPolicyRevision,
    std::string* errorMessage)
{
    if (materialDescriptorSamplerRevision_ == samplerPolicyRevision) {
        return true;
    }
    textureDescriptors_.clear();
    if (vkResetDescriptorPool(context_.device, descriptorPool_, 0) != VK_SUCCESS) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to reset Vulkan material descriptor pool after sampler policy change";
        }
        return false;
    }
    materialDescriptorSamplerRevision_ = samplerPolicyRevision;
    return true;
}

VkDescriptorSet VulkanViewportTarget::textureDescriptor(
    const RenderFrame& frame,
    const RenderMeshDraw& draw,
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
    const bool environmentChanged = descriptorEnvironmentKeyValid_
        && (descriptorIrradianceKey_ != irradianceCube.key
            || descriptorPrefilteredEnvironmentKey_ != prefilteredEnvironment.key);
    if (environmentChanged) {
        textureDescriptors_.clear();
        if (vkResetDescriptorPool(context_.device, descriptorPool_, 0) != VK_SUCCESS) {
            if (errorMessage != nullptr) {
                *errorMessage = "Failed to reset Vulkan texture descriptor pool after environment change";
            }
            return VK_NULL_HANDLE;
        }
    }
    descriptorIrradianceKey_ = irradianceCube.key;
    descriptorPrefilteredEnvironmentKey_ = prefilteredEnvironment.key;
    descriptorEnvironmentKeyValid_ = true;
    if (const auto existing = textureDescriptors_.find(key); existing != textureDescriptors_.end()) {
        if (draw.generatedTerrainModel || frame.renderPath == RenderFramePath::GameRuntime) {
            const auto line = samplerDebugLine(frame, draw, baseColor, existing->second);
            if (lastFrameProfile_.samplerDebugLine.empty() || draw.generatedTerrainModel) {
                lastFrameProfile_.samplerDebugLine = line;
            }
            if (frame.renderPath == RenderFramePath::GameRuntime
                && (lastFrameProfile_.runtimeSamplerDebugLine.empty() || draw.generatedTerrainModel)) {
                lastFrameProfile_.runtimeSamplerDebugLine = line;
            }
        }
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
    const std::array<VkDescriptorImageInfo, 10> imageInfos {{
        {baseColor.sampler, baseColor.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {normal.sampler, normal.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {metallicRoughness.sampler, metallicRoughness.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {occlusion.sampler, occlusion.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {emissive.sampler, emissive.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {shadowPipeline_->sampler(), shadowPipeline_->imageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {brdfLut.sampler, brdfLut.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {irradianceCube.sampler, irradianceCube.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {prefilteredEnvironment.sampler, prefilteredEnvironment.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {shadowPipeline_->pointCubeSampler(), shadowPipeline_->pointCubeImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
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
    if (draw.generatedTerrainModel || frame.renderPath == RenderFramePath::GameRuntime) {
        const auto line = samplerDebugLine(frame, draw, baseColor, descriptor);
        if (lastFrameProfile_.samplerDebugLine.empty() || draw.generatedTerrainModel) {
            lastFrameProfile_.samplerDebugLine = line;
        }
        if (frame.renderPath == RenderFramePath::GameRuntime
            && (lastFrameProfile_.runtimeSamplerDebugLine.empty() || draw.generatedTerrainModel)) {
            lastFrameProfile_.runtimeSamplerDebugLine = line;
        }
        auto signature = mixHash(baseColor.key, reinterpret_cast<std::uintptr_t>(baseColor.sampler));
        signature = mixHash(signature, static_cast<std::uint64_t>(frame.renderPath));
        signature = mixHash(signature, draw.generatedTerrainModel ? 1U : 0U);
        ++runtimeSamplerLogFrame_;
        if (signature != lastRuntimeSamplerLogSignature_ || runtimeSamplerLogFrame_ % 120U == 1U) {
            core::logInfo(core::LogCategory::Renderer, std::string("[RuntimeSampler] ") + line);
            lastRuntimeSamplerLogSignature_ = signature;
        }
    }
    return descriptor;
}

} // namespace projectunity::renderer
