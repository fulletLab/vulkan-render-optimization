#include "ViewportRenderWorld.hpp"

#include "ViewportRenderWorldDiagnostics.hpp"
#include "ViewportMeshLod.hpp"
#include "ViewportRenderWorldRecord.hpp"
#include "ViewportRenderWorldShadowPolicy.hpp"
#include "ViewportRendererCulling.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <span>
#include <unordered_set>
#include <vector>

namespace projectunity::editor {
namespace {

struct PrimitiveProxyKey {
    std::uint64_t modelAssetId {0};
    std::uint32_t primitiveInstanceIndex {0};
};

[[nodiscard]] std::uint64_t mixHash(std::uint64_t seed, std::uint64_t value) noexcept
{
    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U));
}

[[nodiscard]] std::uint64_t floatBits(float value) noexcept
{
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(value));
    return bits;
}

[[nodiscard]] std::uint64_t hashVec3(std::uint64_t seed, math::Vec3 value) noexcept
{
    seed = mixHash(seed, floatBits(value.x));
    seed = mixHash(seed, floatBits(value.y));
    return mixHash(seed, floatBits(value.z));
}

[[nodiscard]] std::uint64_t primitiveProxyKey(PrimitiveProxyKey key) noexcept
{
    return mixHash(key.modelAssetId, key.primitiveInstanceIndex);
}

[[nodiscard]] float& matrixAt(renderer::RenderMatrix4& matrix, int row, int column)
{
    return matrix.values[static_cast<std::size_t>(column * 4 + row)];
}

[[nodiscard]] float matrixAt(const renderer::RenderMatrix4& matrix, int row, int column)
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

[[nodiscard]] float radians(float degrees)
{
    return degrees * 0.01745329251994329577F;
}

[[nodiscard]] math::Vec3 rotateEuler(math::Vec3 value, math::Vec3 rotationEuler)
{
    const auto sinX = std::sin(radians(rotationEuler.x));
    const auto cosX = std::cos(radians(rotationEuler.x));
    const auto sinY = std::sin(radians(rotationEuler.y));
    const auto cosY = std::cos(radians(rotationEuler.y));
    const auto sinZ = std::sin(radians(rotationEuler.z));
    const auto cosZ = std::cos(radians(rotationEuler.z));
    value = {value.x, value.y * cosX - value.z * sinX, value.y * sinX + value.z * cosX};
    value = {value.x * cosY + value.z * sinY, value.y, -value.x * sinY + value.z * cosY};
    return {value.x * cosZ - value.y * sinZ, value.x * sinZ + value.y * cosZ, value.z};
}

[[nodiscard]] renderer::RenderMatrix4 modelMatrix(const scene::Entity& entity, math::Vec3 worldPosition)
{
    renderer::RenderMatrix4 matrix;
    matrix.values.fill(0.0F);
    const auto axisX = rotateEuler({entity.transform.scale.x, 0.0F, 0.0F}, entity.transform.rotationEuler);
    const auto axisY = rotateEuler({0.0F, entity.transform.scale.y, 0.0F}, entity.transform.rotationEuler);
    const auto axisZ = rotateEuler({0.0F, 0.0F, entity.transform.scale.z}, entity.transform.rotationEuler);
    matrixAt(matrix, 0, 0) = axisX.x;
    matrixAt(matrix, 1, 0) = axisX.y;
    matrixAt(matrix, 2, 0) = axisX.z;
    matrixAt(matrix, 0, 1) = axisY.x;
    matrixAt(matrix, 1, 1) = axisY.y;
    matrixAt(matrix, 2, 1) = axisY.z;
    matrixAt(matrix, 0, 2) = axisZ.x;
    matrixAt(matrix, 1, 2) = axisZ.y;
    matrixAt(matrix, 2, 2) = axisZ.z;
    matrixAt(matrix, 0, 3) = worldPosition.x;
    matrixAt(matrix, 1, 3) = worldPosition.y;
    matrixAt(matrix, 2, 3) = worldPosition.z;
    matrixAt(matrix, 3, 3) = 1.0F;
    return matrix;
}

[[nodiscard]] renderer::RenderMatrix4 renderMatrix(const std::array<float, 16>& values)
{
    renderer::RenderMatrix4 matrix;
    matrix.values = values;
    return matrix;
}

[[nodiscard]] renderer::RenderMatrix4 translationMatrix(math::Vec3 offset)
{
    renderer::RenderMatrix4 matrix;
    matrix.values = {
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        offset.x, offset.y, offset.z, 1.0F,
    };
    return matrix;
}

[[nodiscard]] std::optional<math::Vec3> entityWorldPosition(const scene::Scene& scene, scene::EntityId id)
{
    math::Vec3 position {0.0F, 0.0F, 0.0F};
    std::optional<scene::EntityId> currentId = id;
    std::size_t guard = 0;
    while (currentId.has_value() && guard++ < 128U) {
        const auto* entity = scene.findEntity(*currentId);
        if (entity == nullptr) {
            return std::nullopt;
        }
        position = position + entity->transform.position;
        currentId = entity->parent;
    }
    return position;
}

[[nodiscard]] float maxAbsScale(math::Vec3 scale)
{
    return std::max({std::fabs(scale.x), std::fabs(scale.y), std::fabs(scale.z)});
}

[[nodiscard]] math::Vec3 safeNormalized(math::Vec3 value, math::Vec3 fallback)
{
    const auto length = value.length();
    if (length <= 0.00001F || !std::isfinite(length)) {
        return fallback;
    }
    return value / length;
}

[[nodiscard]] renderer::RenderLightType renderLightType(scene::LightComponentType type)
{
    switch (type) {
    case scene::LightComponentType::Directional:
        return renderer::RenderLightType::Directional;
    case scene::LightComponentType::Point:
        return renderer::RenderLightType::Point;
    case scene::LightComponentType::Spot:
        return renderer::RenderLightType::Spot;
    }
    return renderer::RenderLightType::Directional;
}

[[nodiscard]] const assets::TextureAsset* modelTexture(
    const assets::ModelAsset& model,
    std::optional<std::size_t> textureIndex) noexcept
{
    return textureIndex.has_value() && *textureIndex < model.textures.size()
        ? &model.textures[*textureIndex]
        : nullptr;
}

[[nodiscard]] std::uint64_t hashEntitySignature(
    const scene::Entity& entity,
    math::Vec3 worldPosition,
    const assets::ModelAsset* model)
{
    std::uint64_t hash = entity.id.value();
    hash = hashVec3(hash, worldPosition);
    hash = hashVec3(hash, entity.transform.position);
    hash = hashVec3(hash, entity.transform.rotationEuler);
    hash = hashVec3(hash, entity.transform.scale);
    if (entity.meshRenderer.has_value()) {
        hash = mixHash(hash, entity.meshRenderer->modelAssetId.value());
        hash = mixHash(hash, entity.meshRenderer->primitiveInstanceIndex.value_or(UINT32_MAX));
        hash = mixHash(hash, entity.meshRenderer->renderable ? 1U : 0U);
    }
    hash = mixHash(hash, reinterpret_cast<std::uintptr_t>(model));
    if (model != nullptr) {
        hash = mixHash(hash, model->primitives.size());
        hash = mixHash(hash, model->primitiveInstances.size());
        hash = mixHash(hash, model->primitiveClusters.size());
    }
    return hash;
}

} // namespace

ViewportRenderWorld::~ViewportRenderWorld() = default;

void ViewportRenderWorld::markDirty() noexcept
{
    dirty_ = true;
}

std::shared_ptr<ViewportRenderWorld::EntityRecord> ViewportRenderWorld::buildEntityRecord(
    const scene::Scene& scene,
    const assets::IAssetManager& assetManager,
    const scene::Entity& entity,
    const std::unordered_map<std::uint64_t, const scene::Entity*>& primitiveProxyEntities,
    std::uint64_t signature) const
{
    if (!entity.meshRenderer.has_value() || !entity.meshRenderer->renderable) {
        return nullptr;
    }
    const auto model = assetManager.model(entity.meshRenderer->modelAssetId);
    const auto worldPosition = entityWorldPosition(scene, entity.id);
    if (model == nullptr || !worldPosition.has_value()) {
        return nullptr;
    }

    auto record = std::make_shared<EntityRecord>();
    record->entityId = entity.id;
    record->signature = signature;
    record->hasMeshSceneContent = !model->primitives.empty();
    const auto entityModelMatrix = modelMatrix(entity, *worldPosition);

    const auto appendInstance = [&](scene::EntityId sceneNodeId,
                                    std::uint32_t primitiveIndex,
                                    std::uint32_t primitiveInstanceIndex,
                                    const renderer::RenderMatrix4& matrix,
                                    bool flipsWinding) {
        if (primitiveIndex >= model->primitives.size()) {
            return std::optional<std::size_t> {};
        }
        const auto& primitive = model->primitives[primitiveIndex];
        auto instance = EntityRecord::Instance {};
        instance.renderInstanceId = mixHash(entity.id.value(), mixHash(primitiveIndex, primitiveInstanceIndex));
        instance.sceneNodeId = sceneNodeId;
        instance.modelAssetId = model->id;
        instance.model = model;
        instance.primitiveIndex = primitiveIndex;
        instance.primitiveInstanceIndex = primitiveInstanceIndex;
        instance.modelMatrix = matrix;
        instance.worldBounds = transformViewportBounds(matrix, primitive.bounds);
        instance.flipsWinding = flipsWinding;
        record->instances.push_back(instance);
        return std::optional<std::size_t> {record->instances.size() - 1U};
    };

    const auto appendChunk = [&](scene::EntityId sceneNodeId,
                                 std::uint64_t chunkId,
                                 const ViewportWorldBounds& bounds,
                                 std::span<const std::size_t> instanceIndices) {
        if (instanceIndices.empty()) {
            return;
        }
        auto chunk = EntityRecord::Chunk {};
        chunk.sceneNodeId = sceneNodeId;
        chunk.renderChunkId = chunkId;
        chunk.worldBounds = bounds;
        chunk.instanceIndices.assign(instanceIndices.begin(), instanceIndices.end());
        for (const auto instanceIndex : chunk.instanceIndices) {
            if (instanceIndex >= record->instances.size()) {
                continue;
            }
            const auto& instance = record->instances[instanceIndex];
            if (instance.primitiveIndex < model->primitives.size()) {
                chunk.triangleCount += model->primitives[instance.primitiveIndex].indices.size() / 3U;
            }
        }
        record->chunks.push_back(std::move(chunk));
    };

    if (entity.meshRenderer->primitiveInstanceIndex.has_value()
        && *entity.meshRenderer->primitiveInstanceIndex < model->primitiveInstances.size()) {
        const auto index = *entity.meshRenderer->primitiveInstanceIndex;
        const auto& source = model->primitiveInstances[index];
        const auto matrix = multiply(
            multiply(entityModelMatrix, translationMatrix(source.bounds.center * -1.0F)),
            renderMatrix(source.transform));
        if (const auto instanceIndex = appendInstance(entity.id, source.primitiveIndex, index, matrix, source.flipsWinding)) {
            appendChunk(entity.id, mixHash(entity.id.value(), index), record->instances[*instanceIndex].worldBounds, std::span<const std::size_t>(&*instanceIndex, 1U));
        }
        finalizeEntityRecord(*record);
        return record;
    }

    if (!model->primitiveInstances.empty()) {
        std::unordered_set<std::uint32_t> individuallyChunked;
        std::vector<std::size_t> modelInstanceToRecord(model->primitiveInstances.size(), SIZE_MAX);
        for (std::uint32_t index = 0; index < model->primitiveInstances.size(); ++index) {
            const auto& source = model->primitiveInstances[index];
            auto sceneNodeId = entity.id;
            auto matrix = multiply(entityModelMatrix, renderMatrix(source.transform));
            const auto proxyIt = primitiveProxyEntities.find(primitiveProxyKey({model->id.value(), index}));
            if (proxyIt != primitiveProxyEntities.end() && proxyIt->second != nullptr) {
                sceneNodeId = proxyIt->second->id;
                if (!defaultPrimitiveProxyTransform(proxyIt->second->transform, source.bounds.center)) {
                    const auto proxyPosition = entityWorldPosition(scene, proxyIt->second->id);
                    if (proxyPosition.has_value()) {
                        const auto proxyMatrix = modelMatrix(*proxyIt->second, *proxyPosition);
                        matrix = multiply(
                            multiply(proxyMatrix, translationMatrix(source.bounds.center * -1.0F)),
                            renderMatrix(source.transform));
                        individuallyChunked.insert(index);
                    }
                }
            }
            if (const auto instanceIndex = appendInstance(sceneNodeId, source.primitiveIndex, index, matrix, source.flipsWinding)) {
                modelInstanceToRecord[index] = *instanceIndex;
            }
        }

        std::vector<std::size_t> chunkInstances;
        if (!model->primitiveClusters.empty()) {
            for (std::size_t clusterIndex = 0; clusterIndex < model->primitiveClusters.size(); ++clusterIndex) {
                const auto& cluster = model->primitiveClusters[clusterIndex];
                chunkInstances.clear();
                for (const auto sourceIndex : cluster.primitiveInstanceIndices) {
                    if (sourceIndex < modelInstanceToRecord.size()
                        && modelInstanceToRecord[sourceIndex] != SIZE_MAX
                        && individuallyChunked.find(sourceIndex) == individuallyChunked.end()) {
                        chunkInstances.push_back(modelInstanceToRecord[sourceIndex]);
                    }
                }
                const auto bounds = transformViewportBounds(entityModelMatrix, cluster.bounds);
                appendChunk(entity.id, mixHash(entity.id.value(), static_cast<std::uint64_t>(clusterIndex)), bounds, chunkInstances);
            }
        } else {
            for (std::size_t index = 0; index < modelInstanceToRecord.size(); ++index) {
                const auto recordIndex = modelInstanceToRecord[index];
                if (recordIndex == SIZE_MAX) {
                    continue;
                }
                appendChunk(
                    record->instances[recordIndex].sceneNodeId,
                    mixHash(entity.id.value(), static_cast<std::uint64_t>(index)),
                    record->instances[recordIndex].worldBounds,
                    std::span<const std::size_t>(&recordIndex, 1U));
            }
        }
        for (const auto sourceIndex : individuallyChunked) {
            if (sourceIndex >= modelInstanceToRecord.size()) {
                continue;
            }
            const auto recordIndex = modelInstanceToRecord[sourceIndex];
            if (recordIndex == SIZE_MAX) {
                continue;
            }
            appendChunk(
                record->instances[recordIndex].sceneNodeId,
                mixHash(entity.id.value(), mixHash(sourceIndex, 0xf00dU)),
                record->instances[recordIndex].worldBounds,
                std::span<const std::size_t>(&recordIndex, 1U));
        }
        finalizeEntityRecord(*record);
        return record;
    }

    for (std::uint32_t primitiveIndex = 0; primitiveIndex < model->primitives.size(); ++primitiveIndex) {
        if (const auto instanceIndex = appendInstance(entity.id, primitiveIndex, UINT32_MAX, entityModelMatrix, false)) {
            appendChunk(
                entity.id,
                mixHash(entity.id.value(), primitiveIndex),
                record->instances[*instanceIndex].worldBounds,
                std::span<const std::size_t>(&*instanceIndex, 1U));
        }
    }
    finalizeEntityRecord(*record);
    return record;
}

ViewportRenderWorldFrame ViewportRenderWorld::buildFrame(
    const scene::Scene* scene,
    const assets::IAssetManager* assetManager,
    scene::EntityId selectedEntityId,
    const ViewportRenderWorldCamera& camera,
    const renderer::RenderMatrix4& viewProjection,
    int viewportHeight,
    std::vector<renderer::RenderMeshDraw>& meshDraws,
    std::vector<renderer::RenderLight>& lights)
{
    meshDraws.clear();
    lights.clear();
    ViewportRenderWorldFrame result;
    if (scene == nullptr || assetManager == nullptr) {
        records_.clear();
        orderedRecords_.clear();
        overviewRecords_.clear();
        scene_ = scene;
        assetManager_ = assetManager;
        return result;
    }
    result.stats.sceneNodeCount = scene->entityCount();
    bool recordsChanged = false;
    if (scene_ != scene || assetManager_ != assetManager || dirty_) {
        records_.clear();
        orderedRecords_.clear();
        overviewRecords_.clear();
        scene_ = scene;
        assetManager_ = assetManager;
        dirty_ = false;
        recordsChanged = true;
    }

    std::unordered_map<std::uint64_t, const scene::Entity*> primitiveProxyEntities;
    primitiveProxyEntities.reserve(scene->entityCount());
    for (const auto& entity : scene->entities()) {
        if (entity.meshRenderer.has_value()
            && !entity.meshRenderer->renderable
            && entity.meshRenderer->primitiveInstanceIndex.has_value()) {
            primitiveProxyEntities.insert_or_assign(
                primitiveProxyKey({
                    entity.meshRenderer->modelAssetId.value(),
                    *entity.meshRenderer->primitiveInstanceIndex,
                }),
                &entity);
        }
    }

    std::unordered_set<std::uint64_t> visited;
    visited.reserve(scene->entityCount());
    orderedRecords_.clear();
    for (const auto& entity : scene->entities()) {
        const auto worldPosition = entityWorldPosition(*scene, entity.id);
        if (entity.light.has_value() && worldPosition.has_value()) {
            const auto& source = *entity.light;
            const auto direction = safeNormalized(
                rotateEuler(source.direction, entity.transform.rotationEuler),
                {0.35F, -0.82F, 0.45F});
            renderer::RenderLight light;
            light.type = renderLightType(source.type);
            light.position = {worldPosition->x, worldPosition->y, worldPosition->z};
            light.direction = {direction.x, direction.y, direction.z};
            light.color = source.color;
            light.intensity = source.intensity;
            light.range = source.range * maxAbsScale(entity.transform.scale);
            light.innerConeAngle = source.innerConeAngle;
            light.outerConeAngle = source.outerConeAngle;
            if (lights.size() < renderer::kMaxFrameLights) {
                lights.push_back(light);
            }
        }
        if (!entity.meshRenderer.has_value() || !entity.meshRenderer->renderable || !worldPosition.has_value()) {
            continue;
        }
        const auto model = assetManager->model(entity.meshRenderer->modelAssetId);
        auto signature = hashEntitySignature(entity, *worldPosition, model.get());
        for (const auto& [key, proxy] : primitiveProxyEntities) {
            if (proxy == nullptr || !proxy->meshRenderer.has_value()) {
                continue;
            }
            if (proxy->meshRenderer->modelAssetId != entity.meshRenderer->modelAssetId) {
                continue;
            }
            const auto proxyPosition = entityWorldPosition(*scene, proxy->id);
            signature = mixHash(signature, key);
            if (proxyPosition.has_value()) {
                signature = hashVec3(signature, *proxyPosition);
            }
            signature = hashVec3(signature, proxy->transform.position);
            signature = hashVec3(signature, proxy->transform.rotationEuler);
            signature = hashVec3(signature, proxy->transform.scale);
        }

        const auto recordKey = entity.id.value();
        visited.insert(recordKey);
        auto recordIt = records_.find(recordKey);
        if (recordIt == records_.end() || recordIt->second == nullptr || recordIt->second->signature != signature) {
            auto record = buildEntityRecord(*scene, *assetManager, entity, primitiveProxyEntities, signature);
            if (record != nullptr) {
                auto [insertIt, inserted] = records_.insert_or_assign(recordKey, std::move(record));
                orderedRecords_.push_back(insertIt->second.get());
                ++result.stats.rebuiltRecordCount;
                recordsChanged = true;
            }
        } else {
            orderedRecords_.push_back(recordIt->second.get());
            ++result.stats.reusedRecordCount;
        }
    }
    for (auto it = records_.begin(); it != records_.end();) {
        if (visited.find(it->first) == visited.end()) {
            it = records_.erase(it);
            recordsChanged = true;
        } else {
            ++it;
        }
    }
    if (recordsChanged) {
        rebuildOverviewRecords();
    }

    ViewportFrameBounds visibleBounds;
    const auto selectedPrimitiveEntity = selectedEntityId.isValid() ? scene->findEntity(selectedEntityId) : nullptr;
    const auto selectedPrimitiveModel = selectedPrimitiveEntity != nullptr && selectedPrimitiveEntity->meshRenderer.has_value()
            && selectedPrimitiveEntity->meshRenderer->primitiveInstanceIndex.has_value()
        ? selectedPrimitiveEntity->meshRenderer->modelAssetId
        : assets::AssetId {};
    const auto selectedPrimitiveIndex = selectedPrimitiveEntity != nullptr && selectedPrimitiveEntity->meshRenderer.has_value()
            && selectedPrimitiveEntity->meshRenderer->primitiveInstanceIndex.has_value()
        ? *selectedPrimitiveEntity->meshRenderer->primitiveInstanceIndex
        : UINT32_MAX;

    ViewportFrameBounds allChunkBounds;
    for (const auto* record : orderedRecords_) {
        if (record == nullptr) {
            continue;
        }
        result.hasMeshSceneContent = result.hasMeshSceneContent || record->hasMeshSceneContent;
        result.stats.renderInstanceCount += record->instances.size();
        result.stats.renderChunkCount += record->chunks.size();
        result.stats.hlodCandidateDrawCount += record->overviewDraws.size();
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
    for (const auto& overviewRecord : overviewRecords_) {
        if (overviewRecord != nullptr) {
            result.stats.hlodCandidateDrawCount += overviewRecord->overviewDraws.size();
        }
    }

    const auto sceneExtent = allChunkBounds.valid
        ? std::max({
            allChunkBounds.maximum.x - allChunkBounds.minimum.x,
            allChunkBounds.maximum.y - allChunkBounds.minimum.y,
            allChunkBounds.maximum.z - allChunkBounds.minimum.z,
        })
        : 0.0F;
    const auto largeChunkExtent = sceneExtent > 0.0001F ? sceneExtent * 0.35F : std::numeric_limits<float>::infinity();
    const auto collectChunkDebug = result.stats.renderInstanceCount >= 512U || result.stats.renderChunkCount > 32U;
    if (collectChunkDebug) {
        result.debugChunks.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(result.stats.renderChunkCount, 512U)));
    }

    std::vector<ViewportRenderWorldChunkLogRow> chunkDebugRows;
    if (collectChunkDebug) {
        chunkDebugRows.reserve(static_cast<std::size_t>(result.stats.renderChunkCount));
    }

    std::uint64_t visibleSourceTriangleCount = 0;
    std::vector<const EntityRecord::Chunk*> visibleChunks;
    std::unordered_set<std::uint64_t> overviewCoveredModels;

    for (const auto& overviewRecord : overviewRecords_) {
        if (overviewRecord == nullptr) {
            continue;
        }
        if (tryEmitOverviewRecord(
                *overviewRecord,
                selectedPrimitiveModel,
                selectedPrimitiveIndex,
                camera,
                viewProjection,
                true,
                meshDraws,
                result.stats,
                visibleBounds,
                visibleSourceTriangleCount)) {
            overviewCoveredModels.insert(overviewRecord->coveredModelAssetId.value());
        }
    }

    for (const auto* record : orderedRecords_) {
        if (record == nullptr) {
            continue;
        }
        if (!record->instances.empty()
            && overviewCoveredModels.find(record->instances.front().modelAssetId.value()) != overviewCoveredModels.end()) {
            continue;
        }
        visibleChunks.clear();
        visibleChunks.reserve(record->chunks.size());
        for (const auto& chunk : record->chunks) {
            const auto chunkExtent = renderWorldChunkMaxExtent(chunk.worldBounds.corners);
            const auto largeChunk = chunkExtent >= largeChunkExtent
                || chunk.triangleCount == result.stats.largestRenderChunkTriangleCount;
            if (largeChunk) {
                ++result.stats.largeRenderChunkCount;
            }
            const auto chunkVisible = viewportBoundsVisible(
                    chunk.worldBounds,
                    camera.eye,
                    camera.right,
                    camera.up,
                    camera.forward,
                    camera.verticalFovRadians,
                    camera.aspectRatio,
                    camera.nearPlane,
                    camera.farPlane);
            if (collectChunkDebug) {
                chunkDebugRows.push_back({
                    chunk.renderChunkId,
                    chunk.triangleCount,
                    static_cast<std::uint64_t>(chunk.instanceIndices.size()),
                    chunkExtent,
                    chunkVisible,
                });
                if (result.debugChunks.size() < 512U) {
                    result.debugChunks.push_back({
                        chunk.worldBounds.corners,
                        chunk.triangleCount,
                        static_cast<std::uint64_t>(chunk.instanceIndices.size()),
                        chunkExtent,
                        chunkVisible,
                        largeChunk,
                    });
                }
            }
            if (!chunkVisible) {
                continue;
            }
            ++result.stats.visibleRenderChunkCount;
            visibleChunks.push_back(&chunk);
        }

        if (visibleChunks.empty()) {
            continue;
        }

        if (tryEmitOverviewRecord(
                *record,
                selectedPrimitiveModel,
                selectedPrimitiveIndex,
                camera,
                viewProjection,
                false,
                meshDraws,
                result.stats,
                visibleBounds,
                visibleSourceTriangleCount)) {
            continue;
        }

        for (const auto* visibleChunk : visibleChunks) {
            const auto& chunk = *visibleChunk;
            for (const auto instanceIndex : chunk.instanceIndices) {
                if (instanceIndex >= record->instances.size()) {
                    continue;
                }
                const auto& instance = record->instances[instanceIndex];
                if (instance.primitiveIndex >= instance.model->primitives.size()) {
                    continue;
                }
                const auto& primitive = instance.model->primitives[instance.primitiveIndex];
                if (primitive.materialIndex >= instance.model->materials.size()) {
                    continue;
                }
                if (!viewportBoundsVisible(
                        instance.worldBounds,
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
                ++result.stats.visibleRenderInstanceCount;
                visibleBounds.includeSphere(instance.worldBounds.center, instance.worldBounds.radius);
                const auto& material = instance.model->materials[primitive.materialIndex];
                const auto sortDepth = math::dot(instance.worldBounds.center - camera.eye, camera.forward);
                const auto sourceTriangleCount = static_cast<std::uint64_t>(primitive.indices.size() / 3U);
                visibleSourceTriangleCount += sourceTriangleCount;
                const auto forceFullResolution = instance.sceneNodeId == selectedEntityId
                    || (selectedPrimitiveModel.isValid()
                        && selectedPrimitiveModel == instance.modelAssetId
                        && selectedPrimitiveIndex == instance.primitiveInstanceIndex);
                const auto lodIndex = selectViewportMeshLod(
                    primitive,
                    instance.worldBounds.radius,
                    sortDepth,
                    camera.verticalFovRadians,
                    static_cast<float>(std::max(viewportHeight, 1)),
                    forceFullResolution);
                const auto selectedTriangleCount = static_cast<std::uint64_t>(indexCountForViewportLod(primitive, lodIndex) / 3U);
                if (lodIndex > 0U && selectedTriangleCount < sourceTriangleCount) {
                    ++result.stats.lodMeshDrawCount;
                    result.stats.lodTriangleReductionCount += sourceTriangleCount - selectedTriangleCount;
                }
                meshDraws.push_back({
                    instance.modelAssetId,
                    instance.primitiveIndex,
                    lodIndex,
                    &primitive,
                    &material,
                    modelTexture(*instance.model, material.baseColorTexture),
                    modelTexture(*instance.model, material.normalTexture),
                    modelTexture(*instance.model, material.metallicRoughnessTexture),
                    modelTexture(*instance.model, material.occlusionTexture),
                    modelTexture(*instance.model, material.emissiveTexture),
                    sortDepth,
                    {instance.worldBounds.center.x, instance.worldBounds.center.y, instance.worldBounds.center.z},
                    instance.worldBounds.radius,
                    instance.modelMatrix,
                    multiply(viewProjection, instance.modelMatrix),
                    instance.flipsWinding,
                    true,
                    instance.renderInstanceId,
                    chunk.renderChunkId,
                    instance.sceneNodeId.value(),
                });
            }
        }
    }

    logRenderWorldChunkDiagnostics(result.stats, chunkDebugRows, lastDebugSignature_);

    if (visibleBounds.valid) {
        result.visibleBoundsValid = true;
        result.visibleBoundsCenter = visibleBounds.center();
        result.visibleBoundsRadius = visibleBounds.radius();
    }
    result.stats.culledMeshDrawCount = result.stats.candidateMeshDrawCount > result.stats.visibleRenderInstanceCount
        ? result.stats.candidateMeshDrawCount - result.stats.visibleRenderInstanceCount
        : 0U;
    result.stats.culledTriangleCount = result.stats.candidateTriangleCount > visibleSourceTriangleCount
        ? result.stats.candidateTriangleCount - visibleSourceTriangleCount
        : 0U;
    applyViewportShadowPolicy(meshDraws, selectedEntityId, camera, viewportHeight);
    return result;
}

} // namespace projectunity::editor
