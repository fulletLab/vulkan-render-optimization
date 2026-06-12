#pragma once

#include "ViewportRenderWorld.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace projectunity::editor {

struct ViewportRenderWorldChunkLogRow {
    std::string modelName;
    std::uint64_t chunkId {0};
    std::uint64_t triangleCount {0};
    std::uint64_t instanceCount {0};
    float maxExtent {0.0F};
    bool visible {false};
    bool selected {false};
    bool occluder {false};
    bool occlusionRejected {false};
};

template<typename Record, typename Chunk>
[[nodiscard]] std::string viewportRenderWorldChunkModelName(const Record& record, const Chunk& chunk)
{
    for (const auto instanceIndex : chunk.instanceIndices) {
        if (instanceIndex < record.instances.size() && record.instances[instanceIndex].model != nullptr) {
            return record.instances[instanceIndex].model->name;
        }
    }
    return {};
}

[[nodiscard]] float renderWorldChunkMaxExtent(const std::array<math::Vec3, 8>& corners) noexcept;

void logRenderWorldChunkDiagnostics(
    const ViewportRenderWorldStats& stats,
    const std::vector<ViewportRenderWorldChunkLogRow>& rows,
    std::uint64_t& lastDebugSignature);

} // namespace projectunity::editor
