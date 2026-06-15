#pragma once

#include "VulkanMeshCache.hpp"
#include "VulkanResourceContext.hpp"

#include <array>

namespace projectunity::renderer {

struct VulkanDrawPushConstants {
    std::array<float, 16> modelMatrix {};
    std::array<float, 4> baseColor {1.0F, 1.0F, 1.0F, 1.0F};
    std::array<float, 4> pbrFactors {1.0F, 1.0F, 1.0F, 0.0F};
    std::array<float, 4> emissiveColor {0.0F, 0.0F, 0.0F, 0.5F};
    std::array<float, 4> materialExtras {1.0F, 0.0F, 0.0F, 0.0F};
};

class VulkanMeshPipeline final {
public:
    VulkanMeshPipeline(VulkanResourceContext context, VkFormat colorFormat);
    ~VulkanMeshPipeline();

    VulkanMeshPipeline(const VulkanMeshPipeline&) = delete;
    VulkanMeshPipeline& operator=(const VulkanMeshPipeline&) = delete;

    [[nodiscard]] VkRenderPass renderPass() const noexcept;
    [[nodiscard]] VkPipeline pipeline() const noexcept;
    [[nodiscard]] VkPipeline flippedWindingPipeline() const noexcept;
    [[nodiscard]] VkPipeline doubleSidedPipeline() const noexcept;
    [[nodiscard]] VkPipeline transparentPipeline() const noexcept;
    [[nodiscard]] VkPipeline transparentFlippedWindingPipeline() const noexcept;
    [[nodiscard]] VkPipeline transparentDoubleSidedPipeline() const noexcept;
    [[nodiscard]] VkPipeline wirePipeline() const noexcept;
    [[nodiscard]] VkPipelineLayout layout() const noexcept;
    [[nodiscard]] VkDescriptorSetLayout textureLayout() const noexcept;
    [[nodiscard]] VkFormat depthFormat() const noexcept;

private:
    [[nodiscard]] VkFormat chooseDepthFormat() const;
    void createTextureLayout();
    void createRenderPass(VkFormat colorFormat);
    void createPipeline();
    void destroy() noexcept;

    VulkanResourceContext context_;
    VkFormat depthFormat_ {VK_FORMAT_UNDEFINED};
    VkDescriptorSetLayout textureLayout_ {VK_NULL_HANDLE};
    VkRenderPass renderPass_ {VK_NULL_HANDLE};
    VkPipelineLayout layout_ {VK_NULL_HANDLE};
    VkPipeline pipeline_ {VK_NULL_HANDLE};
    VkPipeline flippedWindingPipeline_ {VK_NULL_HANDLE};
    VkPipeline doubleSidedPipeline_ {VK_NULL_HANDLE};
    VkPipeline transparentPipeline_ {VK_NULL_HANDLE};
    VkPipeline transparentFlippedWindingPipeline_ {VK_NULL_HANDLE};
    VkPipeline transparentDoubleSidedPipeline_ {VK_NULL_HANDLE};
    VkPipeline wirePipeline_ {VK_NULL_HANDLE};
};

} // namespace projectunity::renderer
