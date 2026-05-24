#include "MeshPrimitiveBatcher.hpp"

#include "GltfNodeTransforms.hpp"
#include "MeshLodGenerator.hpp"
#include "MeshBounds.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace projectunity::assets::detail {
namespace {

constexpr std::size_t kBatchThreshold = 512;
constexpr std::size_t kSpatialBatchTargetInstances = 64;
constexpr std::size_t kMaxSpatialBatchGridSide = 16;
constexpr std::size_t kMaxSpatialBatchTargets = 768;

struct BatchTarget {
    std::size_t materialIndex {0};
    std::size_t cellX {0};
    std::size_t cellY {0};
    std::uint32_t primitiveIndex {0};
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

[[nodiscard]] std::size_t instanceGridSide(std::size_t instanceCount) noexcept
{
    const auto wantedCells = std::max<std::size_t>(
        1U,
        (instanceCount + kSpatialBatchTargetInstances - 1U) / kSpatialBatchTargetInstances);
    return static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<float>(wantedCells))));
}

[[nodiscard]] std::size_t materialBudgetGridSide(std::size_t materialCount) noexcept
{
    const auto safeMaterialCount = std::max<std::size_t>(materialCount, 1U);
    const auto targetCells = std::max<std::size_t>(1U, kMaxSpatialBatchTargets / safeMaterialCount);
    return static_cast<std::size_t>(std::floor(std::sqrt(static_cast<float>(targetCells))));
}

[[nodiscard]] std::size_t gridCoordinate(float value, float minimum, float extent, std::size_t side) noexcept
{
    if (side <= 1U || extent <= 0.0001F || !std::isfinite(value)) {
        return 0;
    }
    const auto normalized = std::clamp((value - minimum) / extent, 0.0F, 0.999999F);
    return std::min(static_cast<std::size_t>(normalized * static_cast<float>(side)), side - 1U);
}

[[nodiscard]] SpatialBatchGrid makeSpatialBatchGrid(const ModelAsset& model) noexcept
{
    SpatialBatchGrid grid;
    const auto instanceSide = instanceGridSide(model.primitiveInstances.size());
    const auto materialSide = materialBudgetGridSide(model.materials.size());
    grid.side = std::clamp(
        std::min(instanceSide, materialSide),
        std::size_t {1U},
        kMaxSpatialBatchGridSide);
    if (grid.side <= 1U) {
        return grid;
    }

    MeshBounds sceneBounds;
    bool initialized = false;
    for (const auto& instance : model.primitiveInstances) {
        includeBounds(sceneBounds, instance.bounds, initialized);
    }
    if (!initialized) {
        grid.side = 1U;
        return grid;
    }

    sceneBounds.center = (sceneBounds.minimum + sceneBounds.maximum) * 0.5F;
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

void batchModelPrimitives(ModelAsset& model)
{
    deduplicateTextures(model);
    if (model.primitives.size() <= kBatchThreshold || model.primitiveInstances.size() <= kBatchThreshold) {
        deduplicateMaterials(model);
        return;
    }
    bakeFlatBaseColors(model);
    deduplicateMaterials(model);

    const auto spatialGrid = makeSpatialBatchGrid(model);
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
            targets.push_back({source.materialIndex, cellX, cellY, outputIndex});
            target = targets.end() - 1;
        }

        auto transformed = source;
        transformed.lods.clear();
        applyGltfTransform(transformed, toMatrix(instance.transform));
        auto& destination = batched[target->primitiveIndex];
        if (destination.vertices.size() + transformed.vertices.size() > std::numeric_limits<std::uint32_t>::max()) {
            return;
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
        instances.push_back(identityInstance(index, batched[index].bounds));
    }
    model.primitives = std::move(batched);
    model.primitiveInstances = std::move(instances);
}

} // namespace projectunity::assets::detail
