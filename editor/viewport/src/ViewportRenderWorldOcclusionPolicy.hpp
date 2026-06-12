#pragma once

#include "ViewportRenderWorld.hpp"
#include "ViewportRenderWorldDiagnostics.hpp"
#include "ViewportRenderWorldOcclusion.hpp"
#include "ViewportRendererCulling.hpp"

#include <projectunity/assets/AssetManager.hpp>

#include <algorithm>
#include <cstdint>
#include <type_traits>
#include <unordered_set>
#include <vector>

namespace projectunity::editor {

template<typename Record, typename Chunk>
[[nodiscard]] bool viewportChunkContainsSelected(
    const Record& record,
    const Chunk& chunk,
    scene::EntityId selectedEntityId,
    assets::AssetId selectedPrimitiveModel,
    std::uint32_t selectedPrimitiveIndex) noexcept
{
    if (chunk.sceneNodeId == selectedEntityId) {
        return true;
    }
    for (const auto instanceIndex : chunk.instanceIndices) {
        if (instanceIndex >= record.instances.size()) {
            continue;
        }
        const auto& instance = record.instances[instanceIndex];
        if (instance.sceneNodeId == selectedEntityId) {
            return true;
        }
        if (selectedPrimitiveModel.isValid()
            && instance.modelAssetId == selectedPrimitiveModel
            && instance.primitiveInstanceIndex == selectedPrimitiveIndex) {
            return true;
        }
    }
    return false;
}

inline void recordViewportOcclusionQueryResult(
    ViewportRenderWorldStats& stats,
    ViewportOcclusionQueryResult result) noexcept
{
    switch (result) {
    case ViewportOcclusionQueryResult::ProjectionRejected:
        ++stats.occlusionProjectionRejectedChunkCount;
        break;
    case ViewportOcclusionQueryResult::CandidateTooLarge:
        ++stats.occlusionTooLargeChunkCount;
        break;
    case ViewportOcclusionQueryResult::InvalidDepth:
        ++stats.occlusionInvalidDepthChunkCount;
        break;
    case ViewportOcclusionQueryResult::UncoveredCell:
        ++stats.occlusionUncoveredChunkCount;
        break;
    case ViewportOcclusionQueryResult::DepthVisible:
        ++stats.occlusionDepthVisibleChunkCount;
        break;
    default:
        break;
    }
}

template<typename Record, typename Chunk>
[[nodiscard]] bool viewportChunkHasOnlyOpaqueMaterials(const Record& record, const Chunk& chunk) noexcept
{
    for (const auto instanceIndex : chunk.instanceIndices) {
        if (instanceIndex >= record.instances.size()) {
            continue;
        }
        const auto& instance = record.instances[instanceIndex];
        if (instance.primitiveIndex >= instance.model->primitives.size()) {
            return false;
        }
        const auto& primitive = instance.model->primitives[instance.primitiveIndex];
        if (primitive.materialIndex >= instance.model->materials.size()
            || instance.model->materials[primitive.materialIndex].alphaMode == assets::MaterialAlphaMode::Blend) {
            return false;
        }
    }
    return !chunk.instanceIndices.empty();
}

template<typename Record, typename Chunk>
[[nodiscard]] bool viewportChunkCanOcclude(const Record& record, const Chunk& chunk, float sceneExtent) noexcept
{
    constexpr std::uint64_t kMinimumOccluderTriangles = 12'000ULL;
    constexpr float kMinimumOccluderExtent = 2.0F;
    if (chunk.triangleCount < kMinimumOccluderTriangles || !viewportChunkHasOnlyOpaqueMaterials(record, chunk)) {
        return false;
    }
    const auto chunkExtent = renderWorldChunkMaxExtent(chunk.worldBounds.corners);
    const auto minimumExtent = sceneExtent > 0.0001F
        ? std::max(kMinimumOccluderExtent, sceneExtent * 0.015F)
        : kMinimumOccluderExtent;
    return chunkExtent >= minimumExtent;
}

template<typename RecordRange>
void buildViewportOcclusionBuffer(
    const RecordRange& records,
    const ViewportRenderWorldCamera& camera,
    float sceneExtent,
    ViewportOcclusionBuffer& occlusionBuffer,
    std::unordered_set<std::uint64_t>& occluderChunkIds,
    ViewportRenderWorldStats& stats)
{
    using RecordPointer = typename RecordRange::value_type;
    using RecordType = std::remove_const_t<std::remove_pointer_t<RecordPointer>>;

    struct Candidate {
        const RecordType* record {nullptr};
        const typename RecordType::Chunk* chunk {nullptr};
        float depth {0.0F};
    };

    stats.occlusionBackend = static_cast<std::uint64_t>(occlusionBuffer.backend());
    if (occlusionBuffer.backend() == ViewportOcclusionBackend::Off) {
        return;
    }

    std::vector<Candidate> candidates;
    for (const auto* record : records) {
        if (record == nullptr) {
            continue;
        }
        for (const auto& chunk : record->chunks) {
            if (!viewportChunkCanOcclude(*record, chunk, sceneExtent)) {
                continue;
            }
            if (!viewportBoundsVisible(
                    chunk.worldBounds,
                    camera.eye,
                    camera.right,
                    camera.up,
                    camera.forward,
                    camera.verticalFovRadians,
                    camera.aspectRatio,
                    camera.nearPlane,
                    camera.farPlane)) {
                continue;
            }
            candidates.push_back({record, &chunk, math::dot(chunk.worldBounds.center - camera.eye, camera.forward)});
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& lhs, const Candidate& rhs) {
        return lhs.depth < rhs.depth;
    });

    for (const auto& candidate : candidates) {
        if (candidate.record == nullptr || candidate.chunk == nullptr) {
            continue;
        }

        const auto& record = *candidate.record;
        const auto& chunk = *candidate.chunk;
        const auto beforeTriangles = occlusionBuffer.occluderTriangleCount();
        auto added = false;
        if (occlusionBuffer.backend() == ViewportOcclusionBackend::MaskedOcclusionCulling) {
            for (const auto instanceIndex : chunk.instanceIndices) {
                if (instanceIndex >= record.instances.size()) {
                    continue;
                }
                const auto& instance = record.instances[instanceIndex];
                if (instance.primitiveIndex >= instance.model->primitives.size()) {
                    continue;
                }
                const auto& primitive = instance.model->primitives[instance.primitiveIndex];
                if (primitive.materialIndex >= instance.model->materials.size()) {
                    continue;
                }
                const auto& material = instance.model->materials[primitive.materialIndex];
                added = occlusionBuffer.addOccluderTriangles(
                    primitive,
                    instance.modelMatrix,
                    material.doubleSided || instance.flipsWinding) || added;
            }
        } else {
            added = occlusionBuffer.addOccluder(chunk.worldBounds);
        }

        if (added) {
            occluderChunkIds.insert(chunk.renderChunkId);
            ++stats.occlusionOccluderChunkCount;
            stats.occlusionOccluderTriangleCount += occlusionBuffer.occluderTriangleCount() - beforeTriangles;
        }
    }
}

template<typename Record, typename Chunk>
[[nodiscard]] bool viewportChunkRejectedByOcclusion(
    const Record& record,
    const Chunk& chunk,
    scene::EntityId selectedEntityId,
    assets::AssetId selectedPrimitiveModel,
    std::uint32_t selectedPrimitiveIndex,
    const ViewportOcclusionBuffer& occlusionBuffer,
    const std::unordered_set<std::uint64_t>& occluderChunkIds,
    ViewportRenderWorldStats& stats)
{
    const auto selectedChunk = viewportChunkContainsSelected(
        record,
        chunk,
        selectedEntityId,
        selectedPrimitiveModel,
        selectedPrimitiveIndex);
    const auto chunkIsOccluder = occluderChunkIds.find(chunk.renderChunkId) != occluderChunkIds.end();
    if (selectedChunk) {
        ++stats.occlusionSelectedSkippedChunkCount;
        return false;
    }
    if (chunkIsOccluder) {
        ++stats.occlusionOccluderSkippedChunkCount;
        return false;
    }
    if (!occlusionBuffer.hasOccluders()) {
        return false;
    }

    ++stats.occlusionTestedChunkCount;
    if (!occlusionBuffer.isOccluded(chunk.worldBounds)) {
        recordViewportOcclusionQueryResult(stats, occlusionBuffer.lastQueryResult());
        return false;
    }
    ++stats.occlusionRejectedChunkCount;
    stats.occlusionRejectedInstanceCount += static_cast<std::uint64_t>(chunk.instanceIndices.size());
    stats.occlusionRejectedTriangleCount += chunk.triangleCount;
    return true;
}

} // namespace projectunity::editor
