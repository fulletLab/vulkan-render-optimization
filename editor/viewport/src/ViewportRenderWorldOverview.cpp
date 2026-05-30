#include "ViewportRenderWorld.hpp"
#include "ViewportRenderWorldRecord.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace projectunity::editor {
namespace {

[[nodiscard]] std::uint64_t mixHash(std::uint64_t seed, std::uint64_t value) noexcept
{
    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U));
}

[[nodiscard]] float matrixAt(const renderer::RenderMatrix4& matrix, int row, int column)
{
    return matrix.values[static_cast<std::size_t>(column * 4 + row)];
}

[[nodiscard]] renderer::RenderMatrix4 identityMatrix()
{
    return {};
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

[[nodiscard]] const std::vector<std::uint32_t>* overviewSourceIndices(const assets::MeshPrimitive& primitive) noexcept
{
    if (const auto* lod = coarsestLodIndices(primitive)) {
        return lod;
    }
    return primitive.indices.empty() ? nullptr : &primitive.indices;
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

} // namespace

void ViewportRenderWorld::finalizeEntityRecord(EntityRecord& record) const
{
    constexpr std::uint64_t kOverviewMinSourceTriangles = 250'000ULL;
    constexpr std::uint64_t kOverviewTargetTriangles = 300'000ULL;
    constexpr std::uint32_t kOverviewPrimitiveIndexBase = 0x80000000U;

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

    record.sourceTriangleCount = 0;
    for (const auto& instance : record.instances) {
        if (instance.primitiveIndex >= instance.model->primitives.size()) {
            continue;
        }
        record.sourceTriangleCount += instance.model->primitives[instance.primitiveIndex].indices.size() / 3U;
    }
    record.overviewDraws.clear();
    if (record.sourceTriangleCount < kOverviewMinSourceTriangles) {
        return;
    }

    struct MaterialOverview {
        std::size_t materialIndex {0};
        assets::MeshPrimitive primitive;
        std::uint64_t sourceTriangleCount {0};
    };

    std::vector<MaterialOverview> overviews;
    overviews.reserve(32U);
    std::uint64_t overviewSourceTriangles = 0;
    std::uint64_t overviewCandidateTriangles = 0;
    for (const auto& instance : record.instances) {
        if (instance.primitiveIndex >= instance.model->primitives.size()) {
            continue;
        }
        const auto& source = instance.model->primitives[instance.primitiveIndex];
        if (source.materialIndex >= instance.model->materials.size()) {
            continue;
        }
        const auto* indices = overviewSourceIndices(source);
        if (indices == nullptr || indices->size() < 3U) {
            continue;
        }
        overviewCandidateTriangles += indices->size() / 3U;
    }
    const auto overviewTriangleStride = std::max<std::uint64_t>(
        1U,
        (overviewCandidateTriangles + kOverviewTargetTriangles - 1U) / kOverviewTargetTriangles);
    std::uint64_t overviewTriangleOrdinal = 0;
    for (const auto& instance : record.instances) {
        if (instance.primitiveIndex >= instance.model->primitives.size()) {
            continue;
        }
        const auto& source = instance.model->primitives[instance.primitiveIndex];
        if (source.materialIndex >= instance.model->materials.size()) {
            continue;
        }
        const auto* indices = overviewSourceIndices(source);
        if (indices == nullptr || indices->size() < 3U) {
            continue;
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
        overview->sourceTriangleCount += sourceTriangles;
        overviewSourceTriangles += sourceTriangles;
        for (std::size_t index = 2; index < indices->size(); index += 3U) {
            if ((overviewTriangleOrdinal++ % overviewTriangleStride) != 0U) {
                continue;
            }
            const std::array<std::uint32_t, 3> triangle {{
                (*indices)[index - 2U],
                instance.flipsWinding ? (*indices)[index] : (*indices)[index - 1U],
                instance.flipsWinding ? (*indices)[index - 1U] : (*indices)[index],
            }};
            if (triangle[0] >= source.vertices.size()
                || triangle[1] >= source.vertices.size()
                || triangle[2] >= source.vertices.size()
                || overview->primitive.vertices.size() + 3U > std::numeric_limits<std::uint32_t>::max()) {
                continue;
            }
            const auto vertexBase = static_cast<std::uint32_t>(overview->primitive.vertices.size());
            for (std::uint32_t corner = 0; corner < 3U; ++corner) {
                auto vertex = source.vertices[triangle[corner]];
                vertex.position = transformMatrixPoint(instance.modelMatrix, vertex.position);
                vertex.normal = safeNormalized(transformMatrixVector(instance.modelMatrix, vertex.normal), vertex.normal);
                vertex.tangent = safeNormalized(transformMatrixVector(instance.modelMatrix, vertex.tangent), vertex.tangent);
                overview->primitive.vertices.push_back(vertex);
                overview->primitive.indices.push_back(vertexBase + corner);
            }
        }
    }

    if (overviewSourceTriangles < record.sourceTriangleCount / 2U) {
        return;
    }

    const auto overviewModelAssetId = assets::AssetId(
        mixHash(record.instances.front().modelAssetId.value(), mixHash(record.signature, 0x4810d00dULL))
        | 0x8000000000000000ULL);
    record.overviewDraws.reserve(overviews.size());
    for (auto& overview : overviews) {
        if (overview.primitive.indices.size() < 3U
            || overview.sourceTriangleCount <= overview.primitive.indices.size() / 3U) {
            continue;
        }
        updatePrimitiveBounds(overview.primitive);
        if (overview.primitive.bounds.radius <= 0.0F) {
            continue;
        }
        EntityRecord::OverviewDraw draw;
        draw.renderInstanceId = mixHash(record.entityId.value(), mixHash(overview.materialIndex, 0x4810d00dULL));
        draw.modelAssetId = overviewModelAssetId;
        draw.primitiveIndex = kOverviewPrimitiveIndexBase + static_cast<std::uint32_t>(record.overviewDraws.size());
        draw.worldBounds = transformViewportBounds(identityMatrix(), overview.primitive.bounds);
        draw.sourceTriangleCount = overview.sourceTriangleCount;
        draw.primitive = std::move(overview.primitive);
        record.overviewDraws.push_back(std::move(draw));
    }
}

void ViewportRenderWorld::rebuildOverviewRecords()
{
    overviewRecords_.clear();
    struct OverviewGroup {
        std::shared_ptr<EntityRecord> record;
    };
    std::unordered_map<std::uint64_t, OverviewGroup> groups;
    for (const auto* sourceRecord : orderedRecords_) {
        if (sourceRecord == nullptr || sourceRecord->instances.empty()) {
            continue;
        }
        const auto modelAssetId = sourceRecord->instances.front().modelAssetId;
        auto& group = groups[modelAssetId.value()];
        if (group.record == nullptr) {
            group.record = std::make_shared<EntityRecord>();
            group.record->entityId = sourceRecord->entityId;
            group.record->signature = mixHash(modelAssetId.value(), 0x4810d00dULL);
            group.record->hasMeshSceneContent = true;
            group.record->overviewOnly = true;
            group.record->coveredModelAssetId = modelAssetId;
        }
        const auto instanceBase = group.record->instances.size();
        group.record->instances.insert(
            group.record->instances.end(),
            sourceRecord->instances.begin(),
            sourceRecord->instances.end());
        for (const auto& sourceChunk : sourceRecord->chunks) {
            EntityRecord::Chunk chunk;
            chunk.renderChunkId = mixHash(sourceChunk.renderChunkId, sourceRecord->entityId.value());
            chunk.sceneNodeId = sourceChunk.sceneNodeId;
            chunk.worldBounds = sourceChunk.worldBounds;
            chunk.triangleCount = sourceChunk.triangleCount;
            chunk.instanceIndices.reserve(sourceChunk.instanceIndices.size());
            for (const auto sourceIndex : sourceChunk.instanceIndices) {
                if (sourceIndex < sourceRecord->instances.size()) {
                    chunk.instanceIndices.push_back(instanceBase + sourceIndex);
                }
            }
            if (!chunk.instanceIndices.empty()) {
                group.record->chunks.push_back(std::move(chunk));
            }
        }
        group.record->signature = mixHash(group.record->signature, sourceRecord->signature);
    }

    overviewRecords_.reserve(groups.size());
    for (auto& [modelAssetId, group] : groups) {
        (void)modelAssetId;
        if (group.record == nullptr) {
            continue;
        }
        finalizeEntityRecord(*group.record);
        if (!group.record->overviewDraws.empty()) {
            overviewRecords_.push_back(std::move(group.record));
        }
    }
}

bool ViewportRenderWorld::tryEmitOverviewRecord(
    const EntityRecord& record,
    assets::AssetId selectedPrimitiveModel,
    std::uint32_t selectedPrimitiveIndex,
    const ViewportRenderWorldCamera& camera,
    const renderer::RenderMatrix4& viewProjection,
    bool countVisibleChunks,
    std::vector<renderer::RenderMeshDraw>& meshDraws,
    ViewportRenderWorldStats& stats,
    ViewportFrameBounds& visibleBounds,
    std::uint64_t& visibleSourceTriangleCount) const
{
    std::uint64_t visibleChunkCount = 0;
    std::uint64_t visibleChunkInstanceReferences = 0;
    for (const auto& chunk : record.chunks) {
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
        ++visibleChunkCount;
        visibleChunkInstanceReferences += static_cast<std::uint64_t>(chunk.instanceIndices.size());
    }
    if (visibleChunkCount == 0U) {
        return false;
    }
    if (countVisibleChunks) {
        stats.visibleRenderChunkCount += visibleChunkCount;
    }
    const auto visibleChunkRatio = record.chunks.empty()
        ? 0.0F
        : static_cast<float>(visibleChunkCount) / static_cast<float>(record.chunks.size());
    const auto visibleInstanceRatio = record.instances.empty()
        ? 0.0F
        : static_cast<float>(std::min<std::uint64_t>(visibleChunkInstanceReferences, record.instances.size()))
            / static_cast<float>(record.instances.size());
    const auto selectedPrimitiveInRecord = selectedPrimitiveModel.isValid()
        && std::any_of(record.instances.begin(), record.instances.end(), [&](const EntityRecord::Instance& instance) {
            return instance.modelAssetId == selectedPrimitiveModel
                && instance.primitiveInstanceIndex == selectedPrimitiveIndex;
        });
    const auto selectedPrimitiveBlocksOverview = selectedPrimitiveInRecord && !record.overviewOnly;
    const auto overviewCoverageEnough = record.overviewOnly
        ? visibleChunkCount > 0U
        : ((visibleChunkRatio >= 0.60F || visibleInstanceRatio >= 0.60F)
            && visibleChunkInstanceReferences >= 1024U);
    const auto wantsOverview = !selectedPrimitiveBlocksOverview
        && !record.overviewDraws.empty()
        && overviewCoverageEnough;
    if (!wantsOverview) {
        return false;
    }

    bool emittedOverview = false;
    const auto model = record.instances.empty() ? std::shared_ptr<const assets::ModelAsset> {} : record.instances.front().model;
    if (model == nullptr) {
        return false;
    }
    for (const auto& overview : record.overviewDraws) {
        if (overview.primitive.materialIndex >= model->materials.size()) {
            continue;
        }
        const auto& material = model->materials[overview.primitive.materialIndex];
        const auto sortDepth = math::dot(overview.worldBounds.center - camera.eye, camera.forward);
        const auto sourceTriangleCount = overview.sourceTriangleCount;
        const auto selectedTriangleCount = static_cast<std::uint64_t>(overview.primitive.indices.size() / 3U);
        visibleSourceTriangleCount += sourceTriangleCount;
        ++stats.hlodMeshDrawCount;
        if (sourceTriangleCount > selectedTriangleCount) {
            ++stats.lodMeshDrawCount;
            stats.lodTriangleReductionCount += sourceTriangleCount - selectedTriangleCount;
            stats.hlodTriangleReductionCount += sourceTriangleCount - selectedTriangleCount;
        }
        ++stats.visibleRenderInstanceCount;
        visibleBounds.includeSphere(overview.worldBounds.center, overview.worldBounds.radius);
        meshDraws.push_back({
            overview.modelAssetId,
            overview.primitiveIndex,
            0U,
            &overview.primitive,
            &material,
            modelTexture(*model, material.baseColorTexture),
            modelTexture(*model, material.normalTexture),
            modelTexture(*model, material.metallicRoughnessTexture),
            modelTexture(*model, material.occlusionTexture),
            modelTexture(*model, material.emissiveTexture),
            sortDepth,
            {overview.worldBounds.center.x, overview.worldBounds.center.y, overview.worldBounds.center.z},
            overview.worldBounds.radius,
            renderer::RenderMatrix4 {},
            viewProjection,
            false,
            false,
            overview.renderInstanceId,
            mixHash(record.entityId.value(), 0x4810d00dULL),
            record.entityId.value(),
        });
        emittedOverview = true;
    }
    return emittedOverview;
}

} // namespace projectunity::editor
