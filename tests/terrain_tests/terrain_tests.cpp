#include <projectunity/terrain/TerrainGenerator.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message)
{
    std::cerr << message << '\n';
    return EXIT_FAILURE;
}

} // namespace

int main()
{
    using namespace projectunity::terrain;

    TerrainSettings settings;
    settings.width = 16.0F;
    settings.length = 12.0F;
    settings.heightScale = 4.0F;
    settings.resolution = 9U;
    settings.chunkSize = 4U;
    settings.seed = 42U;
    settings.lodLevels = 3U;

    const auto first = TerrainGenerator::generate(settings);
    const auto second = TerrainGenerator::generate(settings);
    if (!first.succeeded() || first.chunks.size() != 4U || first.heightmap != second.heightmap) {
        return fail("terrain generation was not successful and deterministic");
    }
    if (first.stats.vertexCount != 100U || first.stats.indexCount != 384U) {
        return fail("terrain chunk geometry counts are incorrect");
    }
    for (const auto& chunk : first.chunks) {
        if (chunk.vertices.size() != 25U || chunk.indices.size() != 96U || chunk.lodIndices.empty()) {
            return fail("terrain chunk mesh or LOD data is incomplete");
        }
        if (!(chunk.bounds.radius > 0.0F) || chunk.bounds.minimum.x > chunk.bounds.maximum.x) {
            return fail("terrain chunk bounds are invalid");
        }
        for (const auto& vertex : chunk.vertices) {
            if (std::fabs(vertex.normal.length() - 1.0F) > 0.001F) {
                return fail("terrain generated a non-unit normal");
            }
        }
        for (std::size_t index = 2U; index < chunk.indices.size(); index += 3U) {
            const auto& a = chunk.vertices[chunk.indices[index - 2U]].position;
            const auto& b = chunk.vertices[chunk.indices[index - 1U]].position;
            const auto& c = chunk.vertices[chunk.indices[index]].position;
            if (projectunity::math::cross(b - a, c - a).y <= 0.0F) {
                return fail("terrain triangle winding does not face +Y");
            }
        }
    }
    if (first.stats.averageNormalY <= 0.5F) {
        return fail("terrain average normal does not face upward");
    }

    auto editedHeights = first.heightmap;
    TerrainBrushSettings brush;
    brush.radius = 3.0F;
    brush.strength = 1.5F;
    brush.falloff = 0.4F;
    const auto centerIndex = static_cast<std::size_t>(settings.resolution / 2U) * settings.resolution
        + settings.resolution / 2U;
    const auto originalCenter = editedHeights[centerIndex];
    if (!TerrainGenerator::applyBrush(editedHeights, settings, TerrainBrushMode::Raise, {}, brush)
        || editedHeights[centerIndex] <= originalCenter) {
        return fail("raise brush did not increase terrain height");
    }
    if (!TerrainGenerator::applyBrush(editedHeights, settings, TerrainBrushMode::Lower, {}, brush)
        || editedHeights[centerIndex] >= originalCenter + brush.strength) {
        return fail("lower brush did not decrease terrain height");
    }
    brush.targetHeight = 2.0F;
    brush.strength = 1.0F;
    if (!TerrainGenerator::applyBrush(editedHeights, settings, TerrainBrushMode::Flatten, {}, brush)) {
        return fail("flatten brush did not edit terrain height");
    }
    const auto rebuilt = TerrainGenerator::build(settings, editedHeights);
    if (!rebuilt.succeeded() || rebuilt.heightmap != editedHeights || rebuilt.stats.averageNormalY <= 0.5F) {
        return fail("edited terrain rebuild failed");
    }

    const auto hit = TerrainGenerator::raycast(
        rebuilt.heightmap,
        settings,
        {0.0F, 50.0F, 0.0F},
        {0.0F, -1.0F, 0.0F});
    if (!hit.has_value() || std::fabs(hit->y - rebuilt.heightmap[centerIndex]) > 0.02F) {
        return fail("terrain raycast missed the edited heightmap");
    }

    settings.resolution = 1U;
    const auto invalid = TerrainGenerator::generate(settings);
    if (invalid.succeeded() || invalid.error.empty()) {
        return fail("terrain accepted an invalid resolution");
    }

    return EXIT_SUCCESS;
}
