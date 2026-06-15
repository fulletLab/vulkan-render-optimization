#pragma once

#include <projectunity/core/StableId.hpp>
#include <projectunity/math/Vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace projectunity::terrain {

enum class TerrainNoiseType : std::uint8_t {
    Value,
    Ridged,
};

enum class TerrainBrushMode : std::uint8_t {
    Raise,
    Lower,
    Smooth,
    Flatten,
};

struct TerrainBrushSettings {
    float radius {6.0F};
    float strength {1.0F};
    float falloff {0.5F};
    float targetHeight {0.0F};
};

struct TerrainSettings {
    float width {128.0F};
    float length {128.0F};
    float heightScale {24.0F};
    std::uint32_t resolution {129};
    std::uint32_t chunkSize {32};
    std::uint32_t seed {1337};
    TerrainNoiseType noiseType {TerrainNoiseType::Value};
    float frequency {0.0125F};
    std::uint32_t octaves {5};
    float persistence {0.5F};
    float lacunarity {2.0F};
    bool generateNormals {true};
    bool generateTangents {true};
    std::uint32_t lodLevels {3};
    bool generateCollider {false};

    [[nodiscard]] bool operator==(const TerrainSettings&) const noexcept = default;
};

struct TerrainMaterialLayer {
    std::string name {"Grass"};
    core::StableId baseColorTextureId;
    core::StableId normalTextureId;
    float metallic {0.0F};
    float roughness {0.9F};
    float tiling {8.0F};
    float strength {1.0F};
    std::array<float, 2> heightRange {0.0F, 1.0F};
    std::array<float, 2> slopeRange {0.0F, 1.0F};

    [[nodiscard]] bool operator==(const TerrainMaterialLayer&) const noexcept = default;
};

struct TerrainVertex {
    math::Vec3 position;
    math::Vec3 normal {0.0F, 1.0F, 0.0F};
    math::Vec3 tangent {1.0F, 0.0F, 0.0F};
    std::array<float, 2> texCoord {};
};

struct TerrainBounds {
    math::Vec3 minimum;
    math::Vec3 maximum;
    math::Vec3 center;
    float radius {0.0F};
};

struct TerrainChunkMesh {
    std::uint32_t chunkX {0};
    std::uint32_t chunkZ {0};
    std::uint32_t verticesX {0};
    std::uint32_t verticesZ {0};
    std::vector<TerrainVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<std::vector<std::uint32_t>> lodIndices;
    TerrainBounds bounds;
};

struct TerrainGenerationStats {
    std::size_t chunkCount {0};
    std::size_t vertexCount {0};
    std::size_t indexCount {0};
    float averageNormalY {0.0F};
    double generationMilliseconds {0.0};
};

struct TerrainGenerationResult {
    TerrainSettings settings;
    std::vector<float> heightmap;
    std::vector<TerrainChunkMesh> chunks;
    TerrainGenerationStats stats;
    std::string error;

    [[nodiscard]] bool succeeded() const noexcept
    {
        return error.empty() && !chunks.empty();
    }
};

} // namespace projectunity::terrain
