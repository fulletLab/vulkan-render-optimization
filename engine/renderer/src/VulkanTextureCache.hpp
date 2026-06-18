#pragma once

#include "VulkanResourceContext.hpp"
#include "VulkanTextureSamplerPolicy.hpp"
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

enum class VulkanTextureRole : std::uint8_t {
    Unknown,
    FallbackWhite,
    FallbackFlatNormal,
    BaseColor,
    Normal,
    MetallicRoughness,
    Occlusion,
    Emissive,
    BrdfLut,
};

[[nodiscard]] const char* vulkanTextureRoleName(VulkanTextureRole role) noexcept;

struct VulkanTextureSamplerDiagnostics {
    std::uint64_t samplerCreateCount {0};
    std::uint64_t anisotropicSamplerCount {0};
    std::uint64_t trilinearSamplerCount {0};
    std::string lastTextureName;
    std::string lastTextureRole;
    std::uint32_t lastWidth {0};
    std::uint32_t lastHeight {0};
    std::uint32_t lastMipLevels {0};
    bool lastAnisotropyEnabled {false};
    float lastMaxAnisotropy {1.0F};
    float lastMipLodBias {0.0F};
    float lastMinLod {0.0F};
    float lastMaxLod {0.0F};
    std::uint64_t lastTextureHandleKey {0};
    std::uint64_t lastSamplerPolicyRevision {0};
    bool lastSamplerRecreatedAfterSettingsChange {false};
};

struct VulkanTextureHandle {
    std::uint64_t key {0};
    VkImage image {VK_NULL_HANDLE};
    VkImageView view {VK_NULL_HANDLE};
    VkSampler sampler {VK_NULL_HANDLE};
    std::uint32_t width {0};
    std::uint32_t height {0};
    std::uint32_t mipLevels {0};
    VulkanTextureRole role {VulkanTextureRole::Unknown};
    VulkanTextureColorSpace colorSpace {VulkanTextureColorSpace::Linear};
    VulkanTextureSamplerState samplerState;
    std::uint64_t samplerPolicyRevision {0};
    bool samplerRecreatedAfterSettingsChange {false};
};

class VulkanTextureCache final {
public:
    ~VulkanTextureCache();

    [[nodiscard]] const VulkanTextureHandle* ensureUploaded(
        VulkanResourceContext context,
        VulkanUploadContext& uploads,
        const assets::TextureAsset* texture,
        std::string* errorMessage,
        VulkanTextureRole role = VulkanTextureRole::Unknown);
    [[nodiscard]] const VulkanTextureHandle* ensureSrgbUploaded(
        VulkanResourceContext context,
        VulkanUploadContext& uploads,
        const assets::TextureAsset* texture,
        std::string* errorMessage,
        VulkanTextureRole role = VulkanTextureRole::BaseColor);
    [[nodiscard]] const VulkanTextureHandle* ensureNormalUploaded(
        VulkanResourceContext context,
        VulkanUploadContext& uploads,
        const assets::TextureAsset* texture,
        std::string* errorMessage,
        VulkanTextureRole role = VulkanTextureRole::Normal);
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
    void invalidateTextureSamplers() noexcept;
    void recreateSamplersForQualityChange() noexcept;
    [[nodiscard]] std::uint64_t samplerPolicyRevision() const noexcept;
    [[nodiscard]] std::uint64_t uploadCount() const noexcept;
    [[nodiscard]] std::uint64_t uploadedBytes() const noexcept;
    [[nodiscard]] std::uint64_t textureCount() const noexcept;
    [[nodiscard]] const VulkanTextureSamplerDiagnostics& samplerDiagnostics() const noexcept;

private:
    struct TextureKey {
        std::uint64_t source {0};
        VulkanTextureColorSpace colorSpace {VulkanTextureColorSpace::Linear};
        assets::TextureGpuFormat gpuFormat {assets::TextureGpuFormat::Rgba8Unorm};
        assets::TextureSamplerAsset sampler;
        bool samplerAnisotropyEnabled {false};
        float maxSamplerAnisotropy {1.0F};
        float mipLodBias {0.0F};
        bool forceMaxLodZero {false};
        bool forceAnisotropyOff {false};
        bool forceAnisotropyOn {false};
        std::uint64_t samplerPolicyRevision {0};

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
        const assets::TextureAsset* sourceTexture,
        VulkanTextureRole role,
        std::string* errorMessage);
    [[nodiscard]] const VulkanTextureHandle* uploadGpuMipTexture(
        VulkanResourceContext context,
        VulkanUploadContext& uploads,
        TextureKey key,
        const assets::TextureAsset& texture,
        VulkanTextureRole role,
        std::string* errorMessage);
    [[nodiscard]] bool uploadCubeMap(
        VulkanResourceContext context,
        VulkanUploadContext& uploads,
        TextureResource& destination,
        std::uint64_t handleKey,
        const RenderCubeMap& cube,
        std::string* errorMessage);
    [[nodiscard]] static TextureKey textureKeyFor(
        VulkanResourceContext context,
        std::uint64_t source,
        VulkanTextureColorSpace colorSpace,
        assets::TextureGpuFormat gpuFormat,
        const assets::TextureSamplerAsset& sampler) noexcept;
    void destroy(TextureResource& texture) noexcept;
    void recordSamplerDiagnostics(
        const assets::TextureAsset* sourceTexture,
        VulkanTextureRole role,
        std::uint32_t width,
        std::uint32_t height,
        std::uint32_t mipLevels,
        const VulkanTextureSamplerState& samplerState,
        VulkanTextureColorSpace colorSpace,
        std::uint64_t handleKey,
        std::uint64_t samplerPolicyRevision,
        bool samplerRecreatedAfterSettingsChange);

    std::unordered_map<TextureKey, TextureResource, TextureKeyHash> textures_;
    TextureResource irradianceCube_;
    TextureResource prefilteredEnvironmentCube_;
    std::uint64_t irradianceCubeEnvironmentKey_ {0};
    std::uint64_t prefilteredEnvironmentCubeEnvironmentKey_ {0};
    std::uint64_t nextHandleKey_ {1};
    std::uint64_t samplerPolicyRevision_ {0};
    std::uint64_t uploadCount_ {0};
    std::uint64_t uploadedBytes_ {0};
    VulkanTextureSamplerDiagnostics samplerDiagnostics_;
};

} // namespace projectunity::renderer
