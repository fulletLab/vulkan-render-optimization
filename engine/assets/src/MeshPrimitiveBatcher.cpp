#include "MeshPrimitiveBatcher.hpp"

#include "GltfNodeTransforms.hpp"
#include "MeshLodGenerator.hpp"
#include "MeshBounds.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace projectunity::assets::detail {
namespace {

constexpr std::size_t kBatchThreshold = 512;
constexpr std::size_t kSpatialBatchTargetInstances = 8;
constexpr std::size_t kMaxSpatialBatchGridSide = 64;
constexpr float kSpatialBatchTargetCellExtent = 12.0F;
constexpr std::size_t kLegacySpatialBatchTargetInstances = 16;
constexpr std::size_t kLegacyMaxSpatialBatchGridSide = 32;
constexpr std::size_t kLegacyMaxSpatialBatchTargets = 2048;

enum class BatchGridMode {
    Legacy,
    Physical,
    Adaptive,
};

struct BatchTarget {
    std::size_t materialIndex {0};
    std::size_t cellX {0};
    std::size_t cellY {0};
    std::uint32_t primitiveIndex {0};
    std::size_t instanceCount {0};
};

struct BatchTargetKey {
    std::size_t materialIndex {0};
    std::size_t cellX {0};
    std::size_t cellY {0};

    [[nodiscard]] bool operator==(const BatchTargetKey&) const noexcept = default;
};

struct BatchTargetKeyHash {
    [[nodiscard]] std::size_t operator()(const BatchTargetKey& value) const noexcept
    {
        auto result = value.materialIndex + 0x9e3779b97f4a7c15ULL;
        result ^= value.cellX + 0x9e3779b97f4a7c15ULL + (result << 6U) + (result >> 2U);
        result ^= value.cellY + 0x9e3779b97f4a7c15ULL + (result << 6U) + (result >> 2U);
        return result;
    }
};

struct SpatialBatchGrid {
    std::size_t side {1};
    int axisA {0};
    int axisB {2};
    float minA {0.0F};
    float minB {0.0F};
    float extentA {0.0F};
    float extentB {0.0F};
};

[[nodiscard]] GltfMatrix4 toMatrix(const std::array<float, 16>& values)
{
    GltfMatrix4 result {};
    std::transform(values.begin(), values.end(), result.begin(), [](float value) {
        return static_cast<double>(value);
    });
    return result;
}

[[nodiscard]] MeshPrimitiveInstance identityInstance(std::uint32_t primitiveIndex, const MeshBounds& bounds)
{
    MeshPrimitiveInstance instance;
    instance.primitiveIndex = primitiveIndex;
    instance.bounds = bounds;
    return instance;
}

[[nodiscard]] float component(math::Vec3 value, int axis) noexcept
{
    if (axis == 0) {
        return value.x;
    }
    if (axis == 1) {
        return value.y;
    }
    return value.z;
}

void includeBounds(MeshBounds& bounds, const MeshBounds& next, bool& initialized) noexcept
{
    if (!initialized) {
        bounds.minimum = next.minimum;
        bounds.maximum = next.maximum;
        initialized = true;
        return;
    }
    bounds.minimum.x = std::min(bounds.minimum.x, next.minimum.x);
    bounds.minimum.y = std::min(bounds.minimum.y, next.minimum.y);
    bounds.minimum.z = std::min(bounds.minimum.z, next.minimum.z);
    bounds.maximum.x = std::max(bounds.maximum.x, next.maximum.x);
    bounds.maximum.y = std::max(bounds.maximum.y, next.maximum.y);
    bounds.maximum.z = std::max(bounds.maximum.z, next.maximum.z);
}

[[nodiscard]] std::array<int, 2> spatialAxes(const MeshBounds& bounds) noexcept
{
    std::array<std::pair<float, int>, 3> extents {{
        {std::fabs(bounds.maximum.x - bounds.minimum.x), 0},
        {std::fabs(bounds.maximum.y - bounds.minimum.y), 1},
        {std::fabs(bounds.maximum.z - bounds.minimum.z), 2},
    }};
    std::sort(extents.begin(), extents.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first > rhs.first;
    });
    return {extents[0].second, extents[1].second};
}

[[nodiscard]] std::size_t instanceGridSide(std::size_t instanceCount, std::size_t targetInstances) noexcept
{
    const auto wantedCells = std::max<std::size_t>(
        1U,
        (instanceCount + targetInstances - 1U) / targetInstances);
    return static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<float>(wantedCells))));
}

[[nodiscard]] std::size_t materialBudgetGridSide(std::size_t materialCount) noexcept
{
    const auto safeMaterialCount = std::max<std::size_t>(materialCount, 1U);
    const auto targetCells = std::max<std::size_t>(1U, kLegacyMaxSpatialBatchTargets / safeMaterialCount);
    return static_cast<std::size_t>(std::floor(std::sqrt(static_cast<float>(targetCells))));
}

[[nodiscard]] std::size_t physicalGridSide(const MeshBounds& bounds) noexcept
{
    const auto axes = spatialAxes(bounds);
    const auto extentA = std::fabs(component(bounds.maximum, axes[0]) - component(bounds.minimum, axes[0]));
    const auto extentB = std::fabs(component(bounds.maximum, axes[1]) - component(bounds.minimum, axes[1]));
    const auto largestExtent = std::max(extentA, extentB);
    if (!std::isfinite(largestExtent) || largestExtent <= kSpatialBatchTargetCellExtent) {
        return 1U;
    }
    return static_cast<std::size_t>(std::ceil(largestExtent / kSpatialBatchTargetCellExtent));
}

[[nodiscard]] std::size_t gridCoordinate(float value, float minimum, float extent, std::size_t side) noexcept
{
    if (side <= 1U || extent <= 0.0001F || !std::isfinite(value)) {
        return 0;
    }
    const auto normalized = std::clamp((value - minimum) / extent, 0.0F, 0.999999F);
    return std::min(static_cast<std::size_t>(normalized * static_cast<float>(side)), side - 1U);
}

[[nodiscard]] bool modelBounds(const ModelAsset& model, MeshBounds& bounds) noexcept
{
    bool initialized = false;
    for (const auto& instance : model.primitiveInstances) {
        includeBounds(bounds, instance.bounds, initialized);
    }
    if (initialized) {
        bounds.center = (bounds.minimum + bounds.maximum) * 0.5F;
        bounds.radius = (bounds.maximum - bounds.center).length();
    }
    return initialized;
}

[[nodiscard]] SpatialBatchGrid makeSpatialBatchGrid(const MeshBounds& sceneBounds, std::size_t side) noexcept
{
    SpatialBatchGrid grid;
    grid.side = std::max<std::size_t>(side, 1U);
    if (grid.side <= 1U) {
        return grid;
    }

    const auto axes = spatialAxes(sceneBounds);
    grid.axisA = axes[0];
    grid.axisB = axes[1];
    grid.minA = component(sceneBounds.minimum, grid.axisA);
    grid.minB = component(sceneBounds.minimum, grid.axisB);
    grid.extentA = component(sceneBounds.maximum, grid.axisA) - grid.minA;
    grid.extentB = component(sceneBounds.maximum, grid.axisB) - grid.minB;
    if (std::fabs(grid.extentA) <= 0.0001F && std::fabs(grid.extentB) <= 0.0001F) {
        grid.side = 1U;
    }
    return grid;
}

[[nodiscard]] std::pair<std::size_t, std::size_t> spatialCell(
    const SpatialBatchGrid& grid,
    math::Vec3 center) noexcept
{
    return {
        gridCoordinate(component(center, grid.axisA), grid.minA, grid.extentA, grid.side),
        gridCoordinate(component(center, grid.axisB), grid.minB, grid.extentB, grid.side),
    };
}

[[nodiscard]] std::size_t countBatchTargets(const ModelAsset& model, const SpatialBatchGrid& grid)
{
    std::unordered_set<BatchTargetKey, BatchTargetKeyHash> targets;
    targets.reserve(model.primitiveInstances.size());
    for (const auto& instance : model.primitiveInstances) {
        if (instance.primitiveIndex >= model.primitives.size()) {
            continue;
        }
        const auto& source = model.primitives[instance.primitiveIndex];
        const auto [cellX, cellY] = spatialCell(grid, instance.bounds.center);
        targets.insert({source.materialIndex, cellX, cellY});
    }
    return targets.size();
}

[[nodiscard]] BatchGridMode requestedBatchGridMode() noexcept
{
#if defined(_MSC_VER)
    char* rawValue = nullptr;
    std::size_t valueLength = 0;
    if (_dupenv_s(&rawValue, &valueLength, "PROJECTUNITY_BATCH_GRID_MODE") != 0 || rawValue == nullptr) {
        return BatchGridMode::Legacy;
    }
    const std::string modeValue(rawValue);
    std::free(rawValue);
    const std::string_view mode(modeValue);
#else
    const auto* value = std::getenv("PROJECTUNITY_BATCH_GRID_MODE");
    if (value == nullptr) {
        return BatchGridMode::Legacy;
    }
    const std::string_view mode(value);
#endif
    if (mode == "legacy" || mode == "legacy_2x2_baseline") {
        return BatchGridMode::Legacy;
    }
    if (mode == "physical" || mode == "current_physical_grid") {
        return BatchGridMode::Physical;
    }
    return BatchGridMode::Adaptive;
}

[[nodiscard]] bool batchGridModeOverrideEnabled() noexcept
{
#if defined(_MSC_VER)
    char* rawValue = nullptr;
    std::size_t valueLength = 0;
    if (_dupenv_s(&rawValue, &valueLength, "PROJECTUNITY_BATCH_GRID_MODE") != 0 || rawValue == nullptr) {
        return false;
    }
    std::free(rawValue);
    return valueLength > 0U;
#else
    return std::getenv("PROJECTUNITY_BATCH_GRID_MODE") != nullptr;
#endif
}

[[nodiscard]] const char* batchGridModeName(BatchGridMode mode) noexcept
{
    switch (mode) {
    case BatchGridMode::Legacy:
        return "legacy_2x2_baseline";
    case BatchGridMode::Physical:
        return "current_physical_grid";
    case BatchGridMode::Adaptive:
        return "adaptive_grid";
    }
    return "adaptive_grid";
}

struct SpatialBatchSelection {
    SpatialBatchGrid grid;
    std::size_t candidateSide {1};
    std::size_t legacySide {1};
    std::size_t candidateBatchCount {0};
    std::size_t legacyBatchCount {0};
    std::size_t finalBatchCount {0};
    BatchGridMode mode {BatchGridMode::Legacy};
};

[[nodiscard]] SpatialBatchSelection selectSpatialBatchGrid(const ModelAsset& model)
{
    SpatialBatchSelection result;
    result.mode = requestedBatchGridMode();
    MeshBounds sceneBounds;
    if (!modelBounds(model, sceneBounds)) {
        result.grid.side = 1U;
        return result;
    }
    const auto instanceSide = instanceGridSide(model.primitiveInstances.size(), kSpatialBatchTargetInstances);
    const auto extentSide = physicalGridSide(sceneBounds);
    result.candidateSide = std::clamp(
        std::max(instanceSide, extentSide),
        std::size_t {1U},
        kMaxSpatialBatchGridSide);
    result.legacySide = std::clamp(
        std::min(
            instanceGridSide(model.primitiveInstances.size(), kLegacySpatialBatchTargetInstances),
            materialBudgetGridSide(model.materials.size())),
        std::size_t {1U},
        kLegacyMaxSpatialBatchGridSide);

    const auto candidateGrid = makeSpatialBatchGrid(sceneBounds, result.candidateSide);
    const auto legacyGrid = makeSpatialBatchGrid(sceneBounds, result.legacySide);
    result.candidateBatchCount = countBatchTargets(model, candidateGrid);
    result.legacyBatchCount = countBatchTargets(model, legacyGrid);

    if (result.mode == BatchGridMode::Legacy) {
        result.grid = legacyGrid;
        result.finalBatchCount = result.legacyBatchCount;
        return result;
    }
    if (result.mode == BatchGridMode::Physical || result.candidateBatchCount < result.legacyBatchCount) {
        result.grid = candidateGrid;
        result.finalBatchCount = result.candidateBatchCount;
        return result;
    }

    result.grid = legacyGrid;
    result.finalBatchCount = result.legacyBatchCount;
    for (auto side = result.candidateSide; side > result.legacySide; --side) {
        auto grid = makeSpatialBatchGrid(sceneBounds, side);
        const auto batchCount = countBatchTargets(model, grid);
        if (batchCount < result.legacyBatchCount) {
            result.grid = grid;
            result.finalBatchCount = batchCount;
            break;
        }
    }
    return result;
}

[[nodiscard]] bool sameMaterial(const MaterialAsset& lhs, const MaterialAsset& rhs) noexcept
{
    return lhs.baseColor == rhs.baseColor
        && lhs.emissiveColor == rhs.emissiveColor
        && lhs.metallicFactor == rhs.metallicFactor
        && lhs.roughnessFactor == rhs.roughnessFactor
        && lhs.normalScale == rhs.normalScale
        && lhs.occlusionStrength == rhs.occlusionStrength
        && lhs.alphaMode == rhs.alphaMode
        && lhs.alphaCutoff == rhs.alphaCutoff
        && lhs.doubleSided == rhs.doubleSided
        && lhs.baseColorTexture == rhs.baseColorTexture
        && lhs.normalTexture == rhs.normalTexture
        && lhs.metallicRoughnessTexture == rhs.metallicRoughnessTexture
        && lhs.occlusionTexture == rhs.occlusionTexture
        && lhs.emissiveTexture == rhs.emissiveTexture;
}
[[nodiscard]] bool sameTexture(const TextureAsset& lhs, const TextureAsset& rhs) noexcept
{
    return lhs.width == rhs.width
        && lhs.height == rhs.height
        && lhs.gpuFormat == rhs.gpuFormat
        && lhs.sampler == rhs.sampler
        && lhs.rgba8 == rhs.rgba8
        && lhs.rgba32f == rhs.rgba32f
        && lhs.gpuMipLevels == rhs.gpuMipLevels;
}
void remapTexture(std::optional<std::size_t>& index, const std::vector<std::size_t>& remap)
{
    if (index.has_value() && *index < remap.size()) {
        index = remap[*index];
    }
}
void deduplicateTextures(ModelAsset& model)
{
    std::vector<TextureAsset> unique;
    std::vector<std::size_t> remap(model.textures.size(), 0U);
    for (std::size_t index = 0; index < model.textures.size(); ++index) {
        const auto existing = std::find_if(unique.begin(), unique.end(), [&model, index](const TextureAsset& texture) {
            return sameTexture(texture, model.textures[index]);
        });
        if (existing == unique.end()) {
            remap[index] = unique.size();
            unique.push_back(model.textures[index]);
        } else {
            remap[index] = static_cast<std::size_t>(std::distance(unique.begin(), existing));
        }
    }
    for (auto& material : model.materials) {
        remapTexture(material.baseColorTexture, remap);
        remapTexture(material.normalTexture, remap);
        remapTexture(material.metallicRoughnessTexture, remap);
        remapTexture(material.occlusionTexture, remap);
        remapTexture(material.emissiveTexture, remap);
    }
    model.textures = std::move(unique);
}

void bakeFlatBaseColors(ModelAsset& model)
{
    for (auto& primitive : model.primitives) {
        if (primitive.materialIndex >= model.materials.size()) {
            continue;
        }
        const auto& material = model.materials[primitive.materialIndex];
        for (auto& vertex : primitive.vertices) {
            vertex.color[0] *= material.baseColor[0];
            vertex.color[1] *= material.baseColor[1];
            vertex.color[2] *= material.baseColor[2];
            vertex.color[3] *= material.baseColor[3];
            vertex.materialFactors[0] *= material.metallicFactor;
            vertex.materialFactors[1] *= material.roughnessFactor;
            vertex.materialFactors[2] *= material.normalScale;
            vertex.materialFactors[3] *= material.occlusionStrength;
        }
    }
    for (auto& material : model.materials) {
        material.baseColor = {1.0F, 1.0F, 1.0F, 1.0F};
        material.metallicFactor = 1.0F;
        material.roughnessFactor = 1.0F;
        material.normalScale = 1.0F;
        material.occlusionStrength = 1.0F;
    }
}

void deduplicateMaterials(ModelAsset& model)
{
    std::vector<MaterialAsset> unique;
    std::vector<std::size_t> remap(model.materials.size(), 0U);
    for (std::size_t index = 0; index < model.materials.size(); ++index) {
        const auto existing = std::find_if(unique.begin(), unique.end(), [&model, index](const MaterialAsset& material) {
            return sameMaterial(material, model.materials[index]);
        });
        if (existing == unique.end()) {
            remap[index] = unique.size();
            unique.push_back(model.materials[index]);
        } else {
            remap[index] = static_cast<std::size_t>(std::distance(unique.begin(), existing));
        }
    }
    for (auto& primitive : model.primitives) {
        if (primitive.materialIndex < remap.size()) {
            primitive.materialIndex = remap[primitive.materialIndex];
        }
    }
    model.materials = std::move(unique);
}

} // namespace

bool meshPrimitiveBatchComparisonModeEnabled() noexcept
{
    return batchGridModeOverrideEnabled();
}

MeshPrimitiveBatchStats batchModelPrimitives(ModelAsset& model)
{
    MeshPrimitiveBatchStats stats;
    stats.sourcePrimitiveCount = model.primitives.size();
    stats.sourceInstanceCount = model.primitiveInstances.size();
    deduplicateTextures(model);
    if (model.primitives.size() <= kBatchThreshold || model.primitiveInstances.size() <= kBatchThreshold) {
        deduplicateMaterials(model);
        stats.mode = batchGridModeName(requestedBatchGridMode());
        stats.materialCount = model.materials.size();
        stats.outputPrimitiveCount = model.primitives.size();
        stats.outputInstanceCount = model.primitiveInstances.size();
        stats.bypassed = true;
        return stats;
    }
    bakeFlatBaseColors(model);
    deduplicateMaterials(model);
    stats.materialCount = model.materials.size();

    const auto selection = selectSpatialBatchGrid(model);
    const auto& spatialGrid = selection.grid;
    stats.mode = batchGridModeName(selection.mode);
    stats.candidateGridSide = selection.candidateSide;
    stats.legacyGridSide = selection.legacySide;
    stats.finalGridSide = spatialGrid.side;
    stats.candidateBatchCount = selection.candidateBatchCount;
    stats.legacyBatchCount = selection.legacyBatchCount;
    stats.finalBatchCount = selection.finalBatchCount;
    std::vector<MeshPrimitive> batched;
    std::vector<BatchTarget> targets;
    for (const auto& instance : model.primitiveInstances) {
        if (instance.primitiveIndex >= model.primitives.size()) {
            continue;
        }
        const auto& source = model.primitives[instance.primitiveIndex];
        const auto [cellX, cellY] = spatialCell(spatialGrid, instance.bounds.center);
        auto target = std::find_if(targets.begin(), targets.end(), [&source, cellX, cellY](const BatchTarget& value) {
            return value.materialIndex == source.materialIndex
                && value.cellX == cellX
                && value.cellY == cellY;
        });
        if (target == targets.end()) {
            const auto outputIndex = static_cast<std::uint32_t>(batched.size());
            MeshPrimitive next;
            next.materialIndex = source.materialIndex;
            batched.push_back(std::move(next));
            targets.push_back({source.materialIndex, cellX, cellY, outputIndex, 0U});
            target = targets.end() - 1;
        }
        ++target->instanceCount;

        auto transformed = source;
        transformed.lods.clear();
        applyGltfTransform(transformed, toMatrix(instance.transform));
        auto& destination = batched[target->primitiveIndex];
        if (destination.vertices.size() + transformed.vertices.size() > std::numeric_limits<std::uint32_t>::max()) {
            return stats;
        }
        const auto vertexOffset = static_cast<std::uint32_t>(destination.vertices.size());
        destination.vertices.insert(destination.vertices.end(), transformed.vertices.begin(), transformed.vertices.end());
        destination.indices.reserve(destination.indices.size() + transformed.indices.size());
        for (const auto index : transformed.indices) {
            destination.indices.push_back(vertexOffset + index);
        }
    }

    std::vector<MeshPrimitiveInstance> instances;
    instances.reserve(batched.size());
    for (std::uint32_t index = 0; index < batched.size(); ++index) {
        updateMeshBounds(batched[index]);
        rebuildSimplificationLods(batched[index]);
        const auto extent = batched[index].bounds.maximum - batched[index].bounds.minimum;
        stats.largestBatchExtent = std::max({stats.largestBatchExtent, extent.x, extent.y, extent.z});
        stats.largestBatchTriangles = std::max(stats.largestBatchTriangles, batched[index].indices.size() / 3U);
        instances.push_back(identityInstance(index, batched[index].bounds));
    }
    for (const auto& target : targets) {
        stats.largestBatchInstances = std::max(stats.largestBatchInstances, target.instanceCount);
    }
    if (!targets.empty()) {
        stats.averageInstancesPerBatch = static_cast<float>(model.primitiveInstances.size())
            / static_cast<float>(targets.size());
    }
    model.primitives = std::move(batched);
    model.primitiveInstances = std::move(instances);
    stats.finalBatchCount = model.primitives.size();
    stats.outputPrimitiveCount = model.primitives.size();
    stats.outputInstanceCount = model.primitiveInstances.size();
    return stats;
}

} // namespace projectunity::assets::detail
