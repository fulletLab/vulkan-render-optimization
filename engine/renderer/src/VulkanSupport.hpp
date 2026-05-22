#pragma once

#include "VulkanPlatform.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace projectunity::renderer::vulkan {

[[nodiscard]] std::uint32_t major(std::uint32_t version);
[[nodiscard]] std::uint32_t minor(std::uint32_t version);
[[nodiscard]] std::uint32_t patch(std::uint32_t version);
[[nodiscard]] bool hasLayer(std::string_view name);
[[nodiscard]] bool hasInstanceExtension(std::string_view name);
[[nodiscard]] bool hasDeviceExtension(VkPhysicalDevice device, std::string_view name);
[[nodiscard]] std::vector<const char*> requiredInstanceExtensions();
void requireInstanceExtensions(const std::vector<const char*>& extensions);
[[nodiscard]] std::uint32_t findGraphicsQueueFamily(VkPhysicalDevice device);
[[nodiscard]] VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats);
[[nodiscard]] VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes, bool vsync);
[[nodiscard]] VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities, std::uint32_t width, std::uint32_t height);
[[nodiscard]] VkPhysicalDevice choosePhysicalDevice(VkInstance instance);

} // namespace projectunity::renderer::vulkan
