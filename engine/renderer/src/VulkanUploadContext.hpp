#pragma once

#include "VulkanResourceContext.hpp"

#include <functional>
#include <string>

namespace projectunity::renderer {

class VulkanUploadContext final {
public:
    explicit VulkanUploadContext(VulkanResourceContext context);
    ~VulkanUploadContext();

    VulkanUploadContext(const VulkanUploadContext&) = delete;
    VulkanUploadContext& operator=(const VulkanUploadContext&) = delete;

    [[nodiscard]] bool submit(const std::function<void(VkCommandBuffer)>& record, std::string* errorMessage);

private:
    VulkanResourceContext context_;
    VkCommandPool commandPool_ {VK_NULL_HANDLE};
    VkCommandBuffer commandBuffer_ {VK_NULL_HANDLE};
    VkFence fence_ {VK_NULL_HANDLE};
};

} // namespace projectunity::renderer
