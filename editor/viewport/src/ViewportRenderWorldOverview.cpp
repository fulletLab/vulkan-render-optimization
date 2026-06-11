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

constexpr bool kOverviewHlodEnabled = true;
constexpr std::uint64_t kOverviewMinSourceTriangles = 64'000ULL;
constexpr std::uint64_t kOverviewMaxSourceTriangles = 8'000'000ULL;
constexpr std::uint64_t kOverviewMaxTriangles = 2'000'000ULL;
constexpr std::uint64_t kOverviewMinVisibleInstanceReferences = 512ULL;
constexpr std::uint64_t kOverviewMinVisibleTriangles = 250'000ULL;
constexpr std::uint32_t kOverviewPrimitiveIndexBase = 0x80000000U;

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
    return coarsestLodIndices(primitive);
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
    math::Vec3 worldBoundsCenter,
    float worldBoundsRadius,
    const ViewportRenderWorldCamera& camera,
    int viewportHeight,
    std::uint64_t visibleInstanceReferences,
    std::uint64_t visibleTriangles) noexcept
{
    if (!worldBoundsValid || viewportHeight <= 0) {
        return false;
    }
    const auto depth = math::dot(worldBoundsCenter - camera.eye, camera.forward);
    if (!std::isfinite(depth) || depth <= camera.nearPlane) {
        return false;
    }
    const auto projectedRadius = projectedRadiusPixels(
        worldBoundsRadius,
        depth,
        camera.verticalFovRadians,
        viewportHeight);
    if (projectedRadius <= 0.0F) {
        return false;
    }
    const auto heavyDecorativeCluster = visibleInstanceReferences >= 4096ULL
        && visibleTriangles <= kOverviewMaxSourceTriangles;
    const auto maxProjectedRadius = heavyDecorativeCluster
        ? static_cast<float>(viewportHeight) * 1.75F
        : static_cast<float>(viewportHeight) * 0.90F;
    return projectedRadius <= maxProjectedRadius;
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

[[nodiscard]] const std::vector<CachedOverviewDraw>& cachedOverviewDrawsForModel(const assets::ModelAsset& model)
{
    static const std::vector<CachedOverviewDraw> kEmpty;
    static std::unordered_map<std::uint64_t, std::vector<CachedOverviewDraw>> cache;

    const auto cacheKey = model.id.value();
    if (const auto existing = cache.find(cacheKey); existing != cache.end()) {
        return existing->second;
    }

    std::vector<CachedOverviewDraw> cached;
    if (model.primitiveInstances.empty()) {
        auto [inserted, _] = cache.emplace(cacheKey, std::move(cached));
        return inserted->second;
    }

    std::uint64_t sourceTriangleCount = 0;
    for (const auto& instance : model.primitiveInstances) {
        if (instance.primitiveIndex >= model.primitives.size()) {
            continue;
        }
        sourceTriangleCount += model.primitives[instance.primitiveIndex].indices.size() / 3U;
    }
    if (sourceTriangleCount < kOverviewMinSourceTriangles || sourceTriangleCount > kOverviewMaxSourceTriangles) {
        auto [inserted, _] = cache.emplace(cacheKey, std::move(cached));
        return inserted->second;
    }

    std::uint64_t overviewCandidateTriangles = 0;
    for (const auto& instance : model.primitiveInstances) {
        if (instance.primitiveIndex >= model.primitives.size()) {
            continue;
        }
        const auto& source = model.primitives[instance.primitiveIndex];
        if (source.materialIndex >= model.materials.size()) {
            continue;
        }
        const auto* indices = overviewSourceIndices(source);
        if (indices == nullptr || indices->size() < 3U) {
            continue;
        }
        overviewCandidateTriangles += indices->size() / 3U;
    }
    if (overviewCandidateTriangles == 0U || overviewCandidateTriangles > kOverviewMaxTriangles) {
        auto [inserted, _] = cache.emplace(cacheKey, std::move(cached));
        return inserted->second;
    }

    std::vector<MaterialOverview> overviews;
    overviews.reserve(32U);
    std::uint64_t overviewSourceTriangles = 0;
    for (const auto& instance : model.primitiveInstances) {
        if (instance.primitiveIndex >= model.primitives.size()) {
            continue;
        }
        const auto& source = model.primitives[instance.primitiveIndex];
        if (source.materialIndex >= model.materials.size()) {
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
        if (overview->primitive.vertices.size() + source.vertices.size() > std::numeric_limits<std::uint32_t>::max()) {
            continue;
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
            if (triangle[0] >= source.vertices.size()
                || triangle[1] >= source.vertices.size()
                || triangle[2] >= source.vertices.size()) {
                continue;
            }
            overview->primitive.indices.push_back(vertexBase + triangle[0]);
            overview->primitive.indices.push_back(vertexBase + triangle[1]);
            overview->primitive.indices.push_back(vertexBase + triangle[2]);
        }
    }

    if (overviewSourceTriangles < sourceTriangleCount / 2U) {
        auto [inserted, _] = cache.emplace(cacheKey, std::move(cached));
        return inserted->second;
    }

    const auto overviewCanReduceDrawWork = overviews.size() < model.primitiveInstances.size()
        && model.primitiveInstances.size() >= kOverviewMinVisibleInstanceReferences;
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
            mixHash(model.id.value(), mixHash(0x4810d00dULL, static_cast<std::uint64_t>(cached.size())))
            | 0x8000000000000000ULL);
        draw.primitiveIndex = kOverviewPrimitiveIndexBase + static_cast<std::uint32_t>(cached.size());
        draw.sourceTriangleCount = overview.sourceTriangleCount;
        draw.primitive = std::make_shared<const assets::MeshPrimitive>(std::move(overview.primitive));
        cached.push_back(std::move(draw));
    }

    auto [inserted, _] = cache.emplace(cacheKey, std::move(cached));
    return inserted->second.empty() ? kEmpty : inserted->second;
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
    const auto& cachedOverviews = cachedOverviewDrawsForModel(*record.instances.front().model);
    record.overviewDraws.reserve(cachedOverviews.size());
    for (const auto& cached : cachedOverviews) {
        if (cached.primitive == nullptr) {
            continue;
        }
        EntityRecord::OverviewDraw draw;
        draw.renderInstanceId = mixHash(record.entityId.value(), cached.primitiveIndex);
        draw.modelAssetId = cached.modelAssetId;
        draw.primitiveIndex = cached.primitiveIndex;
        draw.primitive = cached.primitive;
        draw.modelMatrix = record.modelMatrix;
        draw.worldBounds = transformViewportBounds(record.modelMatrix, cached.primitive->bounds);
        draw.sourceTriangleCount = cached.sourceTriangleCount;
        record.overviewDraws.push_back(std::move(draw));
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
    bool countVisibleChunks,
    std::vector<renderer::RenderMeshDraw>& meshDraws,
    ViewportRenderWorldStats& stats,
    ViewportFrameBounds& visibleBounds,
    std::uint64_t& visibleSourceTriangleCount) const
{
    if (!kOverviewHlodEnabled) {
        return false;
    }

    std::uint64_t visibleChunkCount = 0;
    std::uint64_t visibleChunkInstanceReferences = 0;
    std::uint64_t visibleChunkTriangles = 0;
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
        visibleChunkTriangles += chunk.triangleCount;
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
    const auto enoughVisibleWork = visibleChunkInstanceReferences >= kOverviewMinVisibleInstanceReferences
        || visibleChunkTriangles >= kOverviewMinVisibleTriangles;
    const auto overviewCoverageEnough = (visibleChunkRatio >= 0.55F || visibleInstanceRatio >= 0.55F)
        && enoughVisibleWork;
    const auto wantsOverview = !record.overviewDraws.empty()
        && overviewCoverageEnough
        && overviewScreenEligible(
            record.worldBoundsValid,
            record.worldBounds.center,
            record.worldBounds.radius,
            camera,
            viewportHeight,
            visibleChunkInstanceReferences,
            visibleChunkTriangles);
    if (!wantsOverview) {
        return false;
    }
    bool emittedOverview = false;
    const auto model = record.instances.empty() ? std::shared_ptr<const assets::ModelAsset> {} : record.instances.front().model;
    if (model == nullptr) {
        return false;
    }
    for (const auto& overview : record.overviewDraws) {
        if (overview.primitive == nullptr || overview.primitive->materialIndex >= model->materials.size()) {
            continue;
        }
        const auto& material = model->materials[overview.primitive->materialIndex];
        const auto sortDepth = math::dot(overview.worldBounds.center - camera.eye, camera.forward);
        const auto sourceTriangleCount = overview.sourceTriangleCount;
        const auto selectedTriangleCount = static_cast<std::uint64_t>(overview.primitive->indices.size() / 3U);
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
            mixHash(record.entityId.value(), 0x4810d00dULL),
            record.entityId.value(),
        });
        emittedOverview = true;
    }
    return emittedOverview;
}

} // namespace projectunity::editor
