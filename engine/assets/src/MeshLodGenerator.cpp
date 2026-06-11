#include "MeshLodGenerator.hpp"

#include <meshoptimizer.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <utility>

namespace projectunity::assets::detail {
namespace {

constexpr std::array<std::size_t, 8> kMeshLodDivisors {{2U, 4U, 8U, 16U, 32U, 64U, 128U, 256U}};
constexpr std::array<float, 8> kMeshLodErrors {{0.005F, 0.012F, 0.025F, 0.05F, 0.09F, 0.14F, 0.22F, 0.32F}};
constexpr std::array<std::size_t, 4> kBudgetLodDivisors {{512U, 1024U, 2048U, 4096U}};
constexpr std::array<float, 4> kBudgetLodErrors {{0.55F, 0.85F, 1.20F, 1.60F}};

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
    if (sourceTriangles < 262'144U) {
        return;
    }
    for (std::size_t lodIndex = 0; lodIndex < kBudgetLodDivisors.size(); ++lodIndex) {
        const auto targetCount = (sourceTriangles / kBudgetLodDivisors[lodIndex]) * 3U;
        if (targetCount < 3U || targetCount >= primitive.indices.size()) {
            continue;
        }
        MeshLod lod;
        lod.indices.resize(primitive.indices.size());
        float resultError = 0.0F;
        const auto result = meshopt_simplifySloppy(
            lod.indices.data(),
            primitive.indices.data(),
            primitive.indices.size(),
            &primitive.vertices.front().position.x,
            primitive.vertices.size(),
            sizeof(MeshVertex),
            targetCount,
            kBudgetLodErrors[lodIndex],
            &resultError);
        lod.indices.resize(result);
        lod.error = (std::isfinite(resultError) && resultError > 0.0F ? resultError : kBudgetLodErrors[lodIndex]) * simplifyScale;
        const auto duplicate = std::any_of(primitive.lods.begin(), primitive.lods.end(), [&lod](const MeshLod& existing) {
            return existing.indices.size() == lod.indices.size();
        });
        if (!duplicate && lod.indices.size() >= 3U && lod.indices.size() < primitive.indices.size()) {
            primitive.lods.push_back(std::move(lod));
        }
    }
}

void rebuildMissingSimplificationLods(ModelAsset& model)
{
    for (auto& primitive : model.primitives) {
        if (primitive.lods.empty()) {
            rebuildSimplificationLods(primitive);
        }
    }
}

} // namespace projectunity::assets::detail
