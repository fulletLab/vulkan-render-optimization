#include "MeshPrimitiveChunker.hpp"

#include "MeshBounds.hpp"
#include "MeshLodGenerator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace projectunity::assets::detail {
namespace {

constexpr std::size_t kLargePrimitiveTriangleThreshold = 32'768;
constexpr std::size_t kTargetTrianglesPerChunk = 16'384;
constexpr std::size_t kMaxChunksPerPrimitive = 128;
constexpr std::uint32_t kInvalidVertex = std::numeric_limits<std::uint32_t>::max();

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

[[nodiscard]] math::Vec3 transformPoint(const std::array<float, 16>& matrix, math::Vec3 point) noexcept
{
    return {
        matrix[0] * point.x + matrix[4] * point.y + matrix[8] * point.z + matrix[12],
        matrix[1] * point.x + matrix[5] * point.y + matrix[9] * point.z + matrix[13],
        matrix[2] * point.x + matrix[6] * point.y + matrix[10] * point.z + matrix[14],
    };
}

void includePoint(MeshBounds& bounds, math::Vec3 point) noexcept
{
    bounds.minimum.x = std::min(bounds.minimum.x, point.x);
    bounds.minimum.y = std::min(bounds.minimum.y, point.y);
    bounds.minimum.z = std::min(bounds.minimum.z, point.z);
    bounds.maximum.x = std::max(bounds.maximum.x, point.x);
    bounds.maximum.y = std::max(bounds.maximum.y, point.y);
    bounds.maximum.z = std::max(bounds.maximum.z, point.z);
}

[[nodiscard]] MeshBounds transformedBounds(const MeshBounds& bounds, const std::array<float, 16>& transform) noexcept
{
    constexpr auto infinity = std::numeric_limits<float>::infinity();
    MeshBounds result;
    result.minimum = {infinity, infinity, infinity};
    result.maximum = {-infinity, -infinity, -infinity};

    const std::array<math::Vec3, 8> corners {{
        {bounds.minimum.x, bounds.minimum.y, bounds.minimum.z},
        {bounds.maximum.x, bounds.minimum.y, bounds.minimum.z},
        {bounds.minimum.x, bounds.maximum.y, bounds.minimum.z},
        {bounds.maximum.x, bounds.maximum.y, bounds.minimum.z},
        {bounds.minimum.x, bounds.minimum.y, bounds.maximum.z},
        {bounds.maximum.x, bounds.minimum.y, bounds.maximum.z},
        {bounds.minimum.x, bounds.maximum.y, bounds.maximum.z},
        {bounds.maximum.x, bounds.maximum.y, bounds.maximum.z},
    }};
    for (const auto corner : corners) {
        includePoint(result, transformPoint(transform, corner));
    }

    result.center = (result.minimum + result.maximum) * 0.5F;
    float radiusSquared = 0.0F;
    for (const auto corner : corners) {
        radiusSquared = std::max(radiusSquared, math::distanceSquared(result.center, transformPoint(transform, corner)));
    }
    result.radius = std::sqrt(radiusSquared);
    return result;
}

[[nodiscard]] std::array<int, 2> chunkAxes(const MeshBounds& bounds) noexcept
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

[[nodiscard]] std::size_t gridCoordinate(float value, float minimum, float extent, std::size_t gridSide) noexcept
{
    if (extent <= 0.0001F || gridSide <= 1U || !std::isfinite(value)) {
        return 0;
    }
    const auto normalized = std::clamp((value - minimum) / extent, 0.0F, 0.999999F);
    return std::min(static_cast<std::size_t>(normalized * static_cast<float>(gridSide)), gridSide - 1U);
}

[[nodiscard]] std::size_t gridSideForTriangleCount(std::size_t triangleCount) noexcept
{
    const auto wantedChunks = std::clamp(
        (triangleCount + kTargetTrianglesPerChunk - 1U) / kTargetTrianglesPerChunk,
        std::size_t {2U},
        kMaxChunksPerPrimitive);
    return static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<float>(wantedChunks))));
}

[[nodiscard]] bool primitiveMayNeedSplitting(const MeshPrimitive& primitive) noexcept
{
    return primitive.indices.size() / 3U > kLargePrimitiveTriangleThreshold && !primitive.vertices.empty();
}

[[nodiscard]] std::vector<MeshPrimitive> splitPrimitive(const MeshPrimitive& source)
{
    const auto triangleCount = source.indices.size() / 3U;
    if (!primitiveMayNeedSplitting(source)) {
        return {};
    }

    const auto axes = chunkAxes(source.bounds);
    const auto axisA = axes[0];
    const auto axisB = axes[1];
    const auto minA = component(source.bounds.minimum, axisA);
    const auto minB = component(source.bounds.minimum, axisB);
    const auto extentA = component(source.bounds.maximum, axisA) - minA;
    const auto extentB = component(source.bounds.maximum, axisB) - minB;
    if (std::fabs(extentA) <= 0.0001F && std::fabs(extentB) <= 0.0001F) {
        return {};
    }

    const auto gridSide = gridSideForTriangleCount(triangleCount);
    const auto bucketCount = gridSide * gridSide;
    std::vector<std::vector<std::uint32_t>> buckets(bucketCount);
    for (std::uint32_t index = 2; index < source.indices.size(); index += 3U) {
        const auto i0 = source.indices[index - 2U];
        const auto i1 = source.indices[index - 1U];
        const auto i2 = source.indices[index];
        if (i0 >= source.vertices.size() || i1 >= source.vertices.size() || i2 >= source.vertices.size()) {
            continue;
        }
        const auto centroid = (source.vertices[i0].position + source.vertices[i1].position + source.vertices[i2].position) / 3.0F;
        const auto x = gridCoordinate(component(centroid, axisA), minA, extentA, gridSide);
        const auto y = gridCoordinate(component(centroid, axisB), minB, extentB, gridSide);
        buckets[y * gridSide + x].push_back(index - 2U);
    }

    std::vector<std::uint32_t> remap(source.vertices.size(), kInvalidVertex);
    std::vector<std::uint32_t> touched;
    std::vector<MeshPrimitive> chunks;
    chunks.reserve(bucketCount);
    for (const auto& bucket : buckets) {
        if (bucket.empty()) {
            continue;
        }
        MeshPrimitive chunk;
        chunk.materialIndex = source.materialIndex;
        chunk.indices.reserve(bucket.size() * 3U);
        touched.clear();
        for (const auto triangleOffset : bucket) {
            for (std::uint32_t corner = 0; corner < 3U; ++corner) {
                const auto sourceIndex = source.indices[triangleOffset + corner];
                auto& mapped = remap[sourceIndex];
                if (mapped == kInvalidVertex) {
                    mapped = static_cast<std::uint32_t>(chunk.vertices.size());
                    touched.push_back(sourceIndex);
                    chunk.vertices.push_back(source.vertices[sourceIndex]);
                }
                chunk.indices.push_back(mapped);
            }
        }
        for (const auto sourceIndex : touched) {
            remap[sourceIndex] = kInvalidVertex;
        }
        if (!chunk.indices.empty()) {
            updateMeshBounds(chunk);
            rebuildSimplificationLods(chunk);
            chunks.push_back(std::move(chunk));
        }
    }

    return chunks.size() > 1U ? chunks : std::vector<MeshPrimitive> {};
}

} // namespace

void splitLargePrimitivesIntoSpatialChunks(ModelAsset& model)
{
    if (model.primitives.empty()) {
        return;
    }
    const auto hasLargePrimitive = std::any_of(model.primitives.begin(), model.primitives.end(), primitiveMayNeedSplitting);
    if (!hasLargePrimitive) {
        return;
    }

    std::vector<MeshPrimitive> primitives;
    std::vector<std::vector<std::uint32_t>> primitiveMap(model.primitives.size());
    primitives.reserve(model.primitives.size());
    bool splitAny = false;
    for (std::uint32_t index = 0; index < model.primitives.size(); ++index) {
        auto chunks = splitPrimitive(model.primitives[index]);
        if (chunks.empty()) {
            const auto mappedIndex = static_cast<std::uint32_t>(primitives.size());
            primitiveMap[index].push_back(mappedIndex);
            primitives.push_back(model.primitives[index]);
            continue;
        }
        splitAny = true;
        primitiveMap[index].reserve(chunks.size());
        for (auto& chunk : chunks) {
            const auto mappedIndex = static_cast<std::uint32_t>(primitives.size());
            primitiveMap[index].push_back(mappedIndex);
            primitives.push_back(std::move(chunk));
        }
    }

    if (!splitAny) {
        return;
    }

    if (!model.primitiveInstances.empty()) {
        std::vector<MeshPrimitiveInstance> instances;
        for (const auto& instance : model.primitiveInstances) {
            if (instance.primitiveIndex >= primitiveMap.size()) {
                continue;
            }
            for (const auto mappedPrimitive : primitiveMap[instance.primitiveIndex]) {
                MeshPrimitiveInstance next = instance;
                next.primitiveIndex = mappedPrimitive;
                next.bounds = transformedBounds(primitives[mappedPrimitive].bounds, next.transform);
                instances.push_back(next);
            }
        }
        model.primitiveInstances = std::move(instances);
    }
    model.primitives = std::move(primitives);
}

} // namespace projectunity::assets::detail
