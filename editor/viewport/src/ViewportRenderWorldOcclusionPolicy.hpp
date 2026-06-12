#pragma once

#include "ViewportRenderWorld.hpp"
#include "ViewportRenderWorldDiagnostics.hpp"
#include "ViewportRenderWorldOcclusion.hpp"
#include "ViewportRendererCulling.hpp"

#include <projectunity/assets/AssetManager.hpp>

#include <algorithm>
#include <cstdint>
#include <unordered_set>

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
            if (occlusionBuffer.addOccluder(chunk.worldBounds)) {
                occluderChunkIds.insert(chunk.renderChunkId);
                ++stats.occlusionOccluderChunkCount;
            }
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
    if (selectedChunk || chunkIsOccluder || !occlusionBuffer.hasOccluders()) {
        return false;
    }

    ++stats.occlusionTestedChunkCount;
    if (!occlusionBuffer.isOccluded(chunk.worldBounds)) {
        return false;
    }
    ++stats.occlusionRejectedChunkCount;
    stats.occlusionRejectedInstanceCount += static_cast<std::uint64_t>(chunk.instanceIndices.size());
    stats.occlusionRejectedTriangleCount += chunk.triangleCount;
    return true;
}

} // namespace projectunity::editor
