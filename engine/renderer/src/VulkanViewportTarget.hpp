#pragma once

#include "VulkanPlatform.hpp"
#include "VulkanColorMesh.hpp"
#include "VulkanColorPipeline.hpp"
#include "VulkanFrameData.hpp"
#include "VulkanMeshCache.hpp"
#include "VulkanMeshPipeline.hpp"
#include "VulkanShadowPipeline.hpp"
#include "VulkanTextureCache.hpp"

#include <projectunity/renderer/RendererTypes.hpp>
#include <projectunity/renderer/ViewportRenderSurface.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace projectunity::renderer {

struct VulkanViewportContext {
    VkInstance instance {VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice {VK_NULL_HANDLE};
    VkDevice device {VK_NULL_HANDLE};
    VkQueue graphicsQueue {VK_NULL_HANDLE};
    VmaAllocator allocator {VK_NULL_HANDLE};
    std::uint32_t queueFamilyIndex {0};

    [[nodiscard]] VulkanResourceContext resources() const noexcept
    {
        return {physicalDevice, device, graphicsQueue, allocator, queueFamilyIndex};
    }
};

struct VulkanMaterialTextureKey {
    std::uint64_t baseColor {0};
    std::uint64_t normal {0};
    std::uint64_t metallicRoughness {0};
    std::uint64_t occlusion {0};
    std::uint64_t emissive {0};

    [[nodiscard]] bool operator==(const VulkanMaterialTextureKey&) const noexcept = default;
};

struct VulkanMaterialTextureKeyHash {
    [[nodiscard]] std::size_t operator()(const VulkanMaterialTextureKey& key) const noexcept;
};

struct VulkanMeshDrawBatch {
    const RenderMeshDraw* draw {nullptr};
    std::uint32_t firstInstance {0};
    std::uint32_t instanceCount {0};
};

class VulkanViewportTarget final {
public:
    VulkanViewportTarget(VulkanViewportContext context, ViewportRenderSurfaceDesc desc);
    ~VulkanViewportTarget();

    VulkanViewportTarget(const VulkanViewportTarget&) = delete;
    VulkanViewportTarget& operator=(const VulkanViewportTarget&) = delete;

    [[nodiscard]] bool matches(const ViewportRenderSurfaceDesc& surfaceDesc) const noexcept;
    [[nodiscard]] bool renderFrame(
        const RenderFrame& frame,
        VulkanUploadContext& uploads,
        VulkanMeshCache& meshCache,
        VulkanTextureCache& textureCache,
        std::string* errorMessage);

private:
    void destroy() noexcept;
    void createSurface();
    void createSwapchain();
    void createImageViews();
    void createDepthTarget();
    void createFramebuffers();
    void createDescriptors();
    void createCommands();
    void createSync();
    [[nodiscard]] VkDescriptorSet textureDescriptor(
        const VulkanTextureHandle& baseColor,
        const VulkanTextureHandle& normal,
        const VulkanTextureHandle& metallicRoughness,
        const VulkanTextureHandle& occlusion,
        const VulkanTextureHandle& emissive,
        std::string* errorMessage);
    [[nodiscard]] bool recordFrameCommand(
        std::uint32_t imageIndex,
        const RenderFrame& frame,
        VulkanUploadContext& uploads,
        VulkanMeshCache& meshCache,
        VulkanTextureCache& textureCache,
        std::string* errorMessage);
    [[nodiscard]] bool recordShadowPass(
        const RenderFrame& frame,
        VulkanUploadContext& uploads,
        VulkanMeshCache& meshCache,
        VulkanTextureCache& textureCache,
        std::string* errorMessage);
    [[nodiscard]] bool buildMeshBatches(
        std::span<const RenderMeshDraw> draws,
        VulkanUploadContext& uploads,
        std::string* errorMessage);

    VulkanViewportContext context_;
    ViewportRenderSurfaceDesc desc_;
    VkSurfaceKHR surface_ {VK_NULL_HANDLE};
    VkSwapchainKHR swapchain_ {VK_NULL_HANDLE};
    VkSurfaceFormatKHR surfaceFormat_ {};
    VkExtent2D extent_ {};
    std::vector<VkImage> images_;
    std::vector<VkImageView> imageViews_;
    VkImage depthImage_ {VK_NULL_HANDLE};
    VmaAllocation depthAllocation_ {VK_NULL_HANDLE};
    VkImageView depthView_ {VK_NULL_HANDLE};
    std::unique_ptr<VulkanMeshPipeline> meshPipeline_;
    std::unique_ptr<VulkanShadowPipeline> shadowPipeline_;
    std::unique_ptr<VulkanColorPipeline> colorPipeline_;
    VulkanFrameData frameData_;
    std::vector<const RenderMeshDraw*> orderedMeshDraws_;
    std::vector<VulkanGpuInstance> meshInstances_;
    std::vector<VulkanMeshDrawBatch> meshBatches_;
    VulkanGpuBuffer meshInstanceBuffer_;
    std::vector<VulkanColorMeshBuffers> colorMeshes_;
    std::vector<VkFramebuffer> framebuffers_;
    VkDescriptorPool descriptorPool_ {VK_NULL_HANDLE};
    std::unordered_map<VulkanMaterialTextureKey, VkDescriptorSet, VulkanMaterialTextureKeyHash> textureDescriptors_;
    VkCommandPool commandPool_ {VK_NULL_HANDLE};
    VkCommandBuffer commandBuffer_ {VK_NULL_HANDLE};
    PFN_vkCmdBeginDebugUtilsLabelEXT beginDebugLabel_ {nullptr};
    PFN_vkCmdEndDebugUtilsLabelEXT endDebugLabel_ {nullptr};
    VkSemaphore imageAvailable_ {VK_NULL_HANDLE};
    VkSemaphore renderFinished_ {VK_NULL_HANDLE};
    VkFence inFlight_ {VK_NULL_HANDLE};
};

} // namespace projectunity::renderer
