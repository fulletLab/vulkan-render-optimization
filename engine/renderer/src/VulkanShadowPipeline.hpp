#pragma once

#include "VulkanMeshCache.hpp"
#include "VulkanResourceContext.hpp"

#include <array>
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
    [[nodiscard]] VkPipeline flippedWindingPipeline() const noexcept;
    [[nodiscard]] VkPipeline doubleSidedPipeline() const noexcept;
    [[nodiscard]] VkPipelineLayout layout() const noexcept;
    [[nodiscard]] VkImageView imageView() const noexcept;
    [[nodiscard]] VkSampler sampler() const noexcept;
    [[nodiscard]] VkExtent2D extent() const noexcept;
    [[nodiscard]] VkImage pointCubeImage() const noexcept;
    [[nodiscard]] VkImageView pointCubeImageView() const noexcept;
    [[nodiscard]] VkSampler pointCubeSampler() const noexcept;
    [[nodiscard]] VkFramebuffer pointCubeFramebuffer(std::uint32_t faceIndex) const noexcept;
    [[nodiscard]] VkExtent2D pointCubeExtent() const noexcept;

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
    VkExtent2D pointCubeExtent_ {};
    VkImage pointCubeImage_ {VK_NULL_HANDLE};
    VmaAllocation pointCubeAllocation_ {VK_NULL_HANDLE};
    VkImageView pointCubeImageView_ {VK_NULL_HANDLE};
    std::array<VkImageView, 6> pointCubeFaceViews_ {};
    VkSampler pointCubeSampler_ {VK_NULL_HANDLE};
    std::array<VkFramebuffer, 6> pointCubeFramebuffers_ {};
    VkPipelineLayout layout_ {VK_NULL_HANDLE};
    VkPipeline pipeline_ {VK_NULL_HANDLE};
    VkPipeline flippedWindingPipeline_ {VK_NULL_HANDLE};
    VkPipeline doubleSidedPipeline_ {VK_NULL_HANDLE};
};

} // namespace projectunity::renderer
