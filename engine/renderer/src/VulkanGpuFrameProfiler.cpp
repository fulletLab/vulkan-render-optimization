#include "VulkanGpuFrameProfiler.hpp"

#include <cstddef>
#include <vector>

namespace projectunity::renderer {

VulkanGpuFrameProfiler::~VulkanGpuFrameProfiler()
{
    destroy();
}

bool VulkanGpuFrameProfiler::create(VulkanResourceContext context, std::string* errorMessage)
{
    context_ = context;
    lastTimes_ = {};

    std::uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(context_.physicalDevice, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(context_.physicalDevice, &familyCount, families.data());

    VkPhysicalDeviceProperties properties {};
    vkGetPhysicalDeviceProperties(context_.physicalDevice, &properties);
    supported_ = context_.queueFamilyIndex < families.size()
        && families[context_.queueFamilyIndex].timestampValidBits > 0
        && properties.limits.timestampPeriod > 0.0F;
    lastTimes_.supported = supported_;
    timestampPeriodNs_ = properties.limits.timestampPeriod;

    if (!supported_) {
        return true;
    }

    VkQueryPoolCreateInfo info {};
    info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = kTimestampCount;
    if (vkCreateQueryPool(context_.device, &info, nullptr, &queryPool_) != VK_SUCCESS) {
        supported_ = false;
        lastTimes_.supported = false;
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to create Vulkan GPU timestamp query pool";
        }
        return false;
    }

    return true;
}

void VulkanGpuFrameProfiler::destroy() noexcept
{
    if (queryPool_ != VK_NULL_HANDLE) {
        vkDestroyQueryPool(context_.device, queryPool_, nullptr);
        queryPool_ = VK_NULL_HANDLE;
    }
    supported_ = false;
    pendingResults_ = false;
    recordingTimestamps_ = false;
    lastTimes_ = {};
}

void VulkanGpuFrameProfiler::collect() noexcept
{
    lastTimes_.supported = supported_;
    lastTimes_.valid = false;
    if (!supported_ || !pendingResults_ || queryPool_ == VK_NULL_HANDLE) {
        return;
    }

    std::array<std::uint64_t, kTimestampCount> timestamps {};
    const auto result = vkGetQueryPoolResults(
        context_.device,
        queryPool_,
        0,
        kTimestampCount,
        sizeof(timestamps),
        timestamps.data(),
        sizeof(timestamps[0]),
        VK_QUERY_RESULT_64_BIT);
    pendingResults_ = false;
    if (result != VK_SUCCESS) {
        return;
    }

    const auto index = [](VulkanGpuFrameTimestamp timestamp) {
        return static_cast<std::size_t>(timestamp);
    };
    lastTimes_.valid = true;
    lastTimes_.frameGpuTimeUs = deltaUs(timestamps[index(VulkanGpuFrameTimestamp::FrameStart)], timestamps[index(VulkanGpuFrameTimestamp::FrameEnd)]);
    lastTimes_.shadowGpuTimeUs = deltaUs(timestamps[index(VulkanGpuFrameTimestamp::ShadowStart)], timestamps[index(VulkanGpuFrameTimestamp::ShadowEnd)]);
    lastTimes_.meshGpuTimeUs = deltaUs(timestamps[index(VulkanGpuFrameTimestamp::MeshStart)], timestamps[index(VulkanGpuFrameTimestamp::MeshEnd)]);
    lastTimes_.colorGpuTimeUs = deltaUs(timestamps[index(VulkanGpuFrameTimestamp::ColorStart)], timestamps[index(VulkanGpuFrameTimestamp::ColorEnd)]);
}

void VulkanGpuFrameProfiler::beginFrame(VkCommandBuffer commandBuffer) noexcept
{
    if (!supported_ || queryPool_ == VK_NULL_HANDLE) {
        return;
    }
    vkCmdResetQueryPool(commandBuffer, queryPool_, 0, kTimestampCount);
    write(commandBuffer, VulkanGpuFrameTimestamp::FrameStart, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    recordingTimestamps_ = true;
}

void VulkanGpuFrameProfiler::write(
    VkCommandBuffer commandBuffer,
    VulkanGpuFrameTimestamp timestamp,
    VkPipelineStageFlagBits stage) noexcept
{
    if (!supported_ || queryPool_ == VK_NULL_HANDLE) {
        return;
    }
    vkCmdWriteTimestamp(commandBuffer, stage, queryPool_, static_cast<std::uint32_t>(timestamp));
}

void VulkanGpuFrameProfiler::endFrame(VkCommandBuffer commandBuffer) noexcept
{
    write(commandBuffer, VulkanGpuFrameTimestamp::FrameEnd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
}

void VulkanGpuFrameProfiler::markSubmitted() noexcept
{
    if (!recordingTimestamps_) {
        return;
    }
    pendingResults_ = true;
    recordingTimestamps_ = false;
}

const VulkanGpuFrameTimes& VulkanGpuFrameProfiler::lastTimes() const noexcept
{
    return lastTimes_;
}

std::uint64_t VulkanGpuFrameProfiler::deltaUs(std::uint64_t begin, std::uint64_t end) const noexcept
{
    if (end <= begin || timestampPeriodNs_ <= 0.0F) {
        return 0;
    }
    const auto ticks = static_cast<long double>(end - begin);
    return static_cast<std::uint64_t>((ticks * static_cast<long double>(timestampPeriodNs_)) / 1000.0L);
}

} // namespace projectunity::renderer
