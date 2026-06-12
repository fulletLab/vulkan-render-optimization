#pragma once

#include "ViewportRenderWorld.hpp"
#include "ViewportRenderWorldRecord.hpp"
#include "ViewportRenderWorldDiagnostics.hpp"

#include <algorithm>
#include <cstdint>

namespace projectunity::editor {

template<typename RecordRange>
void accumulateViewportRenderWorldRecordStats(
    const RecordRange& records,
    ViewportRenderWorldFrame& result,
    ViewportFrameBounds& allChunkBounds)
{
    for (const auto* record : records) {
        if (record == nullptr) {
            continue;
        }
        result.hasMeshSceneContent = result.hasMeshSceneContent || record->hasMeshSceneContent;
        result.stats.renderInstanceCount += record->instances.size();
        result.stats.renderChunkCount += record->chunks.size();
        result.stats.hlodCandidateDrawCount += record->overviewDraws.size();
        for (const auto& chunk : record->chunks) {
            result.stats.hlodCandidateDrawCount += chunk.overviewDraws.size();
        }
        for (const auto& instance : record->instances) {
            if (instance.primitiveIndex >= instance.model->primitives.size()) {
                continue;
            }
            result.stats.candidateMeshDrawCount += 1U;
            result.stats.candidateTriangleCount += instance.model->primitives[instance.primitiveIndex].indices.size() / 3U;
        }
        for (const auto& chunk : record->chunks) {
            allChunkBounds.includeSphere(chunk.worldBounds.center, chunk.worldBounds.radius);
            const auto extent = renderWorldChunkMaxExtent(chunk.worldBounds.corners);
            result.stats.maxRenderChunkExtent = std::max(result.stats.maxRenderChunkExtent, extent);
            result.stats.largestRenderChunkTriangleCount = std::max(
                result.stats.largestRenderChunkTriangleCount,
                chunk.triangleCount);
            result.stats.largestRenderChunkInstanceCount = std::max<std::uint64_t>(
                result.stats.largestRenderChunkInstanceCount,
                chunk.instanceIndices.size());
        }
    }
}

} // namespace projectunity::editor
