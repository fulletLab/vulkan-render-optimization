#pragma once

#include "ViewportRenderWorld.hpp"
#include "ViewportRenderWorldRecord.hpp"

#include <cstdint>
#include <vector>

namespace projectunity::editor {

void appendSelectedPrimitiveOverrideDraw(
    const scene::Scene& scene,
    const assets::IAssetManager& assetManager,
    scene::EntityId selectedEntityId,
    const ViewportRenderWorldCamera& camera,
    const renderer::RenderMatrix4& viewProjection,
    std::vector<renderer::RenderMeshDraw>& meshDraws,
    ViewportRenderWorldStats& stats,
    ViewportFrameBounds& visibleBounds,
    std::uint64_t& visibleSourceTriangleCount);

} // namespace projectunity::editor
