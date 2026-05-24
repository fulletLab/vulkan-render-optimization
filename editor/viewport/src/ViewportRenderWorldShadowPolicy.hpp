#pragma once

#include "ViewportRenderWorld.hpp"

#include <projectunity/renderer/RendererTypes.hpp>
#include <projectunity/scene/Scene.hpp>

#include <vector>

namespace projectunity::editor {

void applyViewportShadowPolicy(
    std::vector<renderer::RenderMeshDraw>& meshDraws,
    scene::EntityId selectedEntityId,
    const ViewportRenderWorldCamera& camera,
    int viewportHeight);

} // namespace projectunity::editor
