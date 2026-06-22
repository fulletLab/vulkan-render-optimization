#include "ViewportRenderWorld.hpp"
#include "ViewportMeshLod.hpp"
#include "ViewportRenderWorldHlodPolicy.hpp"
#include "ViewportRenderWorldOcclusion.hpp"
#include "ViewportRenderWorldOcclusionPolicy.hpp"
#include "ViewportRenderWorldRecord.hpp"
#include "ViewportRenderWorldSpatial.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace projectunity::editor {
namespace {

constexpr bool kOverviewHlodEnabled = true;
constexpr std::uint64_t kOverviewMinSourceTriangles = 32'000ULL;
constexpr std::uint64_t kOverviewMaxSourceTriangles = 1'000'000'000ULL;
constexpr std::uint64_t kOverviewMaxTriangles = 4'000'000ULL;
constexpr std::uint64_t kOverviewMinVisibleInstanceReferences = 128ULL;
constexpr std::uint64_t kOverviewMinVisibleTriangles = 32'000ULL;
constexpr std::size_t kMinChunksForSpatialCells = 64;
constexpr std::size_t kTargetChunksPerSpatialCell = 16;
constexpr std::size_t kMaxChunkSpatialGridSide = 32;

struct CachedOverviewDraw {
    assets::AssetId modelAssetId;
    std::uint32_t primitiveIndex {0};
    std::shared_ptr<const assets::MeshPrimitive> primitive;
    std::uint64_t sourceTriangleCount {0};
};

struct MaterialOverview {
    std::size_t materialIndex {0};
    assets::MeshPrimitive primitive;
    std::uint64_t sourceTriangleCount {0};
};

struct CachedOverviewModel {
    std::vector<CachedOverviewDraw> fullDraws;
    std::vector<std::vector<CachedOverviewDraw>> clusterDraws;
};

[[nodiscard]] std::uint64_t mixHash(std::uint64_t seed, std::uint64_t value) noexcept
{
    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U));
}

[[nodiscard]] float matrixAt(const renderer::RenderMatrix4& matrix, int row, int column)
{
    return matrix.values[static_cast<std::size_t>(column * 4 + row)];
}

[[nodiscard]] float& matrixAt(renderer::RenderMatrix4& matrix, int row, int column)
{
    return matrix.values[static_cast<std::size_t>(column * 4 + row)];
}

[[nodiscard]] renderer::RenderMatrix4 multiply(const renderer::RenderMatrix4& lhs, const renderer::RenderMatrix4& rhs)
{
    renderer::RenderMatrix4 result;
    result.values.fill(0.0F);
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            for (int index = 0; index < 4; ++index) {
                matrixAt(result, row, column) += matrixAt(lhs, row, index) * matrixAt(rhs, index, column);
            }
        }
    }
    return result;
}

[[nodiscard]] renderer::RenderMatrix4 identityMatrix()
{
    return {};
}

[[nodiscard]] renderer::RenderMatrix4 renderMatrix(const std::array<float, 16>& values)
{
    renderer::RenderMatrix4 matrix;
    matrix.values = values;
    return matrix;
}

[[nodiscard]] math::Vec3 transformMatrixPoint(const renderer::RenderMatrix4& matrix, math::Vec3 point) noexcept
{
    return {
        matrixAt(matrix, 0, 0) * point.x + matrixAt(matrix, 0, 1) * point.y + matrixAt(matrix, 0, 2) * point.z + matrixAt(matrix, 0, 3),
        matrixAt(matrix, 1, 0) * point.x + matrixAt(matrix, 1, 1) * point.y + matrixAt(matrix, 1, 2) * point.z + matrixAt(matrix, 1, 3),
        matrixAt(matrix, 2, 0) * point.x + matrixAt(matrix, 2, 1) * point.y + matrixAt(matrix, 2, 2) * point.z + matrixAt(matrix, 2, 3),
    };
}

[[nodiscard]] math::Vec3 transformMatrixVector(const renderer::RenderMatrix4& matrix, math::Vec3 vector) noexcept
{
    return {
        matrixAt(matrix, 0, 0) * vector.x + matrixAt(matrix, 0, 1) * vector.y + matrixAt(matrix, 0, 2) * vector.z,
        matrixAt(matrix, 1, 0) * vector.x + matrixAt(matrix, 1, 1) * vector.y + matrixAt(matrix, 1, 2) * vector.z,
        matrixAt(matrix, 2, 0) * vector.x + matrixAt(matrix, 2, 1) * vector.y + matrixAt(matrix, 2, 2) * vector.z,
    };
}

[[nodiscard]] math::Vec3 safeNormalized(math::Vec3 value, math::Vec3 fallback)
{
    const auto length = value.length();
    if (length <= 0.00001F || !std::isfinite(length)) {
        return fallback;
    }
    return value / length;
}

[[nodiscard]] float worldAxisComponent(math::Vec3 value, int axis) noexcept
{
    if (axis == 0) {
        return value.x;
    }
    if (axis == 1) {
        return value.y;
    }
    return value.z;
}

void includeWorldPoint(ViewportFrameBounds& bounds, math::Vec3 point)
{
    bounds.includeSphere(point, 0.0F);
}

[[nodiscard]] ViewportFrameBounds boundsFrame(const ViewportWorldBounds& bounds)
{
    ViewportFrameBounds result;
    for (const auto corner : bounds.corners) {
        includeWorldPoint(result, corner);
    }
    return result;
}

[[nodiscard]] ViewportWorldBounds worldBoundsFromFrame(const ViewportFrameBounds& bounds)
{
    assets::MeshBounds meshBounds;
    meshBounds.minimum = bounds.minimum;
    meshBounds.maximum = bounds.maximum;
    meshBounds.center = bounds.center();
    meshBounds.radius = bounds.radius();
    return transformViewportBounds(identityMatrix(), meshBounds);
}

[[nodiscard]] std::array<int, 2> spatialCellAxes(const ViewportWorldBounds& bounds) noexcept
{
    const auto frame = boundsFrame(bounds);
    const auto extent = frame.maximum - frame.minimum;
    std::array<std::pair<float, int>, 3> axes {{
        {std::fabs(extent.x), 0},
        {std::fabs(extent.y), 1},
        {std::fabs(extent.z), 2},
    }};
    std::sort(axes.begin(), axes.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first > rhs.first;
    });
    return {axes[0].second, axes[1].second};
}

[[nodiscard]] std::size_t spatialCellCoordinate(float value, float minimum, float extent, std::size_t side) noexcept
{
    if (side <= 1U || extent <= 0.0001F || !std::isfinite(value)) {
        return 0;
    }
    const auto normalized = std::clamp((value - minimum) / extent, 0.0F, 0.999999F);
    return std::min(static_cast<std::size_t>(normalized * static_cast<float>(side)), side - 1U);
}

template<typename Record>
void rebuildChunkSpatialCells(Record& record)
{
    record.chunkCells.clear();
    if (record.chunks.size() < kMinChunksForSpatialCells || !record.worldBoundsValid) {
        return;
    }

    const auto frame = boundsFrame(record.worldBounds);
    if (!frame.valid) {
        return;
    }
    const auto axes = spatialCellAxes(record.worldBounds);
    const auto minA = worldAxisComponent(frame.minimum, axes[0]);
    const auto minB = worldAxisComponent(frame.minimum, axes[1]);
    const auto extentA = worldAxisComponent(frame.maximum, axes[0]) - minA;
    const auto extentB = worldAxisComponent(frame.maximum, axes[1]) - minB;
    if (std::fabs(extentA) <= 0.0001F && std::fabs(extentB) <= 0.0001F) {
        return;
    }

    const auto wantedCells = std::max<std::size_t>(
        1U,
        (record.chunks.size() + kTargetChunksPerSpatialCell - 1U) / kTargetChunksPerSpatialCell);
    const auto side = std::clamp(
        static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<float>(wantedCells)))),
        std::size_t {2U},
        kMaxChunkSpatialGridSide);
    const auto cellCount = side * side;
    std::vector<std::vector<std::size_t>> cellChunks(cellCount);
    std::vector<ViewportFrameBounds> cellBounds(cellCount);

    for (std::size_t chunkIndex = 0; chunkIndex < record.chunks.size(); ++chunkIndex) {
        const auto& chunk = record.chunks[chunkIndex];
        const auto cellX = spatialCellCoordinate(
            worldAxisComponent(chunk.worldBounds.center, axes[0]),
            minA,
            extentA,
            side);
        const auto cellY = spatialCellCoordinate(
            worldAxisComponent(chunk.worldBounds.center, axes[1]),
            minB,
            extentB,
            side);
        const auto cellIndex = cellY * side + cellX;
        if (cellIndex >= cellChunks.size()) {
            continue;
        }
        cellChunks[cellIndex].push_back(chunkIndex);
        cellBounds[cellIndex].includeSphere(chunk.worldBounds.center, chunk.worldBounds.radius);
    }

    record.chunkCells.reserve(cellCount);
    for (std::size_t cellIndex = 0; cellIndex < cellChunks.size(); ++cellIndex) {
        if (cellChunks[cellIndex].empty() || !cellBounds[cellIndex].valid) {
            continue;
        }
        typename Record::ChunkCell cell;
        cell.worldBounds = worldBoundsFromFrame(cellBounds[cellIndex]);
        cell.chunkIndices = std::move(cellChunks[cellIndex]);
        record.chunkCells.push_back(std::move(cell));
    }

    if (record.chunkCells.size() <= 1U) {
        record.chunkCells.clear();
    }
}

[[nodiscard]] const assets::TextureAsset* modelTexture(
    const assets::ModelAsset& model,
    std::optional<std::size_t> textureIndex) noexcept
{
    return textureIndex.has_value() && *textureIndex < model.textures.size()
        ? &model.textures[*textureIndex]
        : nullptr;
}

[[nodiscard]] const std::vector<std::uint32_t>* coarsestLodIndices(const assets::MeshPrimitive& primitive) noexcept
{
    const std::vector<std::uint32_t>* best = nullptr;
    auto bestIndexCount = primitive.indices.size();
    for (const auto& lod : primitive.lods) {
        if (!lod.indices.empty() && lod.indices.size() < bestIndexCount) {
            best = &lod.indices;
            bestIndexCount = lod.indices.size();
        }
    }
    return best;
}

[[nodiscard]] const std::vector<std::uint32_t>* overviewSimplifiedSourceIndices(const assets::MeshPrimitive& primitive) noexcept
{
    return coarsestLodIndices(primitive);
}

[[nodiscard]] const std::vector<std::uint32_t>* overviewSourceIndices(const assets::MeshPrimitive& primitive) noexcept
{
    if (const auto* lodIndices = overviewSimplifiedSourceIndices(primitive)) {
        return lodIndices;
    }
    return primitive.indices.empty() ? nullptr : &primitive.indices;
}

[[nodiscard]] std::uint64_t sourceTriangleCountForInstances(
    const assets::ModelAsset& model,
    std::span<const std::uint32_t> instanceIndices) noexcept
{
    std::uint64_t sourceTriangleCount = 0;
    for (const auto instanceIndex : instanceIndices) {
        if (instanceIndex >= model.primitiveInstances.size()) {
            continue;
        }
        const auto& instance = model.primitiveInstances[instanceIndex];
        if (instance.primitiveIndex >= model.primitives.size()) {
            continue;
        }
        sourceTriangleCount += model.primitives[instance.primitiveIndex].indices.size() / 3U;
    }
    return sourceTriangleCount;
}

[[nodiscard]] float projectedRadiusPixels(
    float radius,
    float depth,
    float verticalFovRadians,
    int viewportHeight) noexcept
{
    if (radius <= 0.0F
        || depth <= 0.05F
        || viewportHeight <= 0
        || !std::isfinite(radius)
        || !std::isfinite(depth)
        || !std::isfinite(verticalFovRadians)) {
        return 0.0F;
    }
    const auto tangent = std::tan(verticalFovRadians * 0.5F);
    if (!std::isfinite(tangent) || tangent <= 0.0F) {
        return 0.0F;
    }
    const auto projectionScale = (static_cast<float>(viewportHeight) * 0.5F) / tangent;
    const auto projected = radius * projectionScale / depth;
    return std::isfinite(projected) ? projected : 0.0F;
}

[[nodiscard]] bool overviewScreenEligible(
    bool worldBoundsValid,
    const ViewportWorldBounds& worldBounds,
    const ViewportRenderWorldCamera& camera,
    int viewportHeight,
    const ViewportAssetLodSettings& settings,
    std::uint64_t visibleInstanceReferences,
    std::uint64_t visibleTriangles,
    std::uint64_t visibleChunkCount,
    bool wasHlodActive,
    ViewportHlodReason& reason) noexcept
{
    if (!worldBoundsValid || viewportHeight <= 0) {
        return false;
    }
    const auto evaluation = evaluateViewportHlod(worldBounds, camera, viewportHeight);
    const auto releaseScale = wasHlodActive ? 1.0F - settings.hlodHysteresisRatio : 1.0F;
    const auto rawOverChunkBudget = visibleChunkCount > settings.maxVisibleChunksFromFar;
    const auto rawOverDrawBudget = visibleInstanceReferences > settings.maxDrawPackets
        || visibleTriangles > settings.maxDetailedTriangles;
    const auto overChunkBudget = visibleChunkCount > static_cast<std::uint64_t>(
        static_cast<float>(settings.maxVisibleChunksFromFar) * releaseScale);
    const auto overDrawBudget = visibleInstanceReferences > static_cast<std::uint64_t>(
        static_cast<float>(settings.maxDrawPackets) * releaseScale)
        || visibleTriangles > static_cast<std::uint64_t>(
            static_cast<float>(settings.maxDetailedTriangles) * releaseScale);
    const auto eligible = viewportRootHlodEligible(
        evaluation,
        settings,
        overChunkBudget,
        overDrawBudget,
        wasHlodActive,
        reason);
    if (eligible && wasHlodActive && !rawOverChunkBudget && !rawOverDrawBudget) {
        reason = ViewportHlodReason::Hysteresis;
    }
    return eligible;
}

[[nodiscard]] bool clusterOverviewScreenEligible(
    const ViewportWorldBounds& worldBounds,
    const ViewportRenderWorldCamera& camera,
    int viewportHeight,
    const ViewportAssetLodSettings& settings,
    bool assetOverBudget,
    bool wasHlodActive,
    ViewportHlodReason& reason) noexcept
{
    if (viewportHeight <= 0 || worldBounds.radius <= 0.0F) {
        return false;
    }
    const auto evaluation = evaluateViewportHlod(worldBounds, camera, viewportHeight);
    return viewportClusterHlodEligible(evaluation, settings, assetOverBudget, wasHlodActive, reason);
}

[[nodiscard]] renderer::RenderLodSelectionReason renderHlodReason(ViewportHlodReason reason) noexcept
{
    switch (reason) {
    case ViewportHlodReason::ScreenSize:
        return renderer::RenderLodSelectionReason::HlodScreenSize;
    case ViewportHlodReason::ChunkBudget:
        return renderer::RenderLodSelectionReason::HlodChunkBudget;
    case ViewportHlodReason::DrawBudget:
        return renderer::RenderLodSelectionReason::HlodDrawBudget;
    case ViewportHlodReason::DebugOverride:
        return renderer::RenderLodSelectionReason::HlodDebugOverride;
    case ViewportHlodReason::Hysteresis:
        return renderer::RenderLodSelectionReason::HlodHysteresisHold;
    case ViewportHlodReason::None:
        break;
    }
    return renderer::RenderLodSelectionReason::Unspecified;
}

void updatePrimitiveBounds(assets::MeshPrimitive& primitive) noexcept
{
    if (primitive.vertices.empty()) {
        primitive.bounds = {};
        return;
    }
    auto minimum = primitive.vertices.front().position;
    auto maximum = primitive.vertices.front().position;
    for (const auto& vertex : primitive.vertices) {
        minimum.x = std::min(minimum.x, vertex.position.x);
        minimum.y = std::min(minimum.y, vertex.position.y);
        minimum.z = std::min(minimum.z, vertex.position.z);
        maximum.x = std::max(maximum.x, vertex.position.x);
        maximum.y = std::max(maximum.y, vertex.position.y);
        maximum.z = std::max(maximum.z, vertex.position.z);
    }
    const auto center = (minimum + maximum) * 0.5F;
    float radiusSquared = 0.0F;
    for (const auto& vertex : primitive.vertices) {
        radiusSquared = std::max(radiusSquared, math::distanceSquared(center, vertex.position));
    }
    primitive.bounds.minimum = minimum;
    primitive.bounds.maximum = maximum;
    primitive.bounds.center = center;
    primitive.bounds.radius = std::sqrt(std::max(radiusSquared, 0.0F));
    if (!std::isfinite(primitive.bounds.radius)) {
        primitive.bounds = {};
    }
}

[[nodiscard]] std::vector<CachedOverviewDraw> buildOverviewDrawsForInstances(
    const assets::ModelAsset& model,
    std::span<const std::uint32_t> instanceIndices,
    std::uint64_t salt)
{
    std::vector<CachedOverviewDraw> cached;
    if (instanceIndices.empty()) {
        return cached;
    }

    std::uint64_t overviewCandidateTriangles = 0;
    for (const auto instanceIndex : instanceIndices) {
        if (instanceIndex >= model.primitiveInstances.size()) {
            return cached;
        }
        const auto& instance = model.primitiveInstances[instanceIndex];
        if (instance.primitiveIndex >= model.primitives.size()) {
            return cached;
        }
        const auto& source = model.primitives[instance.primitiveIndex];
        if (source.materialIndex >= model.materials.size()) {
            return cached;
        }
        const auto* indices = overviewSourceIndices(source);
        if (indices == nullptr || indices->size() < 3U) {
            return cached;
        }
        overviewCandidateTriangles += indices->size() / 3U;
    }
    if (overviewCandidateTriangles == 0U || overviewCandidateTriangles > kOverviewMaxTriangles) {
        return cached;
    }

    std::vector<MaterialOverview> overviews;
    overviews.reserve(32U);
    std::uint64_t overviewSourceTriangles = 0;
    for (const auto instanceIndex : instanceIndices) {
        if (instanceIndex >= model.primitiveInstances.size()) {
            return {};
        }
        const auto& instance = model.primitiveInstances[instanceIndex];
        if (instance.primitiveIndex >= model.primitives.size()) {
            return {};
        }
        const auto& source = model.primitives[instance.primitiveIndex];
        if (source.materialIndex >= model.materials.size()) {
            return {};
        }
        const auto* indices = overviewSourceIndices(source);
        if (indices == nullptr || indices->size() < 3U) {
            return {};
        }
        if (std::any_of(indices->begin(), indices->end(), [&source](std::uint32_t index) {
                return index >= source.vertices.size();
            })) {
            return {};
        }
        auto overview = std::find_if(overviews.begin(), overviews.end(), [&source](const MaterialOverview& value) {
            return value.materialIndex == source.materialIndex;
        });
        if (overview == overviews.end()) {
            MaterialOverview next;
            next.materialIndex = source.materialIndex;
            next.primitive.materialIndex = source.materialIndex;
            overviews.push_back(std::move(next));
            overview = overviews.end() - 1;
        }

        const auto sourceTriangles = source.indices.size() / 3U;
        if (overview->primitive.vertices.size() + source.vertices.size() > std::numeric_limits<std::uint32_t>::max()) {
            return {};
        }
        const auto localMatrix = renderMatrix(instance.transform);
        const auto vertexBase = static_cast<std::uint32_t>(overview->primitive.vertices.size());
        for (const auto& sourceVertex : source.vertices) {
            auto vertex = sourceVertex;
            vertex.position = transformMatrixPoint(localMatrix, vertex.position);
            vertex.normal = safeNormalized(transformMatrixVector(localMatrix, vertex.normal), vertex.normal);
            vertex.tangent = safeNormalized(transformMatrixVector(localMatrix, vertex.tangent), vertex.tangent);
            overview->primitive.vertices.push_back(vertex);
        }
        for (std::size_t index = 2; index < indices->size(); index += 3U) {
            const std::array<std::uint32_t, 3> triangle {{
                (*indices)[index - 2U],
                instance.flipsWinding ? (*indices)[index] : (*indices)[index - 1U],
                instance.flipsWinding ? (*indices)[index - 1U] : (*indices)[index],
            }};
            overview->primitive.indices.push_back(vertexBase + triangle[0]);
            overview->primitive.indices.push_back(vertexBase + triangle[1]);
            overview->primitive.indices.push_back(vertexBase + triangle[2]);
        }
        overview->sourceTriangleCount += sourceTriangles;
        overviewSourceTriangles += sourceTriangles;
    }

    const auto sourceTriangleCount = sourceTriangleCountForInstances(model, instanceIndices);
    if (sourceTriangleCount == 0U || overviewSourceTriangles != sourceTriangleCount) {
        return cached;
    }

    const auto overviewCanReduceDrawWork = overviews.size() < model.primitiveInstances.size()
        && overviews.size() < instanceIndices.size();
    cached.reserve(overviews.size());
    for (auto& overview : overviews) {
        const auto overviewTriangles = static_cast<std::uint64_t>(overview.primitive.indices.size() / 3U);
        if (overview.primitive.indices.size() < 3U
            || (!overviewCanReduceDrawWork && overview.sourceTriangleCount <= overviewTriangles)) {
            continue;
        }
        updatePrimitiveBounds(overview.primitive);
        if (overview.primitive.bounds.radius <= 0.0F) {
            continue;
        }
        CachedOverviewDraw draw;
        draw.modelAssetId = assets::AssetId(
            mixHash(model.id.value(), mixHash(salt, static_cast<std::uint64_t>(cached.size())))
            | 0x8000000000000000ULL);
        draw.primitiveIndex = renderer::kRenderOverviewPrimitiveIndexBase + static_cast<std::uint32_t>(cached.size());
        draw.sourceTriangleCount = overview.sourceTriangleCount;
        draw.primitive = std::make_shared<const assets::MeshPrimitive>(std::move(overview.primitive));
        cached.push_back(std::move(draw));
    }
    const auto cachedSourceTriangles = std::accumulate(
        cached.begin(),
        cached.end(),
        std::uint64_t {0},
        [](std::uint64_t total, const CachedOverviewDraw& draw) {
            return total + draw.sourceTriangleCount;
        });
    if (cachedSourceTriangles != sourceTriangleCount) {
        cached.clear();
    }
    return cached;
}

[[nodiscard]] const CachedOverviewModel& cachedOverviewForModel(const assets::ModelAsset& model)
{
    static const CachedOverviewModel kEmpty;
    static std::unordered_map<std::uint64_t, CachedOverviewModel> cache;

    const auto cacheKey = model.id.value();
    if (const auto existing = cache.find(cacheKey); existing != cache.end()) {
        return existing->second;
    }

    CachedOverviewModel cached;
    if (model.primitiveInstances.empty()) {
        auto [inserted, _] = cache.emplace(cacheKey, std::move(cached));
        return inserted->second;
    }

    std::vector<std::uint32_t> allInstances;
    allInstances.reserve(model.primitiveInstances.size());
    for (std::uint32_t index = 0; index < model.primitiveInstances.size(); ++index) {
        allInstances.push_back(index);
    }

    const auto sourceTriangleCount = sourceTriangleCountForInstances(model, allInstances);
    if (sourceTriangleCount < kOverviewMinSourceTriangles || sourceTriangleCount > kOverviewMaxSourceTriangles) {
        auto [inserted, _] = cache.emplace(cacheKey, std::move(cached));
        return inserted->second;
    }

    cached.fullDraws = buildOverviewDrawsForInstances(model, allInstances, 0x4810d00dULL);
    if (!cached.fullDraws.empty() && !model.primitiveClusters.empty()) {
        cached.clusterDraws.resize(model.primitiveClusters.size());
        for (std::size_t clusterIndex = 0; clusterIndex < model.primitiveClusters.size(); ++clusterIndex) {
            const auto& cluster = model.primitiveClusters[clusterIndex];
            if (cluster.primitiveInstanceIndices.size() < 2U) {
                continue;
            }
            cached.clusterDraws[clusterIndex] = buildOverviewDrawsForInstances(
                model,
                cluster.primitiveInstanceIndices,
                mixHash(0x4810c10dULL, static_cast<std::uint64_t>(clusterIndex)));
        }
    }

    auto [inserted, _] = cache.emplace(cacheKey, std::move(cached));
    return inserted->second.fullDraws.empty() ? kEmpty : inserted->second;
}

} // namespace

void ViewportRenderWorld::finalizeEntityRecord(EntityRecord& record) const
{
    ViewportFrameBounds bounds;
    for (const auto& chunk : record.chunks) {
        bounds.includeSphere(chunk.worldBounds.center, chunk.worldBounds.radius);
    }
    if (bounds.valid) {
        assets::MeshBounds assetBounds;
        assetBounds.minimum = bounds.minimum;
        assetBounds.maximum = bounds.maximum;
        assetBounds.center = bounds.center();
        assetBounds.radius = bounds.radius();
        record.worldBounds = transformViewportBounds(identityMatrix(), assetBounds);
        record.worldBoundsValid = true;
    }
    rebuildChunkSpatialCells(record);

    record.sourceTriangleCount = 0;
    for (const auto& instance : record.instances) {
        if (instance.primitiveIndex >= instance.model->primitives.size()) {
            continue;
        }
        record.sourceTriangleCount += instance.model->primitives[instance.primitiveIndex].indices.size() / 3U;
    }
    record.overviewDraws.clear();
    if (!kOverviewHlodEnabled) {
        return;
    }
    if (record.instances.empty() || record.instances.front().model == nullptr) {
        return;
    }
    for (auto& chunk : record.chunks) {
        chunk.overviewDraws.clear();
    }
    const auto makeOverviewDraw = [&record](const CachedOverviewDraw& cached) {
        EntityRecord::OverviewDraw draw;
        draw.renderInstanceId = mixHash(record.entityId.value(), mixHash(cached.modelAssetId.value(), cached.primitiveIndex));
        draw.modelAssetId = cached.modelAssetId;
        draw.primitiveIndex = cached.primitiveIndex;
        draw.primitive = cached.primitive;
        draw.modelMatrix = record.modelMatrix;
        draw.worldBounds = transformViewportBounds(record.modelMatrix, cached.primitive->bounds);
        draw.sourceTriangleCount = cached.sourceTriangleCount;
        return draw;
    };
    const auto& cachedOverviews = cachedOverviewForModel(*record.instances.front().model);
    record.overviewDraws.reserve(cachedOverviews.fullDraws.size());
    for (const auto& cached : cachedOverviews.fullDraws) {
        if (cached.primitive == nullptr) {
            continue;
        }
        record.overviewDraws.push_back(makeOverviewDraw(cached));
    }
    const auto* model = record.instances.front().model.get();
    for (auto& chunk : record.chunks) {
        if (chunk.sourceClusterIndex >= cachedOverviews.clusterDraws.size()
            || chunk.sourceClusterIndex >= model->primitiveClusters.size()) {
            continue;
        }
        const auto& sourceCluster = model->primitiveClusters[chunk.sourceClusterIndex];
        if (chunk.instanceIndices.size() != sourceCluster.primitiveInstanceIndices.size()) {
            continue;
        }
        const auto& cachedChunkOverviews = cachedOverviews.clusterDraws[chunk.sourceClusterIndex];
        chunk.overviewDraws.reserve(cachedChunkOverviews.size());
        for (const auto& cached : cachedChunkOverviews) {
            if (cached.primitive == nullptr) {
                continue;
            }
            chunk.overviewDraws.push_back(makeOverviewDraw(cached));
        }
    }
}

void ViewportRenderWorld::rebuildOverviewRecords()
{
    overviewRecords_.clear();
}

bool ViewportRenderWorld::tryEmitOverviewRecord(
    const EntityRecord& record,
    assets::AssetId /*selectedPrimitiveModel*/,
    std::uint32_t /*selectedPrimitiveIndex*/,
    const ViewportRenderWorldCamera& camera,
    const renderer::RenderMatrix4& viewProjection,
    int viewportHeight,
    const ViewportAssetLodSettings& lodSettings,
    const ViewportOcclusionBuffer* occlusionBuffer,
    const std::unordered_set<std::uint64_t>* occluderChunkIds,
    bool countVisibleChunks,
    std::vector<renderer::RenderMeshDraw>& meshDraws,
    ViewportRenderWorldStats& stats,
    ViewportFrameBounds& visibleBounds,
    std::uint64_t& visibleSourceTriangleCount,
    std::unordered_set<std::uint64_t>* overviewCoveredChunkIds)
{
    if (!kOverviewHlodEnabled) {
        return false;
    }
    if (record.generatedTerrainModel && lodSettings.debugDisableTerrainHlod) {
        rootHlodHistory_.insert_or_assign(record.entityId.value(), false);
        return false;
    }

    std::uint64_t visibleChunkCount = 0;
    std::uint64_t visibleChunkInstanceReferences = 0;
    std::uint64_t visibleChunkTriangles = 0;
    std::vector<const EntityRecord::Chunk*> visibleChunks;
    visibleChunks.reserve(record.chunks.size());
    forEachSpatialChunkCandidate(record, camera, stats, [&](std::size_t, const auto& chunk) {
        if (lodSettings.frustumCullingEnabled && !viewportBoundsVisible(
                chunk.worldBounds,
                camera.eye,
                camera.right,
                camera.up,
                camera.forward,
                camera.verticalFovRadians,
                camera.aspectRatio,
                camera.nearPlane,
                camera.farPlane,
                lodSettings.cullingBoundsPadding)) {
            return;
        }
        ++visibleChunkCount;
        const auto chunkIsOccluder = occluderChunkIds != nullptr
            && occluderChunkIds->find(chunk.renderChunkId) != occluderChunkIds->end();
        if (!chunkIsOccluder && occlusionBuffer != nullptr && occlusionBuffer->hasOccluders()
            && viewportChunkHasOnlyOpaqueMaterials(record, chunk)) {
            ++stats.occlusionTestedChunkCount;
            if (occlusionBuffer->isOccluded(chunk.worldBounds)) {
                ++stats.occlusionRejectedChunkCount;
                stats.occlusionRejectedInstanceCount += static_cast<std::uint64_t>(chunk.instanceIndices.size());
                stats.occlusionRejectedTriangleCount += chunk.triangleCount;
                return;
            }
        }
        visibleChunkInstanceReferences += static_cast<std::uint64_t>(chunk.instanceIndices.size());
        visibleChunkTriangles += chunk.triangleCount;
        visibleChunks.push_back(&chunk);
    }, !lodSettings.spatialCellCullingEnabled || !lodSettings.frustumCullingEnabled, lodSettings.cullingBoundsPadding);
    if (visibleChunkCount == 0U) {
        return false;
    }
    if (countVisibleChunks) {
        stats.visibleRenderChunkCount += visibleChunkCount;
    }
    auto terrainChunkNearHighQuality = [&](const EntityRecord::Chunk& chunk) {
        if (!record.generatedTerrainModel || !lodSettings.terrainNearHighQualityEnabled) {
            return false;
        }
        const auto evaluation = evaluateViewportHlod(chunk.worldBounds, camera, viewportHeight);
        return evaluation.insideBounds || evaluation.distance <= lodSettings.terrainNearHighQualityRadius;
    };
    const auto terrainNearHighQualityVisible = record.generatedTerrainModel
        && lodSettings.terrainNearHighQualityEnabled
        && std::any_of(visibleChunks.begin(), visibleChunks.end(), [&](const auto* chunk) {
            return chunk != nullptr && terrainChunkNearHighQuality(*chunk);
        });
    const auto visibleChunkRatio = record.chunks.empty()
        ? 0.0F
        : static_cast<float>(visibleChunkCount) / static_cast<float>(record.chunks.size());
    const auto visibleInstanceRatio = record.instances.empty()
        ? 0.0F
        : static_cast<float>(std::min<std::uint64_t>(visibleChunkInstanceReferences, record.instances.size()))
            / static_cast<float>(record.instances.size());
    const auto overChunkBudget = visibleChunkCount > lodSettings.maxVisibleChunksFromFar;
    const auto overDrawBudget = visibleChunkInstanceReferences > lodSettings.maxDrawPackets
        || visibleChunkTriangles > lodSettings.maxDetailedTriangles;
    const auto enoughVisibleWork = visibleChunkInstanceReferences >= kOverviewMinVisibleInstanceReferences
        || visibleChunkTriangles >= kOverviewMinVisibleTriangles;
    const auto rootHistoryIt = rootHlodHistory_.find(record.entityId.value());
    const auto wasRootHlodActive = rootHistoryIt != rootHlodHistory_.end() && rootHistoryIt->second;
    const auto coverageHysteresis = wasRootHlodActive ? lodSettings.hlodHysteresisRatio : 0.0F;
    const auto broadOverviewCoverage = visibleChunkRatio >= 0.45F * (1.0F - coverageHysteresis)
        || visibleInstanceRatio >= 0.45F * (1.0F - coverageHysteresis)
        || ((overChunkBudget || overDrawBudget)
            && (visibleChunkRatio >= 0.30F * (1.0F - coverageHysteresis)
                || visibleInstanceRatio >= 0.30F * (1.0F - coverageHysteresis)));
    const auto overviewCoverageEnough = broadOverviewCoverage
        && enoughVisibleWork;
    ViewportHlodReason rootHlodReason = ViewportHlodReason::None;
    const auto wantsOverview = !record.overviewDraws.empty()
        && !terrainNearHighQualityVisible
        && overviewCoverageEnough
        && overviewScreenEligible(
            record.worldBoundsValid,
            record.worldBounds,
            camera,
            viewportHeight,
            lodSettings,
            visibleChunkInstanceReferences,
            visibleChunkTriangles,
            visibleChunkCount,
            wasRootHlodActive,
            rootHlodReason);
    rootHlodHistory_.insert_or_assign(record.entityId.value(), wantsOverview);
    bool emittedOverview = false;
    const auto model = record.instances.empty() ? std::shared_ptr<const assets::ModelAsset> {} : record.instances.front().model;
    if (model == nullptr) {
        return false;
    }

    auto visibleClusterOverviewDraws = std::uint64_t {0};
    for (const auto* chunk : visibleChunks) {
        if (chunk != nullptr) {
            visibleClusterOverviewDraws += static_cast<std::uint64_t>(chunk->overviewDraws.size());
        }
    }
    stats.hlodCandidateDrawCount += std::max<std::uint64_t>(
        static_cast<std::uint64_t>(record.overviewDraws.size()),
        visibleClusterOverviewDraws);
    if (record.overviewDraws.empty() && visibleClusterOverviewDraws == 0U) {
        ++stats.hlodRejectedNoOverviewCount;
    }
    if (!enoughVisibleWork) {
        ++stats.hlodRejectedVisibleWorkCount;
    } else if (!overviewCoverageEnough) {
        ++stats.hlodRejectedCoverageCount;
    } else if (!record.overviewDraws.empty()
        && !overviewScreenEligible(
            record.worldBoundsValid,
            record.worldBounds,
            camera,
            viewportHeight,
            lodSettings,
            visibleChunkInstanceReferences,
            visibleChunkTriangles,
            visibleChunkCount,
            wasRootHlodActive,
            rootHlodReason)) {
        ++stats.hlodRejectedScreenCount;
    }

    const auto emitOverview = [&](
        const EntityRecord::OverviewDraw& overview,
        std::uint64_t renderChunkId,
        ViewportHlodReason reason) {
        if (overview.primitive == nullptr || overview.primitive->materialIndex >= model->materials.size()) {
            return;
        }
        const auto& material = model->materials[overview.primitive->materialIndex];
        const auto sortDepth = math::dot(overview.worldBounds.center - camera.eye, camera.forward);
        const auto sourceTriangleCount = overview.sourceTriangleCount;
        const auto selectedTriangleCount = static_cast<std::uint64_t>(overview.primitive->indices.size() / 3U);
        visibleSourceTriangleCount += sourceTriangleCount;
        ++stats.hlodMeshDrawCount;
        if (sourceTriangleCount > selectedTriangleCount) {
            stats.hlodTriangleReductionCount += sourceTriangleCount - selectedTriangleCount;
        }
        ++stats.visibleRenderInstanceCount;
        visibleBounds.includeSphere(overview.worldBounds.center, overview.worldBounds.radius);
        meshDraws.push_back({
            overview.modelAssetId,
            overview.primitiveIndex,
            0U,
            overview.primitive.get(),
            &material,
            modelTexture(*model, material.baseColorTexture),
            modelTexture(*model, material.normalTexture),
            modelTexture(*model, material.metallicRoughnessTexture),
            modelTexture(*model, material.occlusionTexture),
            modelTexture(*model, material.emissiveTexture),
            sortDepth,
            {overview.worldBounds.center.x, overview.worldBounds.center.y, overview.worldBounds.center.z},
            overview.worldBounds.radius,
            overview.modelMatrix,
            multiply(viewProjection, overview.modelMatrix),
            false,
            false,
            overview.renderInstanceId,
            renderChunkId,
            record.entityId.value(),
        });
        auto& draw = meshDraws.back();
        draw.materialIndex = static_cast<std::uint32_t>(overview.primitive->materialIndex);
        draw.generatedTerrainModel = record.generatedTerrainModel;
        const auto evaluation = evaluateViewportHlod(overview.worldBounds, camera, viewportHeight);
        draw.distanceToCameraCenter = evaluation.distanceToCenter;
        draw.distanceToCameraBounds = evaluation.distance;
        draw.projectedRadiusPixels = evaluation.projectedRadiusPixels;
        draw.cameraInsideChunkBounds = evaluation.insideBounds;
        draw.lodSelectionReason = renderHlodReason(reason);
        draw.lodHysteresisActive = reason == ViewportHlodReason::Hysteresis;
        emittedOverview = true;
    };

    if (wantsOverview) {
        const auto renderChunkId = mixHash(record.entityId.value(), 0x4810d00dULL);
        for (const auto& overview : record.overviewDraws) {
            emitOverview(overview, renderChunkId, rootHlodReason);
        }
        if (emittedOverview) {
            stats.hlodCollapsedChunkCount += visibleChunkCount > 1U ? visibleChunkCount - 1U : 0U;
            const auto evaluation = evaluateViewportHlod(record.worldBounds, camera, viewportHeight);
            stats.hlodScreenCoverage = std::max(stats.hlodScreenCoverage, evaluation.screenCoverage);
            stats.hlodCameraDistance = evaluation.distance;
            (void)rootHlodReason;
        }
        return emittedOverview;
    }
    if (!enoughVisibleWork) {
        return false;
    }
    for (const auto* chunk : visibleChunks) {
        if (chunk == nullptr || chunk->overviewDraws.empty()) {
            continue;
        }
        const auto historyIt = chunkHlodHistory_.find(chunk->renderChunkId);
        const auto wasChunkHlodActive = historyIt != chunkHlodHistory_.end() && historyIt->second;
        ViewportHlodReason clusterReason = ViewportHlodReason::None;
        const auto terrainClusterNearHighQuality = record.generatedTerrainModel
            && terrainChunkNearHighQuality(*chunk);
        const auto clusterEligible = clusterOverviewScreenEligible(
                chunk->worldBounds,
                camera,
                viewportHeight,
                lodSettings,
                overChunkBudget || overDrawBudget,
                wasChunkHlodActive,
                clusterReason)
            && !terrainClusterNearHighQuality;
        chunkHlodHistory_.insert_or_assign(chunk->renderChunkId, clusterEligible);
        if (overviewCoveredChunkIds != nullptr && !clusterEligible) {
            ++stats.hlodRejectedClusterScreenCount;
            continue;
        }
        for (const auto& overview : chunk->overviewDraws) {
            emitOverview(overview, chunk->renderChunkId, clusterReason);
        }
        if (overviewCoveredChunkIds != nullptr) {
            overviewCoveredChunkIds->insert(chunk->renderChunkId);
        }
    }
    return overviewCoveredChunkIds == nullptr && emittedOverview;
}

} // namespace projectunity::editor
