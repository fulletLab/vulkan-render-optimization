#pragma once

#include "VulkanPlatform.hpp"

#include <projectunity/assets/AssetManager.hpp>

#include <algorithm>
#include <cstdint>

namespace projectunity::renderer {

struct VulkanTextureSamplerCapabilities {
    bool samplerAnisotropyEnabled {false};
    float maxSamplerAnisotropy {1.0F};
    float mipLodBias {0.0F};
};

struct VulkanTextureSamplerState {
    VkFilter magFilter {VK_FILTER_LINEAR};
    VkFilter minFilter {VK_FILTER_LINEAR};
    VkSamplerMipmapMode mipmapMode {VK_SAMPLER_MIPMAP_MODE_LINEAR};
    VkSamplerAddressMode addressModeU {VK_SAMPLER_ADDRESS_MODE_REPEAT};
    VkSamplerAddressMode addressModeV {VK_SAMPLER_ADDRESS_MODE_REPEAT};
    VkSamplerAddressMode addressModeW {VK_SAMPLER_ADDRESS_MODE_REPEAT};
    VkBool32 anisotropyEnable {VK_FALSE};
    float maxAnisotropy {1.0F};
    float mipLodBias {0.0F};
    float minLod {0.0F};
    float maxLod {0.0F};
};

[[nodiscard]] inline VkFilter vulkanTextureFilter(assets::TextureFilterMode filter) noexcept
{
    return filter == assets::TextureFilterMode::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
}

[[nodiscard]] inline VkSamplerMipmapMode vulkanTextureMipmapMode(assets::TextureFilterMode filter) noexcept
{
    return filter == assets::TextureFilterMode::Nearest
        ? VK_SAMPLER_MIPMAP_MODE_NEAREST
        : VK_SAMPLER_MIPMAP_MODE_LINEAR;
}

[[nodiscard]] inline VkSamplerAddressMode vulkanTextureAddressMode(assets::TextureWrapMode wrap) noexcept
{
    if (wrap == assets::TextureWrapMode::MirroredRepeat) {
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    }
    if (wrap == assets::TextureWrapMode::ClampToEdge) {
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

[[nodiscard]] inline VulkanTextureSamplerState buildTextureSamplerState(
    VulkanTextureSamplerCapabilities capabilities,
    const assets::TextureSamplerAsset& sampler,
    std::uint32_t mipLevels) noexcept
{
    mipLevels = std::max(mipLevels, 1U);
    VulkanTextureSamplerState state;
    state.magFilter = vulkanTextureFilter(sampler.magnificationFilter);
    state.minFilter = vulkanTextureFilter(sampler.minificationFilter);
    state.mipmapMode = sampler.useMipmaps
        ? vulkanTextureMipmapMode(sampler.mipmapFilter)
        : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    state.addressModeU = vulkanTextureAddressMode(sampler.wrapU);
    state.addressModeV = vulkanTextureAddressMode(sampler.wrapV);
    state.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    state.mipLodBias = std::clamp(capabilities.mipLodBias, -1.0F, 1.0F);
    state.minLod = 0.0F;
    state.maxLod = sampler.useMipmaps ? static_cast<float>(mipLevels - 1U) : 0.0F;

    const auto canUseAnisotropy = capabilities.samplerAnisotropyEnabled
        && capabilities.maxSamplerAnisotropy > 1.0F
        && sampler.useMipmaps
        && mipLevels > 1U
        && state.minFilter == VK_FILTER_LINEAR;
    if (canUseAnisotropy) {
        state.anisotropyEnable = VK_TRUE;
        state.maxAnisotropy = std::max(1.0F, capabilities.maxSamplerAnisotropy);
    }
    return state;
}

} // namespace projectunity::renderer
