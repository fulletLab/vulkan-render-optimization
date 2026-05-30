#include "VulkanGpuBuffer.hpp"

#include <cstring>
#include <utility>

namespace projectunity::renderer {
namespace {

struct StagingBuffer {
    VulkanResourceContext context;
    VkBuffer buffer {VK_NULL_HANDLE};
    VmaAllocation allocation {VK_NULL_HANDLE};
    VmaAllocationInfo info {};

    ~StagingBuffer()
    {
        if (buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(context.allocator, buffer, allocation);
        }
    }
};

[[nodiscard]] bool createBuffer(
    VulkanResourceContext context,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    const VmaAllocationCreateInfo& allocationInfo,
    VkBuffer* buffer,
    VmaAllocation* allocation,
    VmaAllocationInfo* info)
{
    VkBufferCreateInfo bufferInfo {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    return vmaCreateBuffer(context.allocator, &bufferInfo, &allocationInfo, buffer, allocation, info) == VK_SUCCESS;
}

} // namespace

VulkanGpuBuffer::~VulkanGpuBuffer()
{
    destroy();
}

VulkanGpuBuffer::VulkanGpuBuffer(VulkanGpuBuffer&& other) noexcept
    : context_(other.context_)
    , buffer_(std::exchange(other.buffer_, VK_NULL_HANDLE))
    , allocation_(std::exchange(other.allocation_, VK_NULL_HANDLE))
    , mappedData_(std::exchange(other.mappedData_, nullptr))
    , size_(std::exchange(other.size_, 0))
{
}

VulkanGpuBuffer& VulkanGpuBuffer::operator=(VulkanGpuBuffer&& other) noexcept
{
    if (this == &other) {
        return *this;
    }
    destroy();
    context_ = other.context_;
    buffer_ = std::exchange(other.buffer_, VK_NULL_HANDLE);
    allocation_ = std::exchange(other.allocation_, VK_NULL_HANDLE);
    mappedData_ = std::exchange(other.mappedData_, nullptr);
    size_ = std::exchange(other.size_, 0);
    return *this;
}

bool VulkanGpuBuffer::upload(
    VulkanResourceContext context,
    VulkanUploadContext& uploads,
    VkBufferUsageFlags usage,
    std::span<const std::byte> bytes,
    std::string* errorMessage)
{
    if (bytes.empty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Vulkan GPU buffer upload has no bytes";
        }
        return false;
    }

    StagingBuffer staging;
    staging.context = context;
    VmaAllocationCreateInfo stagingAlloc {};
    stagingAlloc.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAlloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
        | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    if (!createBuffer(
            context,
            static_cast<VkDeviceSize>(bytes.size()),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            stagingAlloc,
            &staging.buffer,
            &staging.allocation,
            &staging.info)
        || staging.info.pMappedData == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to create Vulkan GPU staging buffer";
        }
        return false;
    }
    std::memcpy(staging.info.pMappedData, bytes.data(), bytes.size());
    vmaFlushAllocation(context.allocator, staging.allocation, 0, bytes.size());

    VkBuffer nextBuffer = VK_NULL_HANDLE;
    VmaAllocation nextAllocation = VK_NULL_HANDLE;
    VmaAllocationCreateInfo deviceAlloc {};
    deviceAlloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (!createBuffer(
            context,
            static_cast<VkDeviceSize>(bytes.size()),
            usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            deviceAlloc,
            &nextBuffer,
            &nextAllocation,
            nullptr)) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to create Vulkan GPU device buffer";
        }
        return false;
    }

    const auto uploaded = uploads.submit([&](VkCommandBuffer commandBuffer) {
        VkBufferCopy copy {};
        copy.size = static_cast<VkDeviceSize>(bytes.size());
        vkCmdCopyBuffer(commandBuffer, staging.buffer, nextBuffer, 1, &copy);
    }, errorMessage);
    if (!uploaded) {
        vmaDestroyBuffer(context.allocator, nextBuffer, nextAllocation);
        return false;
    }

    destroy();
    context_ = context;
    buffer_ = nextBuffer;
    allocation_ = nextAllocation;
    mappedData_ = nullptr;
    size_ = static_cast<VkDeviceSize>(bytes.size());
    return true;
}

bool VulkanGpuBuffer::writeMapped(
    VulkanResourceContext context,
    VkBufferUsageFlags usage,
    std::span<const std::byte> bytes,
    std::string* errorMessage)
{
    if (bytes.empty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Vulkan mapped buffer write has no bytes";
        }
        return false;
    }

    if (buffer_ == VK_NULL_HANDLE || mappedData_ == nullptr || size_ < bytes.size()) {
        destroy();
        VkDeviceSize capacity = 1;
        while (capacity < static_cast<VkDeviceSize>(bytes.size())) {
            capacity *= 2;
        }
        VmaAllocationCreateInfo allocationInfo {};
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
            | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo info {};
        if (!createBuffer(context, capacity, usage, allocationInfo, &buffer_, &allocation_, &info)
            || info.pMappedData == nullptr) {
            buffer_ = VK_NULL_HANDLE;
            allocation_ = VK_NULL_HANDLE;
            if (errorMessage != nullptr) {
                *errorMessage = "Failed to create Vulkan mapped buffer";
            }
            return false;
        }
        context_ = context;
        mappedData_ = info.pMappedData;
        size_ = capacity;
    }

    std::memcpy(mappedData_, bytes.data(), bytes.size());
    vmaFlushAllocation(context_.allocator, allocation_, 0, bytes.size());
    return true;
}

void VulkanGpuBuffer::destroy() noexcept
{
    if (buffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(context_.allocator, buffer_, allocation_);
        buffer_ = VK_NULL_HANDLE;
        allocation_ = VK_NULL_HANDLE;
        mappedData_ = nullptr;
        size_ = 0;
    }
}

VkBuffer VulkanGpuBuffer::buffer() const noexcept
{
    return buffer_;
}

VkDeviceSize VulkanGpuBuffer::size() const noexcept
{
    return size_;
}

} // namespace projectunity::renderer
