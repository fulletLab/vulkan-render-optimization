#pragma once

#include "VulkanResourceContext.hpp"

#include <projectunity/renderer/RendererTypes.hpp>

#include <array>
#include <string>

namespace projectunity::renderer {

struct VulkanFrameLight {
    std::array<float, 4> positionType {};
    std::array<float, 4> directionRange {};
    std::array<float, 4> colorIntensity {};
    std::array<float, 4> spotAngles {};
};

struct VulkanFrameUniforms {
    std::array<float, 16> viewProjection {};
    std::array<float, 16> shadowViewProjection {};
    std::array<float, 4> cameraPositionLightCount {};
    std::array<float, 4> ambientSky {};
    std::array<float, 4> ambientGround {};
    std::array<float, 4> shadowSettings {};
    std::array<VulkanFrameLight, kMaxFrameLights> lights {};
};

class VulkanFrameData final {
public:
    VulkanFrameData() = default;
    ~VulkanFrameData();

    VulkanFrameData(const VulkanFrameData&) = delete;
    VulkanFrameData& operator=(const VulkanFrameData&) = delete;

    [[nodiscard]] bool create(VulkanResourceContext context, std::string* errorMessage);
    [[nodiscard]] bool update(const RenderFrame& frame, std::string* errorMessage);
    void destroy() noexcept;

    [[nodiscard]] VkBuffer buffer() const noexcept;

private:
    VulkanResourceContext context_;
    VkBuffer buffer_ {VK_NULL_HANDLE};
    VmaAllocation allocation_ {VK_NULL_HANDLE};
    void* mapped_ {nullptr};
};

} // namespace projectunity::renderer
