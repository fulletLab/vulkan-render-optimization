#pragma once

#include "VulkanPlatform.hpp"

#include <vma/vk_mem_alloc.h>

#include <cstdint>

namespace projectunity::renderer {

struct VulkanResourceContext {
    VkPhysicalDevice physicalDevice {VK_NULL_HANDLE};
    VkDevice device {VK_NULL_HANDLE};
    VkQueue graphicsQueue {VK_NULL_HANDLE};
    VmaAllocator allocator {VK_NULL_HANDLE};
    std::uint32_t queueFamilyIndex {0};
    bool geometryShaderSupported {false};
};

} // namespace projectunity::renderer
