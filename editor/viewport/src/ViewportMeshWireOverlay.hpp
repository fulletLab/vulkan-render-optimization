#pragma once

#include <projectunity/math/Vec3.hpp>
#include <projectunity/renderer/RendererTypes.hpp>
#include <projectunity/scene/Scene.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace projectunity::editor {

void appendViewportMeshWireOverlay(
    std::vector<renderer::RenderColorVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    std::span<const renderer::RenderMeshDraw> meshDraws,
    scene::EntityId selectedEntityId,
    bool includeVisibleMeshes,
    math::Vec3 cameraForward,
    math::Vec3 cameraRight,
    float cameraDistance);

} // namespace projectunity::editor
