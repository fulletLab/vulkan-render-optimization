#pragma once

#include "VulkanMeshCache.hpp"
#include "VulkanResourceContext.hpp"

#include <cstdint>

namespace projectunity::renderer {

class VulkanShadowPipeline final {
public:
    VulkanShadowPipeline(
        VulkanResourceContext context,
        VkDescriptorSetLayout materialLayout,
        VkFormat depthFormat,
        std::uint32_t mapSize = 4096);
    ~VulkanShadowPipeline();

    VulkanShadowPipeline(const VulkanShadowPipeline&) = delete;
    VulkanShadowPipeline& operator=(const VulkanShadowPipeline&) = delete;

    [[nodiscard]] VkRenderPass renderPass() const noexcept;
    [[nodiscard]] VkFramebuffer framebuffer() const noexcept;
    [[nodiscard]] VkPipeline pipeline() const noexcept;
    [[nodiscard]] VkPipelineLayout layout() const noexcept;
    [[nodiscard]] VkImageView imageView() const noexcept;
    [[nodiscard]] VkSampler sampler() const noexcept;
    [[nodiscard]] VkExtent2D extent() const noexcept;

private:
    void createRenderPass();
    void createShadowTarget();
    void createPipeline(VkDescriptorSetLayout materialLayout);
    void destroy() noexcept;

    VulkanResourceContext context_;
    VkFormat depthFormat_ {VK_FORMAT_UNDEFINED};
    VkExtent2D extent_ {};
    VkRenderPass renderPass_ {VK_NULL_HANDLE};
    VkImage image_ {VK_NULL_HANDLE};
    VmaAllocation allocation_ {VK_NULL_HANDLE};
    VkImageView imageView_ {VK_NULL_HANDLE};
    VkSampler sampler_ {VK_NULL_HANDLE};
    VkFramebuffer framebuffer_ {VK_NULL_HANDLE};
    VkPipelineLayout layout_ {VK_NULL_HANDLE};
    VkPipeline pipeline_ {VK_NULL_HANDLE};
};

} // namespace projectunity::renderer
