#pragma once

#include "VulkanResourceContext.hpp"
#include "VulkanUploadContext.hpp"

#include <projectunity/assets/AssetManager.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>

namespace projectunity::renderer {

struct VulkanTextureHandle {
    std::uint64_t key {0};
    VkImage image {VK_NULL_HANDLE};
    VkImageView view {VK_NULL_HANDLE};
    VkSampler sampler {VK_NULL_HANDLE};
};

class VulkanTextureCache final {
public:
    ~VulkanTextureCache();

    [[nodiscard]] const VulkanTextureHandle* ensureUploaded(
        VulkanResourceContext context,
        VulkanUploadContext& uploads,
        const assets::TextureAsset* texture,
        std::string* errorMessage);
    [[nodiscard]] const VulkanTextureHandle* ensureNormalUploaded(
        VulkanResourceContext context,
        VulkanUploadContext& uploads,
        const assets::TextureAsset* texture,
        std::string* errorMessage);
    void clear() noexcept;

private:
    struct TextureResource {
        VulkanResourceContext context;
        VulkanTextureHandle handle;
        VmaAllocation allocation {VK_NULL_HANDLE};
    };

    [[nodiscard]] const VulkanTextureHandle* ensureWhiteTexture(
        VulkanResourceContext context,
        VulkanUploadContext& uploads,
        std::string* errorMessage);
    [[nodiscard]] const VulkanTextureHandle* ensureFlatNormalTexture(
        VulkanResourceContext context,
        VulkanUploadContext& uploads,
        std::string* errorMessage);
    [[nodiscard]] const VulkanTextureHandle* uploadTexture(
        VulkanResourceContext context,
        VulkanUploadContext& uploads,
        std::uint64_t key,
        std::uint32_t width,
        std::uint32_t height,
        const std::uint8_t* rgba8,
        std::size_t byteCount,
        std::string* errorMessage);
    void destroy(TextureResource& texture) noexcept;

    std::unordered_map<std::uint64_t, TextureResource> textures_;
};

} // namespace projectunity::renderer
