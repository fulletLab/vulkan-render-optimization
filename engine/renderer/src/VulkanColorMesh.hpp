#pragma once

#include "VulkanGpuBuffer.hpp"

#include <projectunity/renderer/RendererTypes.hpp>

#include <cstdint>
#include <string>

namespace projectunity::renderer {

struct VulkanColorMeshBuffers {
    VulkanGpuBuffer vertices;
    VulkanGpuBuffer indices;
    std::uint32_t indexCount {0};

    [[nodiscard]] bool upload(
        VulkanResourceContext context,
        VulkanUploadContext& uploads,
        const RenderColorMeshDraw& draw,
        std::string* errorMessage);
};

} // namespace projectunity::renderer
