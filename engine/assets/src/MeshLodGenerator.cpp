#include "MeshLodGenerator.hpp"

#include <meshoptimizer.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <utility>

namespace projectunity::assets::detail {
namespace {

constexpr std::array<std::size_t, 6> kMeshLodDivisors {{2U, 4U, 8U, 16U, 32U, 64U}};
constexpr std::array<float, 6> kMeshLodErrors {{0.005F, 0.01F, 0.02F, 0.035F, 0.055F, 0.08F}};

} // namespace

void rebuildSimplificationLods(MeshPrimitive& primitive)
{
    primitive.lods.clear();
    const auto sourceTriangles = primitive.indices.size() / 3U;
    if (primitive.vertices.empty() || sourceTriangles < 64U) {
        return;
    }
    const auto simplifyScale = meshopt_simplifyScale(
        &primitive.vertices.front().position.x,
        primitive.vertices.size(),
        sizeof(MeshVertex));
    for (std::size_t lodIndex = 0; lodIndex < kMeshLodDivisors.size(); ++lodIndex) {
        const auto targetTriangleCount = sourceTriangles / kMeshLodDivisors[lodIndex];
        const auto targetCount = targetTriangleCount * 3U;
        if (targetCount < 3U || targetCount >= primitive.indices.size()) {
            continue;
        }
        MeshLod lod;
        lod.indices.resize(primitive.indices.size());
        float resultError = 0.0F;
        const auto result = meshopt_simplify(
            lod.indices.data(),
            primitive.indices.data(),
            primitive.indices.size(),
            &primitive.vertices.front().position.x,
            primitive.vertices.size(),
            sizeof(MeshVertex),
            targetCount,
            kMeshLodErrors[lodIndex],
            0,
            &resultError);
        lod.indices.resize(result);
        lod.error = resultError * simplifyScale;
        if (!std::isfinite(lod.error) || lod.error <= 0.0F) {
            lod.error = kMeshLodErrors[lodIndex] * simplifyScale;
        }
        const auto duplicate = std::any_of(primitive.lods.begin(), primitive.lods.end(), [&lod](const MeshLod& existing) {
            return existing.indices.size() == lod.indices.size();
        });
        if (!duplicate && lod.indices.size() >= 3U && lod.indices.size() < primitive.indices.size()) {
            primitive.lods.push_back(std::move(lod));
        }
    }
}

} // namespace projectunity::assets::detail
