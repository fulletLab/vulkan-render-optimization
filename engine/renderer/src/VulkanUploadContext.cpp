#include "VulkanUploadContext.hpp"

#include <stdexcept>

namespace projectunity::renderer {

VulkanUploadContext::VulkanUploadContext(VulkanResourceContext context)
    : context_(context)
{
    VkCommandPoolCreateInfo poolInfo {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = context_.queueFamilyIndex;
    if (vkCreateCommandPool(context_.device, &poolInfo, nullptr, &commandPool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan upload command pool");
    }

    VkCommandBufferAllocateInfo allocInfo {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(context_.device, &allocInfo, &commandBuffer_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate Vulkan upload command buffer");
    }

    VkFenceCreateInfo fenceInfo {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(context_.device, &fenceInfo, nullptr, &fence_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan upload fence");
    }
}

VulkanUploadContext::~VulkanUploadContext()
{
    if (context_.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(context_.device);
    }
    if (fence_ != VK_NULL_HANDLE) {
        vkDestroyFence(context_.device, fence_, nullptr);
    }
    if (commandPool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(context_.device, commandPool_, nullptr);
    }
}

bool VulkanUploadContext::submit(const std::function<void(VkCommandBuffer)>& record, std::string* errorMessage)
{
    if (record == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Vulkan upload recording callback is empty";
        }
        return false;
    }

    vkResetCommandBuffer(commandBuffer_, 0);
    VkCommandBufferBeginInfo begin {};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(commandBuffer_, &begin) != VK_SUCCESS) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to begin Vulkan upload command buffer";
        }
        return false;
    }
    record(commandBuffer_);
    if (vkEndCommandBuffer(commandBuffer_) != VK_SUCCESS) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to end Vulkan upload command buffer";
        }
        return false;
    }

    VkSubmitInfo submitInfo {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer_;
    vkResetFences(context_.device, 1, &fence_);
    if (vkQueueSubmit(context_.graphicsQueue, 1, &submitInfo, fence_) != VK_SUCCESS) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to submit Vulkan upload command buffer";
        }
        return false;
    }

    constexpr std::uint64_t kUploadTimeoutNs = 5'000'000'000ULL;
    if (vkWaitForFences(context_.device, 1, &fence_, VK_TRUE, kUploadTimeoutNs) != VK_SUCCESS) {
        if (errorMessage != nullptr) {
            *errorMessage = "Timed out waiting for Vulkan upload completion";
        }
        return false;
    }
    return true;
}

} // namespace projectunity::renderer
