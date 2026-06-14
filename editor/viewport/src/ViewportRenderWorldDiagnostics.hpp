#pragma once

#include "ViewportRenderWorld.hpp"

#include <cstdint>
#include <iosfwd>
#include <limits>
#include <vector>

namespace projectunity::editor {

struct ViewportRenderWorldChunkLogRow {
    std::uint64_t sceneNodeId {0};
    std::uint64_t modelAssetId {0};
    std::uint64_t chunkId {0};
    std::uint64_t nearestRenderInstanceId {0};
    std::uint64_t triangleCount {0};
    std::uint64_t instanceCount {0};
    std::uint64_t lod0DrawCount {0};
    std::uint64_t lod1DrawCount {0};
    std::uint64_t lod2PlusDrawCount {0};
    math::Vec3 boundsMinimum;
    math::Vec3 boundsMaximum;
    float maxExtent {0.0F};
    float distanceToCamera {0.0F};
    float projectedRadiusPixels {0.0F};
    float nearestInstanceDistance {std::numeric_limits<float>::max()};
    std::uint32_t nearestSelectedLod {0};
    bool visible {false};
    bool cameraInsideRootBounds {false};
    bool cameraInsideChunkBounds {false};
    const char* reason {"candidate"};
};

[[nodiscard]] float renderWorldChunkMaxExtent(const std::array<math::Vec3, 8>& corners) noexcept;

void logRenderWorldChunkDiagnostics(
    const ViewportRenderWorldStats& stats,
    const std::vector<ViewportRenderWorldChunkLogRow>& rows,
    std::uint64_t& lastDebugSignature);

void appendViewportVisibleDrawDiagnostics(
    std::ostringstream& message,
    const std::vector<renderer::RenderMeshDraw>& draws);

} // namespace projectunity::editor
