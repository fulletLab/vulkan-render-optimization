#include "VulkanTextureCache.hpp"

#include <array>
#include <cstring>

namespace projectunity::renderer {
namespace {

constexpr std::uint64_t kWhiteTextureKey = 1ULL;

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

[[nodiscard]] bool textureUsable(const assets::TextureAsset* texture)
{
    return texture != nullptr
        && texture->id.isValid()
        && texture->width > 0
        && texture->height > 0
        && texture->rgba8.size() >= static_cast<std::size_t>(texture->width) * texture->height * 4U;
}

[[nodiscard]] VkImageMemoryBarrier imageBarrier(
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkAccessFlags sourceAccess,
    VkAccessFlags destinationAccess)
{
    VkImageMemoryBarrier barrier {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = sourceAccess;
    barrier.dstAccessMask = destinationAccess;
    return barrier;
}

} // namespace

VulkanTextureCache::~VulkanTextureCache()
{
    clear();
}

const VulkanTextureHandle* VulkanTextureCache::ensureUploaded(
    VulkanResourceContext context,
    VulkanUploadContext& uploads,
    const assets::TextureAsset* texture,
    std::string* errorMessage)
{
    if (!textureUsable(texture)) {
        return ensureWhiteTexture(context, uploads, errorMessage);
    }
    const auto key = texture->id.value();
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
        errorMessage);
}

void VulkanTextureCache::clear() noexcept
{
    for (auto& [key, texture] : textures_) {
        (void)key;
        destroy(texture);
    }
    textures_.clear();
}

const VulkanTextureHandle* VulkanTextureCache::ensureWhiteTexture(
    VulkanResourceContext context,
    VulkanUploadContext& uploads,
    std::string* errorMessage)
{
    if (const auto existing = textures_.find(kWhiteTextureKey); existing != textures_.end()) {
        return &existing->second.handle;
    }
    constexpr std::array<std::uint8_t, 4> white {255U, 255U, 255U, 255U};
    return uploadTexture(context, uploads, kWhiteTextureKey, 1, 1, white.data(), white.size(), errorMessage);
}

const VulkanTextureHandle* VulkanTextureCache::uploadTexture(
    VulkanResourceContext context,
    VulkanUploadContext& uploads,
    std::uint64_t key,
    std::uint32_t width,
    std::uint32_t height,
    const std::uint8_t* rgba8,
    std::size_t byteCount,
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
    next.handle.key = key;
    VkImageCreateInfo imageInfo {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
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
                VK_ACCESS_TRANSFER_WRITE_BIT);
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

            auto toShader = imageBarrier(
                next.handle.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT);
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
        },
        errorMessage)) {
        destroy(next);
        return nullptr;
    }

    VkImageViewCreateInfo viewInfo {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = next.handle.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(context.device, &viewInfo, nullptr, &next.handle.view) != VK_SUCCESS) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to create Vulkan texture image view";
        }
        destroy(next);
        return nullptr;
    }

    VkSamplerCreateInfo samplerInfo {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.maxLod = 1.0F;
    if (vkCreateSampler(context.device, &samplerInfo, nullptr, &next.handle.sampler) != VK_SUCCESS) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to create Vulkan texture sampler";
        }
        destroy(next);
        return nullptr;
    }

    const auto [it, inserted] = textures_.try_emplace(key, std::move(next));
    return inserted ? &it->second.handle : nullptr;
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
