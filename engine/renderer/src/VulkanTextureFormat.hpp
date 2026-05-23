#pragma once

#include <projectunity/assets/AssetManager.hpp>

#include <vulkan/vulkan.h>

#include <optional>

namespace projectunity::renderer {

[[nodiscard]] std::optional<VkFormat> toVkTextureFormat(assets::TextureGpuFormat format);
[[nodiscard]] bool textureFormatCanBeSampled(VkPhysicalDevice physicalDevice, VkFormat format);

} // namespace projectunity::renderer
