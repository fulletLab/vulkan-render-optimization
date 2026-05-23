#include "VulkanTextureFormat.hpp"

namespace projectunity::renderer {

std::optional<VkFormat> toVkTextureFormat(assets::TextureGpuFormat format)
{
    switch (format) {
    case assets::TextureGpuFormat::Rgba8Unorm:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case assets::TextureGpuFormat::Rgba8Srgb:
        return VK_FORMAT_R8G8B8A8_SRGB;
    case assets::TextureGpuFormat::Bc1RgbUnorm:
        return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
    case assets::TextureGpuFormat::Bc1RgbSrgb:
        return VK_FORMAT_BC1_RGB_SRGB_BLOCK;
    case assets::TextureGpuFormat::Bc1RgbaUnorm:
        return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case assets::TextureGpuFormat::Bc1RgbaSrgb:
        return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
    case assets::TextureGpuFormat::Bc2Unorm:
        return VK_FORMAT_BC2_UNORM_BLOCK;
    case assets::TextureGpuFormat::Bc2Srgb:
        return VK_FORMAT_BC2_SRGB_BLOCK;
    case assets::TextureGpuFormat::Bc3Unorm:
        return VK_FORMAT_BC3_UNORM_BLOCK;
    case assets::TextureGpuFormat::Bc3Srgb:
        return VK_FORMAT_BC3_SRGB_BLOCK;
    case assets::TextureGpuFormat::Bc5Unorm:
        return VK_FORMAT_BC5_UNORM_BLOCK;
    case assets::TextureGpuFormat::Bc5Snorm:
        return VK_FORMAT_BC5_SNORM_BLOCK;
    case assets::TextureGpuFormat::Bc7Unorm:
        return VK_FORMAT_BC7_UNORM_BLOCK;
    case assets::TextureGpuFormat::Bc7Srgb:
        return VK_FORMAT_BC7_SRGB_BLOCK;
    case assets::TextureGpuFormat::Etc2Rgba8Unorm:
        return VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
    case assets::TextureGpuFormat::Etc2Rgba8Srgb:
        return VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK;
    case assets::TextureGpuFormat::Astc4x4Unorm:
    case assets::TextureGpuFormat::Astc5x4Unorm:
    case assets::TextureGpuFormat::Astc5x5Unorm:
    case assets::TextureGpuFormat::Astc6x5Unorm:
    case assets::TextureGpuFormat::Astc6x6Unorm:
    case assets::TextureGpuFormat::Astc8x5Unorm:
    case assets::TextureGpuFormat::Astc8x6Unorm:
    case assets::TextureGpuFormat::Astc8x8Unorm:
    case assets::TextureGpuFormat::Astc10x5Unorm:
    case assets::TextureGpuFormat::Astc10x6Unorm:
    case assets::TextureGpuFormat::Astc10x8Unorm:
    case assets::TextureGpuFormat::Astc10x10Unorm:
    case assets::TextureGpuFormat::Astc12x10Unorm:
    case assets::TextureGpuFormat::Astc12x12Unorm:
        return static_cast<VkFormat>(VK_FORMAT_ASTC_4x4_UNORM_BLOCK
            + (static_cast<int>(format) - static_cast<int>(assets::TextureGpuFormat::Astc4x4Unorm)) / 2 * 2);
    case assets::TextureGpuFormat::Astc4x4Srgb:
    case assets::TextureGpuFormat::Astc5x4Srgb:
    case assets::TextureGpuFormat::Astc5x5Srgb:
    case assets::TextureGpuFormat::Astc6x5Srgb:
    case assets::TextureGpuFormat::Astc6x6Srgb:
    case assets::TextureGpuFormat::Astc8x5Srgb:
    case assets::TextureGpuFormat::Astc8x6Srgb:
    case assets::TextureGpuFormat::Astc8x8Srgb:
    case assets::TextureGpuFormat::Astc10x5Srgb:
    case assets::TextureGpuFormat::Astc10x6Srgb:
    case assets::TextureGpuFormat::Astc10x8Srgb:
    case assets::TextureGpuFormat::Astc10x10Srgb:
    case assets::TextureGpuFormat::Astc12x10Srgb:
    case assets::TextureGpuFormat::Astc12x12Srgb:
        return static_cast<VkFormat>(VK_FORMAT_ASTC_4x4_SRGB_BLOCK
            + (static_cast<int>(format) - static_cast<int>(assets::TextureGpuFormat::Astc4x4Srgb)) / 2 * 2);
    }
    return std::nullopt;
}

bool textureFormatCanBeSampled(VkPhysicalDevice physicalDevice, VkFormat format)
{
    VkFormatProperties properties {};
    vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &properties);
    return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;
}

} // namespace projectunity::renderer
