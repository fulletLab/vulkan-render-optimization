#pragma once

#include "VulkanResourceContext.hpp"
#include "VulkanUploadContext.hpp"

#include <projectunity/assets/AssetManager.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace projectunity::renderer {

enum class VulkanTextureColorSpace : std::uint8_t {
    Linear,
    Srgb,
};

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
    [[nodiscard]] const VulkanTextureHandle* ensureSrgbUploaded(
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
    struct TextureKey {
        std::uint64_t source {0};
        VulkanTextureColorSpace colorSpace {VulkanTextureColorSpace::Linear};
        assets::TextureSamplerAsset sampler;

        [[nodiscard]] bool operator==(const TextureKey&) const noexcept = default;
    };

    struct TextureKeyHash {
        [[nodiscard]] std::size_t operator()(const TextureKey& key) const noexcept;
    };

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
        TextureKey key,
        std::uint32_t width,
        std::uint32_t height,
        const std::uint8_t* rgba8,
        std::size_t byteCount,
        std::string* errorMessage);
    void destroy(TextureResource& texture) noexcept;

    std::unordered_map<TextureKey, TextureResource, TextureKeyHash> textures_;
    std::uint64_t nextHandleKey_ {1};
};

} // namespace projectunity::renderer
