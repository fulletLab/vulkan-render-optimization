#pragma once

#include "ViewportRenderWorld.hpp"
#include "ViewportRenderWorldRecord.hpp"
#include "ViewportRendererCulling.hpp"

#include <cstddef>

namespace projectunity::editor {

template<typename Record, typename Callback>
void forEachSpatialChunkCandidate(
    const Record& record,
    const ViewportRenderWorldCamera& camera,
    ViewportRenderWorldStats& stats,
    Callback&& callback)
{
    if (record.chunkCells.empty()) {
        for (std::size_t index = 0; index < record.chunks.size(); ++index) {
            callback(index, record.chunks[index]);
        }
        return;
    }

    for (const auto& cell : record.chunkCells) {
        ++stats.spatialCellTestCount;
        if (!viewportBoundsVisible(
                cell.worldBounds,
                camera.eye,
                camera.right,
                camera.up,
                camera.forward,
                camera.verticalFovRadians,
                camera.aspectRatio,
                camera.nearPlane,
                camera.farPlane)) {
            ++stats.spatialCellRejectedCount;
            continue;
        }
        stats.spatialCellCandidateChunkCount += static_cast<std::uint64_t>(cell.chunkIndices.size());
        for (const auto chunkIndex : cell.chunkIndices) {
            if (chunkIndex >= record.chunks.size()) {
                continue;
            }
            callback(chunkIndex, record.chunks[chunkIndex]);
        }
    }
}

} // namespace projectunity::editor
