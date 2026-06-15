#pragma once

#include <projectunity/terrain/TerrainTypes.hpp>

#include <optional>
#include <span>

namespace projectunity::terrain {

class TerrainGenerator final {
public:
    [[nodiscard]] static TerrainGenerationResult generate(const TerrainSettings& settings);
    [[nodiscard]] static TerrainGenerationResult build(
        const TerrainSettings& settings,
        std::span<const float> heightmap);
    [[nodiscard]] static bool applyBrush(
        std::vector<float>& heightmap,
        const TerrainSettings& settings,
        TerrainBrushMode mode,
        math::Vec3 localCenter,
        const TerrainBrushSettings& brush);
    [[nodiscard]] static std::optional<math::Vec3> raycast(
        std::span<const float> heightmap,
        const TerrainSettings& settings,
        math::Vec3 localRayOrigin,
        math::Vec3 localRayDirection);
    [[nodiscard]] static std::optional<float> sampleHeight(
        std::span<const float> heightmap,
        const TerrainSettings& settings,
        float localX,
        float localZ);
    [[nodiscard]] static math::Vec3 surfaceNormal(
        std::span<const float> heightmap,
        const TerrainSettings& settings,
        float localX,
        float localZ);
};

} // namespace projectunity::terrain
