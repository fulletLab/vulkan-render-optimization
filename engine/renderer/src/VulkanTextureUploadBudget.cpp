#include "VulkanTextureCache.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace projectunity::renderer {
namespace {

constexpr std::uint64_t kWhiteTextureKey = UINT64_MAX;
constexpr std::uint64_t kFlatNormalTextureKey = UINT64_MAX - 1ULL;
constexpr std::uint64_t kBrdfLutTextureKey = UINT64_MAX - 2ULL;

[[nodiscard]] bool textureHasRgba8(const assets::TextureAsset* texture)
{
    return texture != nullptr
        && texture->id.isValid()
        && texture->width > 0
        && texture->height > 0
        && texture->rgba8.size() >= static_cast<std::size_t>(texture->width) * texture->height * 4U;
}

[[nodiscard]] bool textureHasGpuMips(const assets::TextureAsset* texture)
{
    return texture != nullptr
        && texture->id.isValid()
        && texture->width > 0
        && texture->height > 0
        && !texture->gpuMipLevels.empty()
        && std::all_of(texture->gpuMipLevels.begin(), texture->gpuMipLevels.end(), [](const assets::TextureMipLevel& mip) {
            return mip.width > 0 && mip.height > 0 && !mip.bytes.empty();
        });
}

[[nodiscard]] std::uint64_t gpuMipByteCount(const assets::TextureAsset& texture) noexcept
{
    std::uint64_t bytes = 0;
    for (const auto& mip : texture.gpuMipLevels) {
        bytes += static_cast<std::uint64_t>(mip.bytes.size());
    }
    return bytes;
}

[[nodiscard]] assets::TextureGpuFormat rgba8Format(VulkanTextureColorSpace colorSpace) noexcept
{
    return colorSpace == VulkanTextureColorSpace::Srgb
        ? assets::TextureGpuFormat::Rgba8Srgb
        : assets::TextureGpuFormat::Rgba8Unorm;
}

} // namespace

std::uint64_t VulkanTextureCache::estimatedUploadBytes(
    const assets::TextureAsset* texture,
    VulkanTextureColorSpace colorSpace) const noexcept
{
    if (textureHasGpuMips(texture)) {
        const TextureKey gpuKey {texture->id.value(), VulkanTextureColorSpace::Linear, texture->gpuFormat, texture->sampler};
        if (textures_.find(gpuKey) != textures_.end()) {
            return 0;
        }
        const TextureKey rgbaKey {texture->id.value(), colorSpace, rgba8Format(colorSpace), texture->sampler};
        if (textures_.find(rgbaKey) != textures_.end()) {
            return 0;
        }
        if (!textureHasRgba8(texture)) {
            return gpuMipByteCount(*texture);
        }
        return std::max<std::uint64_t>(
            gpuMipByteCount(*texture),
            static_cast<std::uint64_t>(texture->rgba8.size()));
    }

    if (!textureHasRgba8(texture)) {
        const TextureKey key {kWhiteTextureKey, VulkanTextureColorSpace::Linear, assets::TextureGpuFormat::Rgba8Unorm, {}};
        return textures_.find(key) == textures_.end() ? 4U : 0U;
    }

    const TextureKey key {texture->id.value(), colorSpace, rgba8Format(colorSpace), texture->sampler};
    return textures_.find(key) == textures_.end()
        ? static_cast<std::uint64_t>(texture->rgba8.size())
        : 0U;
}

std::uint64_t VulkanTextureCache::estimatedNormalUploadBytes(const assets::TextureAsset* texture) const noexcept
{
    if (!textureHasGpuMips(texture) && !textureHasRgba8(texture)) {
        const TextureKey key {kFlatNormalTextureKey, VulkanTextureColorSpace::Linear, assets::TextureGpuFormat::Rgba8Unorm, {}};
        return textures_.find(key) == textures_.end() ? 4U : 0U;
    }
    return estimatedUploadBytes(texture, VulkanTextureColorSpace::Linear);
}

std::uint64_t VulkanTextureCache::estimatedBrdfLutUploadBytes() const noexcept
{
    assets::TextureSamplerAsset sampler;
    sampler.wrapU = assets::TextureWrapMode::ClampToEdge;
    sampler.wrapV = assets::TextureWrapMode::ClampToEdge;
    sampler.useMipmaps = false;
    const TextureKey key {kBrdfLutTextureKey, VulkanTextureColorSpace::Linear, assets::TextureGpuFormat::Rgba8Unorm, sampler};
    return textures_.find(key) == textures_.end() ? 128ULL * 128ULL * 4ULL : 0ULL;
}

} // namespace projectunity::renderer
