#pragma once

#include <projectunity/renderer/RendererTypes.hpp>

#include <span>
#include <vector>

namespace projectunity::renderer {

[[nodiscard]] bool isTransparentMeshDraw(const RenderMeshDraw& draw) noexcept;

// Opaque/masked draws keep their submission order; blended primitive draws are
// deferred and sorted back-to-front by camera-space depth.
void orderMeshDraws(std::span<const RenderMeshDraw> draws, std::vector<const RenderMeshDraw*>& ordered);

} // namespace projectunity::renderer
