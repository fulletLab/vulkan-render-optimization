#include "VulkanTextureCache.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace projectunity::renderer {
namespace {
constexpr std::uint64_t kIrradianceCubeKeyBase = 0xF100000000000000ULL;
constexpr std::uint64_t kPrefilteredCubeKeyBase = 0xF200000000000000ULL;
constexpr std::size_t kRgba8BytesPerTexel = 4U;
constexpr std::size_t kRgba16fBytesPerTexel = 8U;

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

[[nodiscard]] std::uint64_t quantized(float value)
{
    const auto clamped = std::clamp(value, 0.0F, 16.0F);
    return static_cast<std::uint64_t>(clamped * 4095.0F + 0.5F);
}

[[nodiscard]] bool textureHasEnvironmentPixels(const assets::TextureAsset& texture)
{
    const auto pixelCount = static_cast<std::size_t>(texture.width) * texture.height;
    return texture.width > 0
        && texture.height > 0
        && (texture.rgba8.size() >= pixelCount * 4U
            || texture.rgba32f.size() >= pixelCount * 4U);
}

[[nodiscard]] std::uint64_t environmentKey(const RenderEnvironmentSettings& environment)
{
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    const auto mix = [&hash](std::uint64_t value) {
        hash ^= value;
        hash *= 0x100000001b3ULL;
    };
    const auto* texture = environment.sourceTexture;
    if (texture != nullptr && texture->id.isValid() && textureHasEnvironmentPixels(*texture)) {
        mix(texture->id.value());
        mix(texture->width);
        mix(texture->height);
        mix(static_cast<std::uint64_t>(texture->rgba8.size()));
        mix(static_cast<std::uint64_t>(texture->rgba32f.size()));
    } else {
        for (const auto value : environment.skyColor) {
            mix(quantized(value));
        }
        for (const auto value : environment.groundColor) {
            mix(quantized(value));
        }
    }
    mix(quantized(environment.intensity));
    return hash == 0 ? 1ULL : hash;
}

[[nodiscard]] VkImageMemoryBarrier imageBarrier(
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkAccessFlags sourceAccess,
    VkAccessFlags destinationAccess,
    std::uint32_t mipLevels)
{
    VkImageMemoryBarrier barrier {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.layerCount = 6;
    barrier.srcAccessMask = sourceAccess;
    barrier.dstAccessMask = destinationAccess;
    return barrier;
}

[[nodiscard]] bool supportsEnvironmentFormat(VkPhysicalDevice physicalDevice, VkFormat format)
{
    VkFormatProperties properties {};
    vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &properties);
    constexpr auto required = VK_FORMAT_FEATURE_TRANSFER_DST_BIT
        | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT
        | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    return (properties.optimalTilingFeatures & required) == required;
}

[[nodiscard]] bool mipHasRgba32f(const RenderCubeMip& mip)
{
    return mip.faceSize > 0
        && mip.rgba32f.size() >= static_cast<std::size_t>(mip.faceSize) * mip.faceSize * 6U * 4U;
}

[[nodiscard]] std::uint16_t floatToHalf(float value)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const auto sign = static_cast<std::uint16_t>((bits >> 16U) & 0x8000U);
    auto exponent = static_cast<int>((bits >> 23U) & 0xffU) - 127 + 15;
    auto mantissa = bits & 0x7fffffU;
    if (exponent <= 0) {
        if (exponent < -10) {
            return sign;
        }
        mantissa = (mantissa | 0x800000U) >> static_cast<std::uint32_t>(1 - exponent);
        return static_cast<std::uint16_t>(sign | ((mantissa + 0x1000U) >> 13U));
    }
    if (exponent >= 31) {
        return static_cast<std::uint16_t>(sign | 0x7c00U);
    }
    return static_cast<std::uint16_t>(sign | (static_cast<std::uint16_t>(exponent) << 10U) | ((mantissa + 0x1000U) >> 13U));
}

[[nodiscard]] std::size_t cubeMipBytes(const RenderCubeMip& mip, bool useFloat)
{
    const auto texels = static_cast<std::size_t>(mip.faceSize) * mip.faceSize * 6U;
    return texels * (useFloat ? kRgba16fBytesPerTexel : kRgba8BytesPerTexel);
}
} // namespace

const VulkanTextureHandle* VulkanTextureCache::ensureIrradianceCubeUploaded(
    VulkanResourceContext context,
    VulkanUploadContext& uploads,
    const RenderEnvironmentSettings& environment,
    std::string* errorMessage)
{
    const auto key = environmentKey(environment);
    if (irradianceCube_.handle.image != VK_NULL_HANDLE && irradianceCubeEnvironmentKey_ == key) {
        return &irradianceCube_.handle;
    }
    const auto cube = generateProceduralIrradianceCube(32U, environment);
    const auto handleKey = kIrradianceCubeKeyBase ^ (key & 0x00FFFFFFFFFFFFFFULL);
    if (!uploadCubeMap(context, uploads, irradianceCube_, handleKey, cube, errorMessage)) {
        irradianceCubeEnvironmentKey_ = 0;
        return nullptr;
    }
    irradianceCubeEnvironmentKey_ = key;
    return &irradianceCube_.handle;
}

const VulkanTextureHandle* VulkanTextureCache::ensurePrefilteredEnvironmentCubeUploaded(
    VulkanResourceContext context,
    VulkanUploadContext& uploads,
    const RenderEnvironmentSettings& environment,
    std::string* errorMessage)
{
    const auto key = environmentKey(environment);
    if (prefilteredEnvironmentCube_.handle.image != VK_NULL_HANDLE
        && prefilteredEnvironmentCubeEnvironmentKey_ == key) {
        return &prefilteredEnvironmentCube_.handle;
    }
    const auto cube = generateProceduralPrefilteredCube(128U, 5U, environment);
    const auto handleKey = kPrefilteredCubeKeyBase ^ (key & 0x00FFFFFFFFFFFFFFULL);
    if (!uploadCubeMap(context, uploads, prefilteredEnvironmentCube_, handleKey, cube, errorMessage)) {
        prefilteredEnvironmentCubeEnvironmentKey_ = 0;
        return nullptr;
    }
    prefilteredEnvironmentCubeEnvironmentKey_ = key;
    return &prefilteredEnvironmentCube_.handle;
}

bool VulkanTextureCache::uploadCubeMap(
    VulkanResourceContext context,
    VulkanUploadContext& uploads,
    TextureResource& destination,
    std::uint64_t handleKey,
    const RenderCubeMap& cube,
    std::string* errorMessage)
{
    if (cube.mips.empty() || cube.mips.front().rgba8.empty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Cannot upload an empty Vulkan environment cube map";
        }
        return false;
    }
    const auto useFloat = std::all_of(cube.mips.begin(), cube.mips.end(), mipHasRgba32f)
        && supportsEnvironmentFormat(context.physicalDevice, VK_FORMAT_R16G16B16A16_SFLOAT);
    std::size_t byteCount = 0;
    for (const auto& mip : cube.mips) {
        byteCount += cubeMipBytes(mip, useFloat);
    }

    TextureStagingBuffer staging;
    staging.context = context;
    VkBufferCreateInfo stagingInfo {};
    stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingInfo.size = static_cast<VkDeviceSize>(byteCount);
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo stagingAlloc {};
    stagingAlloc.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAlloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    if (vmaCreateBuffer(context.allocator, &stagingInfo, &stagingAlloc, &staging.buffer, &staging.allocation, &staging.info) != VK_SUCCESS
        || staging.info.pMappedData == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to create Vulkan cube-map staging buffer";
        }
        return false;
    }
    auto* mapped = static_cast<std::uint8_t*>(staging.info.pMappedData);
    for (const auto& mip : cube.mips) {
        if (useFloat) {
            auto* halfMapped = reinterpret_cast<std::uint16_t*>(mapped);
            const auto texelChannels = static_cast<std::size_t>(mip.faceSize) * mip.faceSize * 6U * 4U;
            for (std::size_t channel = 0; channel < texelChannels; ++channel) {
                halfMapped[channel] = floatToHalf(mip.rgba32f[channel]);
            }
        } else {
            std::memcpy(mapped, mip.rgba8.data(), cubeMipBytes(mip, false));
        }
        mapped += cubeMipBytes(mip, useFloat);
    }
    vmaFlushAllocation(context.allocator, staging.allocation, 0, byteCount);

    destroy(destination);
    destination.context = context;
    destination.handle.key = handleKey;
    VkImageCreateInfo imageInfo {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = useFloat ? VK_FORMAT_R16G16B16A16_SFLOAT : VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = {cube.mips.front().faceSize, cube.mips.front().faceSize, 1};
    imageInfo.mipLevels = static_cast<std::uint32_t>(cube.mips.size());
    imageInfo.arrayLayers = 6;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VmaAllocationCreateInfo imageAlloc {};
    imageAlloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (vmaCreateImage(context.allocator, &imageInfo, &imageAlloc, &destination.handle.image, &destination.allocation, nullptr) != VK_SUCCESS) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to create Vulkan environment cube-map image";
        }
        return false;
    }

    if (!uploads.submit([&](VkCommandBuffer commandBuffer) {
            auto toTransfer = imageBarrier(
                destination.handle.image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                0,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                imageInfo.mipLevels);
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toTransfer);
            std::vector<VkBufferImageCopy> copies;
            copies.reserve(cube.mips.size() * 6U);
            VkDeviceSize offset = 0;
            for (std::uint32_t mip = 0; mip < cube.mips.size(); ++mip) {
                const auto& source = cube.mips[mip];
                const auto pixelBytes = useFloat ? kRgba16fBytesPerTexel : kRgba8BytesPerTexel;
                const auto faceBytes = static_cast<VkDeviceSize>(source.faceSize) * source.faceSize * pixelBytes;
                for (std::uint32_t face = 0; face < 6U; ++face) {
                    VkBufferImageCopy copy {};
                    copy.bufferOffset = offset + faceBytes * face;
                    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    copy.imageSubresource.mipLevel = mip;
                    copy.imageSubresource.baseArrayLayer = face;
                    copy.imageSubresource.layerCount = 1;
                    copy.imageExtent = {source.faceSize, source.faceSize, 1};
                    copies.push_back(copy);
                }
                offset += static_cast<VkDeviceSize>(cubeMipBytes(source, useFloat));
            }
            vkCmdCopyBufferToImage(commandBuffer, staging.buffer, destination.handle.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<std::uint32_t>(copies.size()), copies.data());
            auto toShader = imageBarrier(
                destination.handle.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT,
                imageInfo.mipLevels);
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toShader);
        },
        errorMessage)) {
        destroy(destination);
        return false;
    }

    VkImageViewCreateInfo viewInfo {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = destination.handle.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    viewInfo.format = imageInfo.format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = imageInfo.mipLevels;
    viewInfo.subresourceRange.layerCount = 6;
    if (vkCreateImageView(context.device, &viewInfo, nullptr, &destination.handle.view) != VK_SUCCESS) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to create Vulkan environment cube-map image view";
        }
        destroy(destination);
        return false;
    }
    VkSamplerCreateInfo samplerInfo {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = static_cast<float>(imageInfo.mipLevels - 1U);
    if (vkCreateSampler(context.device, &samplerInfo, nullptr, &destination.handle.sampler) != VK_SUCCESS) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to create Vulkan environment cube-map sampler";
        }
        destroy(destination);
        return false;
    }
    ++uploadCount_;
    uploadedBytes_ += static_cast<std::uint64_t>(byteCount);
    return true;
}

} // namespace projectunity::renderer
