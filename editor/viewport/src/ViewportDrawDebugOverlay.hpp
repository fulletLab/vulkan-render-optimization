#pragma once

#include "ViewportLabelGeometry.hpp"

#include <projectunity/math/Vec3.hpp>
#include <projectunity/renderer/RendererTypes.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace projectunity::editor {

void appendViewportDrawDebugOverlay(
    std::vector<renderer::RenderColorVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    std::span<const renderer::RenderMeshDraw> meshDraws,
    std::span<const renderer::RenderMeshDraw> shadowDraws,
    const renderer::RenderFrame& frame,
    const ViewportLabelCamera& labelCamera,
    float viewportWidthPixels,
    math::Vec3 cameraRight,
    math::Vec3 cameraUp,
    math::Vec3 sunDirection,
    bool lodDebugEnabled,
    bool shadowDebugEnabled);

} // namespace projectunity::editor
