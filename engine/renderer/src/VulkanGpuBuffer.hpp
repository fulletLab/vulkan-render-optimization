#pragma once

#include "VulkanResourceContext.hpp"
#include "VulkanUploadContext.hpp"

#include <cstddef>
#include <span>
#include <string>

namespace projectunity::renderer {

class VulkanGpuBuffer final {
public:
    VulkanGpuBuffer() = default;
    ~VulkanGpuBuffer();

    VulkanGpuBuffer(const VulkanGpuBuffer&) = delete;
    VulkanGpuBuffer& operator=(const VulkanGpuBuffer&) = delete;
    VulkanGpuBuffer(VulkanGpuBuffer&& other) noexcept;
    VulkanGpuBuffer& operator=(VulkanGpuBuffer&& other) noexcept;

    [[nodiscard]] bool upload(
        VulkanResourceContext context,
        VulkanUploadContext& uploads,
        VkBufferUsageFlags usage,
        std::span<const std::byte> bytes,
        std::string* errorMessage);
    void destroy() noexcept;

    [[nodiscard]] VkBuffer buffer() const noexcept;
    [[nodiscard]] VkDeviceSize size() const noexcept;

private:
    VulkanResourceContext context_;
    VkBuffer buffer_ {VK_NULL_HANDLE};
    VmaAllocation allocation_ {VK_NULL_HANDLE};
    VkDeviceSize size_ {0};
};

} // namespace projectunity::renderer
