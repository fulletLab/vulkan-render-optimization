#pragma once

#include "ViewportLabelGeometry.hpp"

#include <projectunity/assets/AssetManager.hpp>
#include <projectunity/renderer/RendererTypes.hpp>
#include <projectunity/scene/Scene.hpp>

#include <cstdint>
#include <vector>

namespace projectunity::editor {

void appendViewportSourceObjectDebugOverlay(
    std::vector<renderer::RenderColorVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    const scene::Scene& scene,
    const assets::IAssetManager& assetManager,
    const ViewportLabelCamera& labelCamera,
    float viewportWidthPixels,
    float aspectRatio,
    float nearPlane,
    float farPlane);

} // namespace projectunity::editor
