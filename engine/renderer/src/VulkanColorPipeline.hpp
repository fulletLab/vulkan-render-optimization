#pragma once

#include "VulkanResourceContext.hpp"

#include <array>

namespace projectunity::renderer {

struct VulkanColorPushConstants {
    std::array<float, 16> modelViewProjection {};
};

class VulkanColorPipeline final {
public:
    VulkanColorPipeline(VulkanResourceContext context, VkRenderPass renderPass);
    ~VulkanColorPipeline();

    VulkanColorPipeline(const VulkanColorPipeline&) = delete;
    VulkanColorPipeline& operator=(const VulkanColorPipeline&) = delete;

    [[nodiscard]] VkPipeline pipeline() const noexcept;
    [[nodiscard]] VkPipelineLayout layout() const noexcept;

private:
    void create(VkRenderPass renderPass);
    void destroy() noexcept;

    VulkanResourceContext context_;
    VkPipelineLayout layout_ {VK_NULL_HANDLE};
    VkPipeline pipeline_ {VK_NULL_HANDLE};
};

} // namespace projectunity::renderer
