#include "VulkanFrameData.hpp"

#include <algorithm>
#include <cstring>

namespace projectunity::renderer {
namespace {

static_assert(sizeof(VulkanFrameLight) == sizeof(float) * 16U);

[[nodiscard]] float lightTypeValue(RenderLightType type)
{
    switch (type) {
    case RenderLightType::Directional:
        return 0.0F;
    case RenderLightType::Point:
        return 1.0F;
    case RenderLightType::Spot:
        return 2.0F;
    }
    return 0.0F;
}

[[nodiscard]] VulkanFrameUniforms makeUniforms(const RenderFrame& frame)
{
    VulkanFrameUniforms uniforms;
    uniforms.viewProjection = frame.viewProjection.values;
    uniforms.shadowViewProjection = frame.shadowViewProjection.values;
    uniforms.cameraPositionLightCount = {
        frame.cameraPosition[0],
        frame.cameraPosition[1],
        frame.cameraPosition[2],
        static_cast<float>(std::min(frame.lights.size(), kMaxFrameLights)),
    };
    uniforms.ambientSky = {frame.ambientSkyColor[0], frame.ambientSkyColor[1], frame.ambientSkyColor[2], 1.0F};
    uniforms.ambientGround = {
        frame.ambientGroundColor[0],
        frame.ambientGroundColor[1],
        frame.ambientGroundColor[2],
        1.0F,
    };
    uniforms.shadowSettings = {
        frame.shadowsEnabled ? 1.0F : 0.0F,
        static_cast<float>(std::min<std::size_t>(frame.shadowLightIndex, kMaxFrameLights - 1U)),
        0.0018F,
        0.0F,
    };
    for (std::size_t index = 0; index < std::min(frame.lights.size(), kMaxFrameLights); ++index) {
        const auto& source = frame.lights[index];
        auto& light = uniforms.lights[index];
        light.positionType = {source.position[0], source.position[1], source.position[2], lightTypeValue(source.type)};
        light.directionRange = {source.direction[0], source.direction[1], source.direction[2], source.range};
        light.colorIntensity = {source.color[0], source.color[1], source.color[2], source.intensity};
        light.spotAngles = {source.innerConeAngle, source.outerConeAngle, 0.0F, 0.0F};
    }
    return uniforms;
}

} // namespace

VulkanFrameData::~VulkanFrameData()
{
    destroy();
}

bool VulkanFrameData::create(VulkanResourceContext context, std::string* errorMessage)
{
    destroy();
    context_ = context;

    VkBufferCreateInfo bufferInfo {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = sizeof(VulkanFrameUniforms);
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo allocationInfo {};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
        | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo resultInfo {};
    if (vmaCreateBuffer(
            context.allocator,
            &bufferInfo,
            &allocationInfo,
            &buffer_,
            &allocation_,
            &resultInfo)
        != VK_SUCCESS
        || resultInfo.pMappedData == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to create mapped Vulkan frame uniform buffer";
        }
        destroy();
        return false;
    }
    mapped_ = resultInfo.pMappedData;
    return true;
}

bool VulkanFrameData::update(const RenderFrame& frame, std::string* errorMessage)
{
    if (buffer_ == VK_NULL_HANDLE || allocation_ == VK_NULL_HANDLE || mapped_ == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Vulkan frame uniform buffer is not ready";
        }
        return false;
    }

    const auto uniforms = makeUniforms(frame);
    std::memcpy(mapped_, &uniforms, sizeof(uniforms));
    vmaFlushAllocation(context_.allocator, allocation_, 0, sizeof(uniforms));
    return true;
}

void VulkanFrameData::destroy() noexcept
{
    if (buffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(context_.allocator, buffer_, allocation_);
        buffer_ = VK_NULL_HANDLE;
        allocation_ = VK_NULL_HANDLE;
        mapped_ = nullptr;
    }
}

VkBuffer VulkanFrameData::buffer() const noexcept
{
    return buffer_;
}

} // namespace projectunity::renderer
