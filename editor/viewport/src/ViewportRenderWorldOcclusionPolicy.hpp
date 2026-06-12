#pragma once

#include "ViewportRenderWorld.hpp"
#include "ViewportRenderWorldDiagnostics.hpp"
#include "ViewportRenderWorldOcclusion.hpp"
#include "ViewportRendererCulling.hpp"

#include <projectunity/assets/AssetManager.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <unordered_set>

namespace projectunity::editor {

namespace detail {

constexpr float kMinimumMultiInstanceOccluderFootprintCoverage = 0.35F;

[[nodiscard]] inline float viewportAxisComponent(math::Vec3 value, int axis) noexcept
{
    if (axis == 0) {
        return value.x;
    }
    if (axis == 1) {
        return value.y;
    }
    return value.z;
}

[[nodiscard]] inline math::Vec3 viewportBoundsExtents(const ViewportWorldBounds& bounds) noexcept
{
    auto minimum = bounds.corners.front();
    auto maximum = bounds.corners.front();
    for (const auto corner : bounds.corners) {
        minimum.x = std::min(minimum.x, corner.x);
        minimum.y = std::min(minimum.y, corner.y);
        minimum.z = std::min(minimum.z, corner.z);
        maximum.x = std::max(maximum.x, corner.x);
        maximum.y = std::max(maximum.y, corner.y);
        maximum.z = std::max(maximum.z, corner.z);
    }
    return maximum - minimum;
}

[[nodiscard]] inline std::array<int, 2> viewportFootprintAxes(math::Vec3 extents) noexcept
{
    std::array<std::pair<float, int>, 3> axes {{
        {std::fabs(extents.x), 0},
        {std::fabs(extents.y), 1},
        {std::fabs(extents.z), 2},
    }};
    std::sort(axes.begin(), axes.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first > rhs.first;
    });
    return {axes[0].second, axes[1].second};
}

} // namespace detail

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
            || instance.model->materials[primitive.materialIndex].alphaMode != assets::MaterialAlphaMode::Opaque) {
            return false;
        }
    }
    return !chunk.instanceIndices.empty();
}

template<typename Record, typename Chunk>
[[nodiscard]] bool viewportChunkHasSolidOccluderFootprint(const Record& record, const Chunk& chunk) noexcept
{
    if (chunk.instanceIndices.size() <= 1U) {
        return true;
    }

    const auto chunkExtents = detail::viewportBoundsExtents(chunk.worldBounds);
    const auto axes = detail::viewportFootprintAxes(chunkExtents);
    const auto chunkArea =
        std::fabs(detail::viewportAxisComponent(chunkExtents, axes[0]))
        * std::fabs(detail::viewportAxisComponent(chunkExtents, axes[1]));
    if (!std::isfinite(chunkArea) || chunkArea <= 0.0001F) {
        return false;
    }

    auto instanceAreaSum = 0.0F;
    for (const auto instanceIndex : chunk.instanceIndices) {
        if (instanceIndex >= record.instances.size()) {
            continue;
        }
        const auto instanceExtents = detail::viewportBoundsExtents(record.instances[instanceIndex].worldBounds);
        const auto instanceArea =
            std::fabs(detail::viewportAxisComponent(instanceExtents, axes[0]))
            * std::fabs(detail::viewportAxisComponent(instanceExtents, axes[1]));
        if (std::isfinite(instanceArea) && instanceArea > 0.0F) {
            instanceAreaSum += std::min(instanceArea, chunkArea);
        }
    }

    const auto coverage = instanceAreaSum / chunkArea;
    return std::isfinite(coverage)
        && coverage >= detail::kMinimumMultiInstanceOccluderFootprintCoverage;
}

template<typename Record, typename Chunk>
[[nodiscard]] bool viewportChunkCanOcclude(const Record& record, const Chunk& chunk, float sceneExtent) noexcept
{
    constexpr std::uint64_t kMinimumOccluderTriangles = 12'000ULL;
    constexpr float kMinimumOccluderExtent = 2.0F;
    if (chunk.triangleCount < kMinimumOccluderTriangles
        || !viewportChunkHasOnlyOpaqueMaterials(record, chunk)
        || !viewportChunkHasSolidOccluderFootprint(record, chunk)) {
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
    if (selectedChunk || chunkIsOccluder || !occlusionBuffer.hasOccluders()
        || !viewportChunkHasOnlyOpaqueMaterials(record, chunk)) {
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
