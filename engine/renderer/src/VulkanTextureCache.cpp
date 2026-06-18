#include "VulkanTextureCache.hpp"

#include "VulkanTextureFormat.hpp"

#include <projectunity/core/Log.hpp>
#include <projectunity/renderer/RenderBrdfLut.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <vector>

namespace projectunity::renderer {
namespace {

constexpr std::uint64_t kWhiteTextureKey = UINT64_MAX;
constexpr std::uint64_t kFlatNormalTextureKey = UINT64_MAX - 1ULL;
constexpr std::uint64_t kBrdfLutTextureKey = UINT64_MAX - 2ULL;

struct TextureStagingBuffer {
    VulkanResourceContext context;
    VkBuffer buffer {VK_NULL_HANDLE};
    VmaAllocation allocation {VK_NULL_HANDLE};
    VmaAllocationInfo info {};

    ~TextureStagingBuffer()
    {
        if (buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(context.allocator, buffer, allocation);
        }
    }
};

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

[[nodiscard]] VkImageMemoryBarrier imageBarrier(
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkAccessFlags sourceAccess,
    VkAccessFlags destinationAccess,
    std::uint32_t baseMipLevel,
    std::uint32_t levelCount,
    std::uint32_t layerCount = 1)
{
    VkImageMemoryBarrier barrier {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = baseMipLevel;
    barrier.subresourceRange.levelCount = levelCount;
    barrier.subresourceRange.layerCount = layerCount;
    barrier.srcAccessMask = sourceAccess;
    barrier.dstAccessMask = destinationAccess;
    return barrier;
}

[[nodiscard]] std::uint32_t mipLevelCount(std::uint32_t width, std::uint32_t height)
{
    std::uint32_t levels = 1;
    auto size = std::max(width, height);
    while (size > 1U) {
        size /= 2U;
        ++levels;
    }
    return levels;
}

[[nodiscard]] VkFormat textureFormat(VulkanTextureColorSpace colorSpace)
{
    return colorSpace == VulkanTextureColorSpace::Srgb
        ? VK_FORMAT_R8G8B8A8_SRGB
        : VK_FORMAT_R8G8B8A8_UNORM;
}

[[nodiscard]] bool supportsLinearMipBlit(VkPhysicalDevice physicalDevice, VkFormat format)
{
    VkFormatProperties properties {};
    vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &properties);
    constexpr auto required = VK_FORMAT_FEATURE_BLIT_SRC_BIT
        | VK_FORMAT_FEATURE_BLIT_DST_BIT
        | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    return (properties.optimalTilingFeatures & required) == required;
}

[[nodiscard]] const char* filterName(VkFilter filter) noexcept
{
    return filter == VK_FILTER_NEAREST ? "VK_FILTER_NEAREST" : "VK_FILTER_LINEAR";
}

[[nodiscard]] const char* mipmapModeName(VkSamplerMipmapMode mode) noexcept
{
    return mode == VK_SAMPLER_MIPMAP_MODE_NEAREST
        ? "VK_SAMPLER_MIPMAP_MODE_NEAREST"
        : "VK_SAMPLER_MIPMAP_MODE_LINEAR";
}

[[nodiscard]] const char* addressModeName(VkSamplerAddressMode mode) noexcept
{
    switch (mode) {
    case VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT: return "VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT";
    case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE: return "VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE";
    case VK_SAMPLER_ADDRESS_MODE_REPEAT: return "VK_SAMPLER_ADDRESS_MODE_REPEAT";
    default: return "VK_SAMPLER_ADDRESS_MODE_REPEAT";
    }
}

[[nodiscard]] const char* colorSpaceName(VulkanTextureColorSpace colorSpace) noexcept
{
    return colorSpace == VulkanTextureColorSpace::Srgb ? "sRGB" : "linear";
}

} // namespace

const char* vulkanTextureRoleName(VulkanTextureRole role) noexcept
{
    switch (role) {
    case VulkanTextureRole::Unknown: return "unknown";
    case VulkanTextureRole::FallbackWhite: return "fallback-white";
    case VulkanTextureRole::FallbackFlatNormal: return "fallback-flat-normal";
    case VulkanTextureRole::BaseColor: return "baseColor";
    case VulkanTextureRole::Normal: return "normal";
    case VulkanTextureRole::MetallicRoughness: return "metallicRoughness";
    case VulkanTextureRole::Occlusion: return "occlusion";
    case VulkanTextureRole::Emissive: return "emissive";
    case VulkanTextureRole::BrdfLut: return "brdfLut";
    }
    return "unknown";
}

VulkanTextureCache::~VulkanTextureCache()
{
    clear();
}

std::size_t VulkanTextureCache::TextureKeyHash::operator()(const TextureKey& key) const noexcept
{
    auto hash = static_cast<std::size_t>(key.source);
    const auto mix = [&hash](std::size_t value) {
        hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    };
    const auto mixFloat = [&mix](float value) {
        std::uint32_t bits = 0;
        static_assert(sizeof(bits) == sizeof(value));
        std::memcpy(&bits, &value, sizeof(value));
        mix(static_cast<std::size_t>(bits));
    };
    mix(static_cast<std::size_t>(key.colorSpace));
    mix(static_cast<std::size_t>(key.gpuFormat));
    mix(static_cast<std::size_t>(key.sampler.magnificationFilter));
    mix(static_cast<std::size_t>(key.sampler.minificationFilter));
    mix(static_cast<std::size_t>(key.sampler.mipmapFilter));
    mix(static_cast<std::size_t>(key.sampler.wrapU));
    mix(static_cast<std::size_t>(key.sampler.wrapV));
    mix(key.sampler.useMipmaps ? 1U : 0U);
    mix(key.samplerAnisotropyEnabled ? 1U : 0U);
    mixFloat(key.maxSamplerAnisotropy);
    mixFloat(key.mipLodBias);
    mix(key.forceMaxLodZero ? 1U : 0U);
    mix(key.forceAnisotropyOff ? 1U : 0U);
    mix(key.forceAnisotropyOn ? 1U : 0U);
    mix(static_cast<std::size_t>(key.samplerPolicyRevision));
    return hash;
}

VulkanTextureCache::TextureKey VulkanTextureCache::textureKeyFor(
    VulkanResourceContext context,
    std::uint64_t source,
    VulkanTextureColorSpace colorSpace,
    assets::TextureGpuFormat gpuFormat,
    const assets::TextureSamplerAsset& sampler) noexcept
{
    TextureKey key;
    key.source = source;
    key.colorSpace = colorSpace;
    key.gpuFormat = gpuFormat;
    key.sampler = sampler;
    key.samplerAnisotropyEnabled = context.samplerAnisotropyEnabled;
    key.maxSamplerAnisotropy = std::max(1.0F, context.maxSamplerAnisotropy);
    key.mipLodBias = std::clamp(context.textureMipLodBias, -1.0F, 1.0F);
    key.forceMaxLodZero = context.forceSamplerMaxLodZero;
    key.forceAnisotropyOff = context.forceSamplerAnisotropyOff;
    key.forceAnisotropyOn = context.forceSamplerAnisotropyOn;
    key.samplerPolicyRevision = context.samplerPolicyRevision;
    return key;
}

const VulkanTextureHandle* VulkanTextureCache::ensureUploaded(
    VulkanResourceContext context,
    VulkanUploadContext& uploads,
    const assets::TextureAsset* texture,
    std::string* errorMessage,
    VulkanTextureRole role)
{
    if (textureHasGpuMips(texture)) {
        const auto key = textureKeyFor(
            context,
            texture->id.value(),
            VulkanTextureColorSpace::Linear,
            texture->gpuFormat,
            texture->sampler);
        if (const auto existing = textures_.find(key); existing != textures_.end()) {
            return &existing->second.handle;
        }
        if (const auto uploaded = uploadGpuMipTexture(context, uploads, key, *texture, role, errorMessage);
            uploaded != nullptr || !textureHasRgba8(texture)) {
            return uploaded;
        }
        if (errorMessage != nullptr) {
            errorMessage->clear();
        }
    }
    if (!textureHasRgba8(texture)) {
        return ensureWhiteTexture(context, uploads, errorMessage);
    }
    const auto key = textureKeyFor(
        context,
        texture->id.value(),
        VulkanTextureColorSpace::Linear,
        assets::TextureGpuFormat::Rgba8Unorm,
        texture->sampler);
    if (const auto existing = textures_.find(key); existing != textures_.end()) {
        return &existing->second.handle;
    }
    return uploadTexture(
        context,
        uploads,
        key,
        texture->width,
        texture->height,
        texture->rgba8.data(),
        texture->rgba8.size(),
        texture,
        role,
        errorMessage);
}

const VulkanTextureHandle* VulkanTextureCache::ensureSrgbUploaded(
    VulkanResourceContext context,
    VulkanUploadContext& uploads,
    const assets::TextureAsset* texture,
    std::string* errorMessage,
    VulkanTextureRole role)
{
    if (textureHasGpuMips(texture)) {
        const auto key = textureKeyFor(
            context,
            texture->id.value(),
            VulkanTextureColorSpace::Linear,
            texture->gpuFormat,
            texture->sampler);
        if (const auto existing = textures_.find(key); existing != textures_.end()) {
            return &existing->second.handle;
        }
        if (const auto uploaded = uploadGpuMipTexture(context, uploads, key, *texture, role, errorMessage);
            uploaded != nullptr || !textureHasRgba8(texture)) {
            return uploaded;
        }
        if (errorMessage != nullptr) {
            errorMessage->clear();
        }
    }
    if (!textureHasRgba8(texture)) {
        return ensureWhiteTexture(context, uploads, errorMessage);
    }
    const auto key = textureKeyFor(
        context,
        texture->id.value(),
        VulkanTextureColorSpace::Srgb,
        assets::TextureGpuFormat::Rgba8Srgb,
        texture->sampler);
    if (const auto existing = textures_.find(key); existing != textures_.end()) {
        return &existing->second.handle;
    }
    return uploadTexture(
        context,
        uploads,
        key,
        texture->width,
        texture->height,
        texture->rgba8.data(),
        texture->rgba8.size(),
        texture,
        role,
        errorMessage);
}

const VulkanTextureHandle* VulkanTextureCache::ensureNormalUploaded(
    VulkanResourceContext context,
    VulkanUploadContext& uploads,
    const assets::TextureAsset* texture,
    std::string* errorMessage,
    VulkanTextureRole role)
{
    if (!textureHasGpuMips(texture) && !textureHasRgba8(texture)) {
        return ensureFlatNormalTexture(context, uploads, errorMessage);
    }
    return ensureUploaded(context, uploads, texture, errorMessage, role);
}

const VulkanTextureHandle* VulkanTextureCache::ensureBrdfLutUploaded(
    VulkanResourceContext context,
    VulkanUploadContext& uploads,
    std::string* errorMessage)
{
    assets::TextureSamplerAsset sampler;
    sampler.wrapU = assets::TextureWrapMode::ClampToEdge;
    sampler.wrapV = assets::TextureWrapMode::ClampToEdge;
    sampler.useMipmaps = false;
    const auto key = textureKeyFor(
        context,
        kBrdfLutTextureKey,
        VulkanTextureColorSpace::Linear,
        assets::TextureGpuFormat::Rgba8Unorm,
        sampler);
    if (const auto existing = textures_.find(key); existing != textures_.end()) {
        return &existing->second.handle;
    }
    const auto lut = generateBrdfIntegrationLut(128U, 128U);
    return uploadTexture(
        context,
        uploads,
        key,
        lut.width,
        lut.height,
        lut.rgba8.data(),
        lut.rgba8.size(),
        nullptr,
        VulkanTextureRole::BrdfLut,
        errorMessage);
}

void VulkanTextureCache::clear() noexcept
{
    for (auto& [key, texture] : textures_) {
        (void)key;
        destroy(texture);
    }
    textures_.clear();
    destroy(irradianceCube_);
    destroy(prefilteredEnvironmentCube_);
    irradianceCubeEnvironmentKey_ = 0;
    prefilteredEnvironmentCubeEnvironmentKey_ = 0;
    nextHandleKey_ = 1;
    uploadCount_ = 0;
    uploadedBytes_ = 0;
    samplerDiagnostics_ = {};
}

void VulkanTextureCache::invalidateTextureSamplers() noexcept
{
    clear();
    ++samplerPolicyRevision_;
}

void VulkanTextureCache::recreateSamplersForQualityChange() noexcept
{
    invalidateTextureSamplers();
}

std::uint64_t VulkanTextureCache::samplerPolicyRevision() const noexcept
{
    return samplerPolicyRevision_;
}

const VulkanTextureHandle* VulkanTextureCache::ensureWhiteTexture(
    VulkanResourceContext context,
    VulkanUploadContext& uploads,
    std::string* errorMessage)
{
    const auto key = textureKeyFor(
        context,
        kWhiteTextureKey,
        VulkanTextureColorSpace::Linear,
        assets::TextureGpuFormat::Rgba8Unorm,
        {});
    if (const auto existing = textures_.find(key); existing != textures_.end()) {
        return &existing->second.handle;
    }
    constexpr std::array<std::uint8_t, 4> white {255U, 255U, 255U, 255U};
    return uploadTexture(
        context,
        uploads,
        key,
        1,
        1,
        white.data(),
        white.size(),
        nullptr,
        VulkanTextureRole::FallbackWhite,
        errorMessage);
}

const VulkanTextureHandle* VulkanTextureCache::ensureFlatNormalTexture(
    VulkanResourceContext context,
    VulkanUploadContext& uploads,
    std::string* errorMessage)
{
    const auto key = textureKeyFor(
        context,
        kFlatNormalTextureKey,
        VulkanTextureColorSpace::Linear,
        assets::TextureGpuFormat::Rgba8Unorm,
        {});
    if (const auto existing = textures_.find(key); existing != textures_.end()) {
        return &existing->second.handle;
    }
    constexpr std::array<std::uint8_t, 4> flatNormal {128U, 128U, 255U, 255U};
    return uploadTexture(
        context,
        uploads,
        key,
        1,
        1,
        flatNormal.data(),
        flatNormal.size(),
        nullptr,
        VulkanTextureRole::FallbackFlatNormal,
        errorMessage);
}

const VulkanTextureHandle* VulkanTextureCache::uploadTexture(
    VulkanResourceContext context,
    VulkanUploadContext& uploads,
    TextureKey key,
    std::uint32_t width,
    std::uint32_t height,
    const std::uint8_t* rgba8,
    std::size_t byteCount,
    const assets::TextureAsset* sourceTexture,
    VulkanTextureRole role,
    std::string* errorMessage)
{
    TextureStagingBuffer staging;
    staging.context = context;
    VkBufferCreateInfo stagingBufferInfo {};
    stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufferInfo.size = static_cast<VkDeviceSize>(byteCount);
    stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo stagingAlloc {};
    stagingAlloc.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAlloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    if (vmaCreateBuffer(
            context.allocator,
            &stagingBufferInfo,
            &stagingAlloc,
            &staging.buffer,
            &staging.allocation,
            &staging.info)
        != VK_SUCCESS
        || staging.info.pMappedData == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to create Vulkan texture staging buffer";
        }
        return nullptr;
    }
    std::memcpy(staging.info.pMappedData, rgba8, byteCount);
    vmaFlushAllocation(context.allocator, staging.allocation, 0, byteCount);

    TextureResource next;
    next.context = context;
    next.handle.key = nextHandleKey_++;
    const auto format = textureFormat(key.colorSpace);
    const auto mipLevels = key.sampler.useMipmaps && supportsLinearMipBlit(context.physicalDevice, format)
        ? mipLevelCount(width, height)
        : 1U;
    VkImageCreateInfo imageInfo {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (mipLevels > 1U) {
        imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo imageAlloc {};
    imageAlloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (vmaCreateImage(context.allocator, &imageInfo, &imageAlloc, &next.handle.image, &next.allocation, nullptr) != VK_SUCCESS) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to create Vulkan texture image";
        }
        return nullptr;
    }

    if (!uploads.submit([&](VkCommandBuffer commandBuffer) {
            auto toTransfer = imageBarrier(
                next.handle.image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                0,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                0,
                mipLevels);
            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                0,
                0,
                nullptr,
                0,
                nullptr,
                1,
                &toTransfer);

            VkBufferImageCopy copy {};
            copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.imageSubresource.layerCount = 1;
            copy.imageExtent = {width, height, 1};
            vkCmdCopyBufferToImage(
                commandBuffer,
                staging.buffer,
                next.handle.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1,
                &copy);

            if (mipLevels == 1U) {
                auto toShader = imageBarrier(
                    next.handle.image,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_SHADER_READ_BIT,
                    0,
                    1);
                vkCmdPipelineBarrier(
                    commandBuffer,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0,
                    0,
                    nullptr,
                    0,
                    nullptr,
                    1,
                    &toShader);
                return;
            }

            std::int32_t sourceWidth = static_cast<std::int32_t>(width);
            std::int32_t sourceHeight = static_cast<std::int32_t>(height);
            for (std::uint32_t level = 1; level < mipLevels; ++level) {
                auto sourceToBlit = imageBarrier(
                    next.handle.image,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_TRANSFER_READ_BIT,
                    level - 1U,
                    1);
                vkCmdPipelineBarrier(
                    commandBuffer,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0,
                    0,
                    nullptr,
                    0,
                    nullptr,
                    1,
                    &sourceToBlit);

                const auto destinationWidth = std::max(sourceWidth / 2, 1);
                const auto destinationHeight = std::max(sourceHeight / 2, 1);
                VkImageBlit blit {};
                blit.srcOffsets[1] = {sourceWidth, sourceHeight, 1};
                blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.srcSubresource.mipLevel = level - 1U;
                blit.srcSubresource.layerCount = 1;
                blit.dstOffsets[1] = {destinationWidth, destinationHeight, 1};
                blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.dstSubresource.mipLevel = level;
                blit.dstSubresource.layerCount = 1;
                vkCmdBlitImage(
                    commandBuffer,
                    next.handle.image,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    next.handle.image,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1,
                    &blit,
                    VK_FILTER_LINEAR);

                auto sourceToShader = imageBarrier(
                    next.handle.image,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_ACCESS_TRANSFER_READ_BIT,
                    VK_ACCESS_SHADER_READ_BIT,
                    level - 1U,
                    1);
                vkCmdPipelineBarrier(
                    commandBuffer,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0,
                    0,
                    nullptr,
                    0,
                    nullptr,
                    1,
                    &sourceToShader);
                sourceWidth = destinationWidth;
                sourceHeight = destinationHeight;
            }

            auto lastToShader = imageBarrier(
                next.handle.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT,
                mipLevels - 1U,
                1);
            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0,
                0,
                nullptr,
                0,
                nullptr,
                1,
                &lastToShader);
        },
        errorMessage)) {
        destroy(next);
        return nullptr;
    }

    VkImageViewCreateInfo viewInfo {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = next.handle.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(context.device, &viewInfo, nullptr, &next.handle.view) != VK_SUCCESS) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to create Vulkan texture image view";
        }
        destroy(next);
        return nullptr;
    }

    const auto samplerState = buildTextureSamplerState(
        {
            context.samplerAnisotropyEnabled,
            context.maxSamplerAnisotropy,
            context.textureMipLodBias,
            context.forceSamplerMaxLodZero,
            context.forceSamplerAnisotropyOff,
            context.forceSamplerAnisotropyOn,
        },
        key.sampler,
        mipLevels);
    next.handle.width = width;
    next.handle.height = height;
    next.handle.mipLevels = mipLevels;
    next.handle.role = role;
    next.handle.colorSpace = key.colorSpace;
    next.handle.samplerState = samplerState;
    next.handle.samplerPolicyRevision = key.samplerPolicyRevision;
    next.handle.samplerRecreatedAfterSettingsChange = key.samplerPolicyRevision > 0U;
    VkSamplerCreateInfo samplerInfo {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = samplerState.magFilter;
    samplerInfo.minFilter = samplerState.minFilter;
    samplerInfo.mipmapMode = samplerState.mipmapMode;
    samplerInfo.addressModeU = samplerState.addressModeU;
    samplerInfo.addressModeV = samplerState.addressModeV;
    samplerInfo.addressModeW = samplerState.addressModeW;
    samplerInfo.mipLodBias = samplerState.mipLodBias;
    samplerInfo.anisotropyEnable = samplerState.anisotropyEnable;
    samplerInfo.maxAnisotropy = samplerState.maxAnisotropy;
    samplerInfo.minLod = samplerState.minLod;
    samplerInfo.maxLod = samplerState.maxLod;
    if (vkCreateSampler(context.device, &samplerInfo, nullptr, &next.handle.sampler) != VK_SUCCESS) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to create Vulkan texture sampler";
        }
        destroy(next);
        return nullptr;
    }

    const auto [it, inserted] = textures_.try_emplace(key, std::move(next));
    if (inserted) {
        ++uploadCount_;
        uploadedBytes_ += static_cast<std::uint64_t>(byteCount);
        recordSamplerDiagnostics(
            sourceTexture,
            role,
            width,
            height,
            mipLevels,
            samplerState,
            key.colorSpace,
            it->second.handle.key,
            key.samplerPolicyRevision,
            key.samplerPolicyRevision > 0U);
    }
    return inserted ? &it->second.handle : nullptr;
}

const VulkanTextureHandle* VulkanTextureCache::uploadGpuMipTexture(
    VulkanResourceContext context,
    VulkanUploadContext& uploads,
    TextureKey key,
    const assets::TextureAsset& texture,
    VulkanTextureRole role,
    std::string* errorMessage)
{
    const auto format = toVkTextureFormat(texture.gpuFormat);
    if (!format.has_value() || !textureFormatCanBeSampled(context.physicalDevice, *format)) {
        if (errorMessage != nullptr) {
            *errorMessage = "Vulkan device does not support this KTX/KTX2 texture format for sampled images";
        }
        return nullptr;
    }
    std::size_t byteCount = 0;
    for (const auto& mip : texture.gpuMipLevels) {
        byteCount += mip.bytes.size();
    }
    if (byteCount == 0U) {
        if (errorMessage != nullptr) {
            *errorMessage = "KTX/KTX2 texture has no uploadable mip data";
        }
        return nullptr;
    }

    TextureStagingBuffer staging;
    staging.context = context;
    VkBufferCreateInfo stagingBufferInfo {};
    stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufferInfo.size = static_cast<VkDeviceSize>(byteCount);
    stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo stagingAlloc {};
    stagingAlloc.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAlloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    if (vmaCreateBuffer(context.allocator, &stagingBufferInfo, &stagingAlloc, &staging.buffer, &staging.allocation, &staging.info)
            != VK_SUCCESS
        || staging.info.pMappedData == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to create Vulkan KTX texture staging buffer";
        }
        return nullptr;
    }
    auto* mapped = static_cast<std::uint8_t*>(staging.info.pMappedData);
    std::vector<VkBufferImageCopy> copies;
    copies.reserve(texture.gpuMipLevels.size());
    VkDeviceSize offset = 0;
    for (std::uint32_t level = 0; level < texture.gpuMipLevels.size(); ++level) {
        const auto& mip = texture.gpuMipLevels[level];
        std::memcpy(mapped + offset, mip.bytes.data(), mip.bytes.size());
        VkBufferImageCopy copy {};
        copy.bufferOffset = offset;
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.mipLevel = level;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {mip.width, mip.height, 1};
        copies.push_back(copy);
        offset += static_cast<VkDeviceSize>(mip.bytes.size());
    }
    vmaFlushAllocation(context.allocator, staging.allocation, 0, byteCount);

    TextureResource next;
    next.context = context;
    next.handle.key = nextHandleKey_++;
    const auto mipLevels = static_cast<std::uint32_t>(texture.gpuMipLevels.size());
    VkImageCreateInfo imageInfo {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = *format;
    imageInfo.extent = {texture.width, texture.height, 1};
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo imageAlloc {};
    imageAlloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (vmaCreateImage(context.allocator, &imageInfo, &imageAlloc, &next.handle.image, &next.allocation, nullptr) != VK_SUCCESS) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to create Vulkan KTX/KTX2 texture image";
        }
        return nullptr;
    }

    if (!uploads.submit([&](VkCommandBuffer commandBuffer) {
            auto toTransfer = imageBarrier(
                next.handle.image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                0,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                0,
                mipLevels);
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toTransfer);
            vkCmdCopyBufferToImage(
                commandBuffer,
                staging.buffer,
                next.handle.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                static_cast<std::uint32_t>(copies.size()),
                copies.data());
            auto toShader = imageBarrier(
                next.handle.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT,
                0,
                mipLevels);
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toShader);
        },
        errorMessage)) {
        destroy(next);
        return nullptr;
    }

    VkImageViewCreateInfo viewInfo {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = next.handle.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = *format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(context.device, &viewInfo, nullptr, &next.handle.view) != VK_SUCCESS) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to create Vulkan KTX/KTX2 texture image view";
        }
        destroy(next);
        return nullptr;
    }

    const auto samplerState = buildTextureSamplerState(
        {
            context.samplerAnisotropyEnabled,
            context.maxSamplerAnisotropy,
            context.textureMipLodBias,
            context.forceSamplerMaxLodZero,
            context.forceSamplerAnisotropyOff,
            context.forceSamplerAnisotropyOn,
        },
        key.sampler,
        mipLevels);
    next.handle.width = texture.width;
    next.handle.height = texture.height;
    next.handle.mipLevels = mipLevels;
    next.handle.role = role;
    next.handle.colorSpace = key.colorSpace;
    next.handle.samplerState = samplerState;
    next.handle.samplerPolicyRevision = key.samplerPolicyRevision;
    next.handle.samplerRecreatedAfterSettingsChange = key.samplerPolicyRevision > 0U;
    VkSamplerCreateInfo samplerInfo {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = samplerState.magFilter;
    samplerInfo.minFilter = samplerState.minFilter;
    samplerInfo.mipmapMode = samplerState.mipmapMode;
    samplerInfo.addressModeU = samplerState.addressModeU;
    samplerInfo.addressModeV = samplerState.addressModeV;
    samplerInfo.addressModeW = samplerState.addressModeW;
    samplerInfo.mipLodBias = samplerState.mipLodBias;
    samplerInfo.anisotropyEnable = samplerState.anisotropyEnable;
    samplerInfo.maxAnisotropy = samplerState.maxAnisotropy;
    samplerInfo.minLod = samplerState.minLod;
    samplerInfo.maxLod = samplerState.maxLod;
    if (vkCreateSampler(context.device, &samplerInfo, nullptr, &next.handle.sampler) != VK_SUCCESS) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to create Vulkan KTX/KTX2 texture sampler";
        }
        destroy(next);
        return nullptr;
    }

    const auto [it, inserted] = textures_.try_emplace(key, std::move(next));
    if (inserted) {
        ++uploadCount_;
        uploadedBytes_ += static_cast<std::uint64_t>(byteCount);
        recordSamplerDiagnostics(
            &texture,
            role,
            texture.width,
            texture.height,
            mipLevels,
            samplerState,
            key.colorSpace,
            it->second.handle.key,
            key.samplerPolicyRevision,
            key.samplerPolicyRevision > 0U);
    }
    return inserted ? &it->second.handle : nullptr;
}

std::uint64_t VulkanTextureCache::uploadCount() const noexcept
{
    return uploadCount_;
}

std::uint64_t VulkanTextureCache::uploadedBytes() const noexcept
{
    return uploadedBytes_;
}

std::uint64_t VulkanTextureCache::textureCount() const noexcept
{
    return static_cast<std::uint64_t>(textures_.size());
}

const VulkanTextureSamplerDiagnostics& VulkanTextureCache::samplerDiagnostics() const noexcept
{
    return samplerDiagnostics_;
}

void VulkanTextureCache::recordSamplerDiagnostics(
    const assets::TextureAsset* sourceTexture,
    VulkanTextureRole role,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t mipLevels,
    const VulkanTextureSamplerState& samplerState,
    VulkanTextureColorSpace colorSpace,
    std::uint64_t handleKey,
    std::uint64_t samplerPolicyRevision,
    bool samplerRecreatedAfterSettingsChange)
{
    ++samplerDiagnostics_.samplerCreateCount;
    if (samplerState.anisotropyEnable == VK_TRUE) {
        ++samplerDiagnostics_.anisotropicSamplerCount;
    }
    if (samplerState.minFilter == VK_FILTER_LINEAR
        && samplerState.mipmapMode == VK_SAMPLER_MIPMAP_MODE_LINEAR
        && samplerState.maxLod > 0.0F) {
        ++samplerDiagnostics_.trilinearSamplerCount;
    }

    samplerDiagnostics_.lastTextureName = sourceTexture != nullptr && !sourceTexture->name.empty()
        ? sourceTexture->name
        : vulkanTextureRoleName(role);
    samplerDiagnostics_.lastTextureRole = vulkanTextureRoleName(role);
    samplerDiagnostics_.lastWidth = width;
    samplerDiagnostics_.lastHeight = height;
    samplerDiagnostics_.lastMipLevels = mipLevels;
    samplerDiagnostics_.lastAnisotropyEnabled = samplerState.anisotropyEnable == VK_TRUE;
    samplerDiagnostics_.lastMaxAnisotropy = samplerState.maxAnisotropy;
    samplerDiagnostics_.lastMipLodBias = samplerState.mipLodBias;
    samplerDiagnostics_.lastMinLod = samplerState.minLod;
    samplerDiagnostics_.lastMaxLod = samplerState.maxLod;
    samplerDiagnostics_.lastTextureHandleKey = handleKey;
    samplerDiagnostics_.lastSamplerPolicyRevision = samplerPolicyRevision;
    samplerDiagnostics_.lastSamplerRecreatedAfterSettingsChange = samplerRecreatedAfterSettingsChange;

    std::ostringstream message;
    message << "Vulkan texture sampler"
            << " role=" << vulkanTextureRoleName(role)
            << " name=" << samplerDiagnostics_.lastTextureName
            << " id=" << (sourceTexture != nullptr ? sourceTexture->id.value() : 0ULL)
            << " handleKey=" << handleKey
            << " samplerPolicyRevision=" << samplerPolicyRevision
            << " samplerRecreatedAfterSettingsChange=" << (samplerRecreatedAfterSettingsChange ? "yes" : "no")
            << " resolution=" << width << "x" << height
            << " mipLevels=" << mipLevels
            << " minFilter=" << filterName(samplerState.minFilter)
            << " magFilter=" << filterName(samplerState.magFilter)
            << " mipmapMode=" << mipmapModeName(samplerState.mipmapMode)
            << " anisotropy=" << (samplerState.anisotropyEnable == VK_TRUE ? "on" : "off")
            << " maxAnisotropy=" << samplerState.maxAnisotropy
            << " mipLodBias=" << samplerState.mipLodBias
            << " minLod=" << samplerState.minLod
            << " maxLod=" << samplerState.maxLod
            << " wrapU=" << addressModeName(samplerState.addressModeU)
            << " wrapV=" << addressModeName(samplerState.addressModeV)
            << " colorSpace=" << colorSpaceName(colorSpace);
    core::logInfo(core::LogCategory::Renderer, message.str());
}

void VulkanTextureCache::destroy(TextureResource& texture) noexcept
{
    if (texture.handle.sampler != VK_NULL_HANDLE) {
        vkDestroySampler(texture.context.device, texture.handle.sampler, nullptr);
        texture.handle.sampler = VK_NULL_HANDLE;
    }
    if (texture.handle.view != VK_NULL_HANDLE) {
        vkDestroyImageView(texture.context.device, texture.handle.view, nullptr);
        texture.handle.view = VK_NULL_HANDLE;
    }
    if (texture.handle.image != VK_NULL_HANDLE) {
        vmaDestroyImage(texture.context.allocator, texture.handle.image, texture.allocation);
        texture.handle.image = VK_NULL_HANDLE;
        texture.allocation = VK_NULL_HANDLE;
    }
}

} // namespace projectunity::renderer
