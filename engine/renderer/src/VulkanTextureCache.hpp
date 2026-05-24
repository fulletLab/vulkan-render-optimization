#pragma once

#include "VulkanResourceContext.hpp"
#include "VulkanUploadContext.hpp"

#include <projectunity/assets/AssetManager.hpp>
#include <projectunity/renderer/RenderEnvironmentMap.hpp>

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
    [[nodiscard]] const VulkanTextureHandle* ensureBrdfLutUploaded(
        VulkanResourceContext context,
        VulkanUploadContext& uploads,
        std::string* errorMessage);
    [[nodiscard]] const VulkanTextureHandle* ensureIrradianceCubeUploaded(
        VulkanResourceContext context,
        VulkanUploadContext& uploads,
        const RenderEnvironmentSettings& environment,
        std::string* errorMessage);
    [[nodiscard]] const VulkanTextureHandle* ensurePrefilteredEnvironmentCubeUploaded(
        VulkanResourceContext context,
        VulkanUploadContext& uploads,
        const RenderEnvironmentSettings& environment,
        std::string* errorMessage);
    [[nodiscard]] std::uint64_t estimatedUploadBytes(
        const assets::TextureAsset* texture,
        VulkanTextureColorSpace colorSpace) const noexcept;
    [[nodiscard]] std::uint64_t estimatedNormalUploadBytes(const assets::TextureAsset* texture) const noexcept;
    [[nodiscard]] std::uint64_t estimatedBrdfLutUploadBytes() const noexcept;
    void clear() noexcept;
    [[nodiscard]] std::uint64_t uploadCount() const noexcept;
    [[nodiscard]] std::uint64_t uploadedBytes() const noexcept;
    [[nodiscard]] std::uint64_t textureCount() const noexcept;

private:
    struct TextureKey {
        std::uint64_t source {0};
        VulkanTextureColorSpace colorSpace {VulkanTextureColorSpace::Linear};
        assets::TextureGpuFormat gpuFormat {assets::TextureGpuFormat::Rgba8Unorm};
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
    [[nodiscard]] const VulkanTextureHandle* uploadGpuMipTexture(
        VulkanResourceContext context,
        VulkanUploadContext& uploads,
        TextureKey key,
        const assets::TextureAsset& texture,
        std::string* errorMessage);
    [[nodiscard]] bool uploadCubeMap(
        VulkanResourceContext context,
        VulkanUploadContext& uploads,
        TextureResource& destination,
        std::uint64_t handleKey,
        const RenderCubeMap& cube,
        std::string* errorMessage);
    void destroy(TextureResource& texture) noexcept;

    std::unordered_map<TextureKey, TextureResource, TextureKeyHash> textures_;
    TextureResource irradianceCube_;
    TextureResource prefilteredEnvironmentCube_;
    std::uint64_t irradianceCubeEnvironmentKey_ {0};
    std::uint64_t prefilteredEnvironmentCubeEnvironmentKey_ {0};
    std::uint64_t nextHandleKey_ {1};
    std::uint64_t uploadCount_ {0};
    std::uint64_t uploadedBytes_ {0};
};

} // namespace projectunity::renderer
