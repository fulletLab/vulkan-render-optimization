#pragma once

#include "VulkanPlatform.hpp"

#include <algorithm>
#include <array>

namespace projectunity::renderer {

class VulkanScopedLabel final {
public:
    VulkanScopedLabel(
        PFN_vkCmdBeginDebugUtilsLabelEXT beginLabel,
        PFN_vkCmdEndDebugUtilsLabelEXT endLabel,
        VkCommandBuffer commandBuffer,
        const char* name,
        std::array<float, 4> color)
        : commandBuffer_(commandBuffer)
        , endLabel_(endLabel)
    {
        if (beginLabel == nullptr || endLabel == nullptr || commandBuffer == VK_NULL_HANDLE) {
            return;
        }
        VkDebugUtilsLabelEXT label {};
        label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pLabelName = name;
        std::copy(color.begin(), color.end(), label.color);
        beginLabel(commandBuffer, &label);
        active_ = true;
    }

    ~VulkanScopedLabel()
    {
        if (active_) {
            endLabel_(commandBuffer_);
        }
    }

    VulkanScopedLabel(const VulkanScopedLabel&) = delete;
    VulkanScopedLabel& operator=(const VulkanScopedLabel&) = delete;

private:
    VkCommandBuffer commandBuffer_ {VK_NULL_HANDLE};
    PFN_vkCmdEndDebugUtilsLabelEXT endLabel_ {nullptr};
    bool active_ {false};
};

} // namespace projectunity::renderer
