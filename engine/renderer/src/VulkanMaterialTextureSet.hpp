#pragma once

#include "VulkanTextureCache.hpp"
#include "VulkanUploadContext.hpp"

#include <projectunity/renderer/RendererTypes.hpp>

#include <string>

namespace projectunity::renderer {

struct VulkanMaterialTextureSet {
    const VulkanTextureHandle* baseColor {nullptr};
    const VulkanTextureHandle* normal {nullptr};
    const VulkanTextureHandle* metallicRoughness {nullptr};
    const VulkanTextureHandle* occlusion {nullptr};
    const VulkanTextureHandle* emissive {nullptr};
    const VulkanTextureHandle* brdfLut {nullptr};
    const VulkanTextureHandle* irradianceCube {nullptr};
    const VulkanTextureHandle* prefilteredEnvironment {nullptr};

    [[nodiscard]] bool complete() const noexcept;
};

[[nodiscard]] VulkanMaterialTextureSet uploadMaterialTextureSet(
    VulkanResourceContext context,
    VulkanUploadContext& uploads,
    VulkanTextureCache& textureCache,
    const RenderMeshDraw& draw,
    const RenderEnvironmentSettings& environment,
    std::string* errorMessage);

} // namespace projectunity::renderer
