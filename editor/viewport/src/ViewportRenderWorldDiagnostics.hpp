#pragma once

#include "ViewportRenderWorld.hpp"

#include <cstdint>
#include <vector>

namespace projectunity::editor {

struct ViewportRenderWorldChunkLogRow {
    std::uint64_t chunkId {0};
    std::uint64_t triangleCount {0};
    std::uint64_t instanceCount {0};
    float maxExtent {0.0F};
    bool visible {false};
};

[[nodiscard]] float renderWorldChunkMaxExtent(const std::array<math::Vec3, 8>& corners) noexcept;

void logRenderWorldChunkDiagnostics(
    const ViewportRenderWorldStats& stats,
    const std::vector<ViewportRenderWorldChunkLogRow>& rows,
    std::uint64_t& lastDebugSignature);

} // namespace projectunity::editor
