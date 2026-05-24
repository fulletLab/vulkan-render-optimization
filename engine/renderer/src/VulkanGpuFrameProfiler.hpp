#pragma once

#include "VulkanResourceContext.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace projectunity::renderer {

enum class VulkanGpuFrameTimestamp : std::uint32_t {
    FrameStart,
    ShadowStart,
    ShadowEnd,
    MeshStart,
    MeshEnd,
    ColorStart,
    ColorEnd,
    FrameEnd,
    Count,
};

struct VulkanGpuFrameTimes {
    bool supported {false};
    bool valid {false};
    std::uint64_t frameGpuTimeUs {0};
    std::uint64_t shadowGpuTimeUs {0};
    std::uint64_t meshGpuTimeUs {0};
    std::uint64_t colorGpuTimeUs {0};
};

class VulkanGpuFrameProfiler final {
public:
    VulkanGpuFrameProfiler() = default;
    ~VulkanGpuFrameProfiler();

    VulkanGpuFrameProfiler(const VulkanGpuFrameProfiler&) = delete;
    VulkanGpuFrameProfiler& operator=(const VulkanGpuFrameProfiler&) = delete;

    [[nodiscard]] bool create(VulkanResourceContext context, std::string* errorMessage);
    void destroy() noexcept;
    void collect() noexcept;
    void beginFrame(VkCommandBuffer commandBuffer) noexcept;
    void write(VkCommandBuffer commandBuffer, VulkanGpuFrameTimestamp timestamp, VkPipelineStageFlagBits stage) noexcept;
    void endFrame(VkCommandBuffer commandBuffer) noexcept;
    void markSubmitted() noexcept;

    [[nodiscard]] const VulkanGpuFrameTimes& lastTimes() const noexcept;

private:
    static constexpr auto kTimestampCount = static_cast<std::uint32_t>(VulkanGpuFrameTimestamp::Count);

    [[nodiscard]] std::uint64_t deltaUs(std::uint64_t begin, std::uint64_t end) const noexcept;

    VulkanResourceContext context_;
    VkQueryPool queryPool_ {VK_NULL_HANDLE};
    float timestampPeriodNs_ {0.0F};
    bool supported_ {false};
    bool pendingResults_ {false};
    bool recordingTimestamps_ {false};
    VulkanGpuFrameTimes lastTimes_;
};

} // namespace projectunity::renderer
