#include <projectunity/terrain/TerrainGenerator.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <span>

namespace projectunity::terrain {
namespace {

constexpr std::uint32_t kMaximumResolution = 4097;
constexpr std::uint32_t kMaximumChunkSize = 512;
constexpr std::uint32_t kMaximumOctaves = 12;

[[nodiscard]] std::uint32_t hashCoordinate(std::int32_t x, std::int32_t z, std::uint32_t seed) noexcept
{
    auto value = seed ^ (static_cast<std::uint32_t>(x) * 0x9E3779B9U);
    value ^= static_cast<std::uint32_t>(z) * 0x85EBCA6BU;
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    value ^= value >> 16U;
    return value;
}

[[nodiscard]] float randomUnit(std::int32_t x, std::int32_t z, std::uint32_t seed) noexcept
{
    return static_cast<float>(hashCoordinate(x, z, seed) & 0x00FFFFFFU)
        / static_cast<float>(0x00FFFFFFU);
}

[[nodiscard]] float smooth(float value) noexcept
{
    return value * value * (3.0F - 2.0F * value);
}

[[nodiscard]] float interpolate(float lhs, float rhs, float amount) noexcept
{
    return lhs + (rhs - lhs) * amount;
}

[[nodiscard]] float valueNoise(float x, float z, std::uint32_t seed) noexcept
{
    const auto x0 = static_cast<std::int32_t>(std::floor(x));
    const auto z0 = static_cast<std::int32_t>(std::floor(z));
    const auto x1 = x0 + 1;
    const auto z1 = z0 + 1;
    const auto tx = smooth(x - static_cast<float>(x0));
    const auto tz = smooth(z - static_cast<float>(z0));
    const auto lower = interpolate(randomUnit(x0, z0, seed), randomUnit(x1, z0, seed), tx);
    const auto upper = interpolate(randomUnit(x0, z1, seed), randomUnit(x1, z1, seed), tx);
    return interpolate(lower, upper, tz);
}

[[nodiscard]] float terrainNoise(float x, float z, const TerrainSettings& settings) noexcept
{
    float amplitude = 1.0F;
    float frequency = settings.frequency;
    float total = 0.0F;
    float amplitudeTotal = 0.0F;
    for (std::uint32_t octave = 0; octave < settings.octaves; ++octave) {
        auto sample = valueNoise(x * frequency, z * frequency, settings.seed + octave * 1013U);
        if (settings.noiseType == TerrainNoiseType::Ridged) {
            sample = 1.0F - std::fabs(sample * 2.0F - 1.0F);
        }
        total += sample * amplitude;
        amplitudeTotal += amplitude;
        amplitude *= settings.persistence;
        frequency *= settings.lacunarity;
    }
    const auto normalized = amplitudeTotal > 0.0F ? total / amplitudeTotal : 0.5F;
    return (normalized * 2.0F - 1.0F) * settings.heightScale;
}

[[nodiscard]] bool validate(const TerrainSettings& settings, std::string& error)
{
    if (!(settings.width > 0.0F) || !(settings.length > 0.0F) || settings.heightScale < 0.0F) {
        error = "Terrain dimensions and height scale are invalid";
        return false;
    }
    if (settings.resolution < 2U || settings.resolution > kMaximumResolution) {
        error = "Terrain resolution must be between 2 and 4097";
        return false;
    }
    if (settings.chunkSize == 0U || settings.chunkSize > kMaximumChunkSize) {
        error = "Terrain chunk size must be between 1 and 512";
        return false;
    }
    if (!(settings.frequency > 0.0F) || settings.octaves == 0U || settings.octaves > kMaximumOctaves
        || settings.persistence < 0.0F || settings.persistence > 1.0F || settings.lacunarity < 1.0F) {
        error = "Terrain noise settings are invalid";
        return false;
    }
    return true;
}

[[nodiscard]] std::size_t heightIndex(std::uint32_t x, std::uint32_t z, std::uint32_t resolution) noexcept
{
    return static_cast<std::size_t>(z) * resolution + x;
}

[[nodiscard]] float sampleHeightUnchecked(
    std::span<const float> heights,
    const TerrainSettings& settings,
    float localX,
    float localZ) noexcept
{
    const auto normalizedX = localX / settings.width + 0.5F;
    const auto normalizedZ = localZ / settings.length + 0.5F;
    if (normalizedX < 0.0F || normalizedX > 1.0F || normalizedZ < 0.0F || normalizedZ > 1.0F) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    const auto maximum = settings.resolution - 1U;
    const auto gridX = normalizedX * static_cast<float>(maximum);
    const auto gridZ = normalizedZ * static_cast<float>(maximum);
    const auto x0 = static_cast<std::uint32_t>(std::floor(gridX));
    const auto z0 = static_cast<std::uint32_t>(std::floor(gridZ));
    const auto x1 = std::min(x0 + 1U, maximum);
    const auto z1 = std::min(z0 + 1U, maximum);
    const auto tx = gridX - static_cast<float>(x0);
    const auto tz = gridZ - static_cast<float>(z0);
    const auto lower = interpolate(
        heights[heightIndex(x0, z0, settings.resolution)],
        heights[heightIndex(x1, z0, settings.resolution)],
        tx);
    const auto upper = interpolate(
        heights[heightIndex(x0, z1, settings.resolution)],
        heights[heightIndex(x1, z1, settings.resolution)],
        tx);
    return interpolate(lower, upper, tz);
}

[[nodiscard]] math::Vec3 normalAt(
    const std::vector<float>& heights,
    std::uint32_t x,
    std::uint32_t z,
    const TerrainSettings& settings) noexcept
{
    if (!settings.generateNormals) {
        return {0.0F, 1.0F, 0.0F};
    }
    const auto maximum = settings.resolution - 1U;
    const auto left = heights[heightIndex(x == 0U ? 0U : x - 1U, z, settings.resolution)];
    const auto right = heights[heightIndex(std::min(x + 1U, maximum), z, settings.resolution)];
    const auto down = heights[heightIndex(x, z == 0U ? 0U : z - 1U, settings.resolution)];
    const auto up = heights[heightIndex(x, std::min(z + 1U, maximum), settings.resolution)];
    const auto spacingX = settings.width / static_cast<float>(maximum);
    const auto spacingZ = settings.length / static_cast<float>(maximum);
    return math::Vec3 {
        (left - right) / (2.0F * spacingX),
        1.0F,
        (down - up) / (2.0F * spacingZ),
    }
        .normalized(std::min(spacingX, spacingZ) * 0.00001F);
}

[[nodiscard]] math::Vec3 tangentFromNormal(math::Vec3 normal) noexcept
{
    const auto tangent = math::cross(normal, {0.0F, 0.0F, 1.0F}).normalized();
    return tangent.lengthSquared() > 0.0F ? tangent : math::Vec3 {1.0F, 0.0F, 0.0F};
}

[[nodiscard]] bool raySlab(float origin, float direction, float minimum, float maximum, float& nearT, float& farT)
{
    if (std::fabs(direction) <= 0.000001F) {
        return origin >= minimum && origin <= maximum;
    }
    auto first = (minimum - origin) / direction;
    auto second = (maximum - origin) / direction;
    if (first > second) {
        std::swap(first, second);
    }
    nearT = std::max(nearT, first);
    farT = std::min(farT, second);
    return nearT <= farT;
}

void appendGridIndices(
    std::vector<std::uint32_t>& output,
    std::uint32_t verticesX,
    std::uint32_t verticesZ,
    std::uint32_t step)
{
    for (std::uint32_t z = 0; z + 1U < verticesZ; z += step) {
        const auto nextZ = std::min(z + step, verticesZ - 1U);
        for (std::uint32_t x = 0; x + 1U < verticesX; x += step) {
            const auto nextX = std::min(x + step, verticesX - 1U);
            const auto i0 = z * verticesX + x;
            const auto i1 = z * verticesX + nextX;
            const auto i2 = nextZ * verticesX + x;
            const auto i3 = nextZ * verticesX + nextX;
            output.insert(output.end(), {i0, i2, i1, i1, i2, i3});
        }
    }
}

void updateBounds(TerrainChunkMesh& chunk)
{
    auto minimum = math::Vec3 {
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
    };
    auto maximum = math::Vec3 {
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
    };
    for (const auto& vertex : chunk.vertices) {
        minimum.x = std::min(minimum.x, vertex.position.x);
        minimum.y = std::min(minimum.y, vertex.position.y);
        minimum.z = std::min(minimum.z, vertex.position.z);
        maximum.x = std::max(maximum.x, vertex.position.x);
        maximum.y = std::max(maximum.y, vertex.position.y);
        maximum.z = std::max(maximum.z, vertex.position.z);
    }
    chunk.bounds.minimum = minimum;
    chunk.bounds.maximum = maximum;
    chunk.bounds.center = (minimum + maximum) * 0.5F;
    float radiusSquared = 0.0F;
    for (const auto& vertex : chunk.vertices) {
        radiusSquared = std::max(radiusSquared, math::distanceSquared(chunk.bounds.center, vertex.position));
    }
    chunk.bounds.radius = std::sqrt(radiusSquared);
}

} // namespace

TerrainGenerationResult TerrainGenerator::generate(const TerrainSettings& settings)
{
    std::string error;
    if (!validate(settings, error)) {
        TerrainGenerationResult invalid;
        invalid.settings = settings;
        invalid.error = std::move(error);
        return invalid;
    }

    const auto started = std::chrono::steady_clock::now();
    std::vector<float> heightmap(static_cast<std::size_t>(settings.resolution) * settings.resolution);
    const auto denominator = static_cast<float>(settings.resolution - 1U);
    for (std::uint32_t z = 0; z < settings.resolution; ++z) {
        for (std::uint32_t x = 0; x < settings.resolution; ++x) {
            const auto worldX = (static_cast<float>(x) / denominator - 0.5F) * settings.width;
            const auto worldZ = (static_cast<float>(z) / denominator - 0.5F) * settings.length;
            heightmap[heightIndex(x, z, settings.resolution)] = terrainNoise(worldX, worldZ, settings);
        }
    }
    auto result = build(settings, heightmap);
    result.stats.generationMilliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    return result;
}

TerrainGenerationResult TerrainGenerator::build(const TerrainSettings& settings, std::span<const float> heightmap)
{
    TerrainGenerationResult result;
    result.settings = settings;
    if (!validate(settings, result.error)) {
        return result;
    }
    const auto expectedSize = static_cast<std::size_t>(settings.resolution) * settings.resolution;
    if (heightmap.size() != expectedSize) {
        result.error = "Terrain heightmap size does not match resolution";
        return result;
    }
    const auto started = std::chrono::steady_clock::now();
    result.heightmap.assign(heightmap.begin(), heightmap.end());
    const auto denominator = static_cast<float>(settings.resolution - 1U);

    const auto quads = settings.resolution - 1U;
    const auto chunksPerX = (quads + settings.chunkSize - 1U) / settings.chunkSize;
    const auto chunksPerZ = (quads + settings.chunkSize - 1U) / settings.chunkSize;
    result.chunks.reserve(static_cast<std::size_t>(chunksPerX) * chunksPerZ);
    for (std::uint32_t chunkZ = 0; chunkZ < chunksPerZ; ++chunkZ) {
        for (std::uint32_t chunkX = 0; chunkX < chunksPerX; ++chunkX) {
            const auto startX = chunkX * settings.chunkSize;
            const auto startZ = chunkZ * settings.chunkSize;
            const auto endX = std::min(startX + settings.chunkSize, quads);
            const auto endZ = std::min(startZ + settings.chunkSize, quads);

            TerrainChunkMesh chunk;
            chunk.chunkX = chunkX;
            chunk.chunkZ = chunkZ;
            chunk.verticesX = endX - startX + 1U;
            chunk.verticesZ = endZ - startZ + 1U;
            chunk.vertices.reserve(static_cast<std::size_t>(chunk.verticesX) * chunk.verticesZ);
            for (std::uint32_t z = startZ; z <= endZ; ++z) {
                for (std::uint32_t x = startX; x <= endX; ++x) {
                    TerrainVertex vertex;
                    vertex.position = {
                        (static_cast<float>(x) / denominator - 0.5F) * settings.width,
                        result.heightmap[heightIndex(x, z, settings.resolution)],
                        (static_cast<float>(z) / denominator - 0.5F) * settings.length,
                    };
                    vertex.normal = normalAt(result.heightmap, x, z, settings);
                    result.stats.averageNormalY += vertex.normal.y;
                    if (settings.generateTangents) {
                        vertex.tangent = tangentFromNormal(vertex.normal);
                    }
                    vertex.texCoord = {static_cast<float>(x) / denominator, static_cast<float>(z) / denominator};
                    chunk.vertices.push_back(vertex);
                }
            }

            appendGridIndices(chunk.indices, chunk.verticesX, chunk.verticesZ, 1U);
            for (std::uint32_t level = 1U; level < settings.lodLevels; ++level) {
                const auto step = 1U << std::min(level, 15U);
                if (step >= chunk.verticesX && step >= chunk.verticesZ) {
                    break;
                }
                auto& lod = chunk.lodIndices.emplace_back();
                appendGridIndices(lod, chunk.verticesX, chunk.verticesZ, step);
            }
            updateBounds(chunk);
            result.stats.vertexCount += chunk.vertices.size();
            result.stats.indexCount += chunk.indices.size();
            result.chunks.push_back(std::move(chunk));
        }
    }

    result.stats.chunkCount = result.chunks.size();
    if (result.stats.vertexCount > 0U) {
        result.stats.averageNormalY /= static_cast<float>(result.stats.vertexCount);
    }
    result.stats.generationMilliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    return result;
}

bool TerrainGenerator::applyBrush(
    std::vector<float>& heightmap,
    const TerrainSettings& settings,
    TerrainBrushMode mode,
    math::Vec3 localCenter,
    const TerrainBrushSettings& brush)
{
    const auto expectedSize = static_cast<std::size_t>(settings.resolution) * settings.resolution;
    if (heightmap.size() != expectedSize || !(brush.radius > 0.0F) || brush.strength < 0.0F) {
        return false;
    }
    const auto source = mode == TerrainBrushMode::Smooth ? heightmap : std::vector<float> {};
    const auto maximum = settings.resolution - 1U;
    const auto exponent = interpolate(1.0F, 5.0F, std::clamp(brush.falloff, 0.0F, 1.0F));
    bool changed = false;
    for (std::uint32_t z = 0; z < settings.resolution; ++z) {
        const auto localZ = (static_cast<float>(z) / static_cast<float>(maximum) - 0.5F) * settings.length;
        for (std::uint32_t x = 0; x < settings.resolution; ++x) {
            const auto localX = (static_cast<float>(x) / static_cast<float>(maximum) - 0.5F) * settings.width;
            const auto dx = localX - localCenter.x;
            const auto dz = localZ - localCenter.z;
            const auto distance = std::sqrt(dx * dx + dz * dz);
            if (distance > brush.radius) {
                continue;
            }
            const auto weight = std::pow(std::max(0.0F, 1.0F - distance / brush.radius), exponent);
            const auto index = heightIndex(x, z, settings.resolution);
            const auto current = heightmap[index];
            float next = current;
            if (mode == TerrainBrushMode::Raise) {
                next += brush.strength * weight;
            } else if (mode == TerrainBrushMode::Lower) {
                next -= brush.strength * weight;
            } else if (mode == TerrainBrushMode::Flatten) {
                next = interpolate(current, brush.targetHeight, std::clamp(brush.strength * weight, 0.0F, 1.0F));
            } else {
                float sum = 0.0F;
                std::uint32_t count = 0U;
                for (int offsetZ = -1; offsetZ <= 1; ++offsetZ) {
                    for (int offsetX = -1; offsetX <= 1; ++offsetX) {
                        const auto sampleX = std::clamp(static_cast<int>(x) + offsetX, 0, static_cast<int>(maximum));
                        const auto sampleZ = std::clamp(static_cast<int>(z) + offsetZ, 0, static_cast<int>(maximum));
                        sum += source[heightIndex(
                            static_cast<std::uint32_t>(sampleX),
                            static_cast<std::uint32_t>(sampleZ),
                            settings.resolution)];
                        ++count;
                    }
                }
                next = interpolate(current, sum / static_cast<float>(count), std::clamp(brush.strength * weight, 0.0F, 1.0F));
            }
            if (std::fabs(next - current) > 0.000001F) {
                heightmap[index] = next;
                changed = true;
            }
        }
    }
    return changed;
}

std::optional<math::Vec3> TerrainGenerator::raycast(
    std::span<const float> heightmap,
    const TerrainSettings& settings,
    math::Vec3 localRayOrigin,
    math::Vec3 localRayDirection)
{
    const auto expectedSize = static_cast<std::size_t>(settings.resolution) * settings.resolution;
    if (heightmap.size() != expectedSize || localRayDirection.lengthSquared() <= 0.000001F) {
        return std::nullopt;
    }
    localRayDirection = localRayDirection.normalized();
    float nearT = 0.0F;
    float farT = 100000.0F;
    if (!raySlab(localRayOrigin.x, localRayDirection.x, -settings.width * 0.5F, settings.width * 0.5F, nearT, farT)
        || !raySlab(localRayOrigin.z, localRayDirection.z, -settings.length * 0.5F, settings.length * 0.5F, nearT, farT)) {
        return std::nullopt;
    }
    const auto spacing = std::min(
        settings.width / static_cast<float>(settings.resolution - 1U),
        settings.length / static_cast<float>(settings.resolution - 1U));
    const auto step = std::max(spacing * 0.35F, 0.02F);
    auto previousT = nearT;
    auto previousPoint = localRayOrigin + localRayDirection * previousT;
    auto previousDelta = previousPoint.y - sampleHeightUnchecked(heightmap, settings, previousPoint.x, previousPoint.z);
    for (auto currentT = nearT + step; currentT <= farT + step; currentT += step) {
        const auto clampedT = std::min(currentT, farT);
        const auto point = localRayOrigin + localRayDirection * clampedT;
        const auto height = sampleHeightUnchecked(heightmap, settings, point.x, point.z);
        if (!std::isfinite(height)) {
            continue;
        }
        const auto delta = point.y - height;
        if ((previousDelta >= 0.0F && delta <= 0.0F) || (previousDelta <= 0.0F && delta >= 0.0F)) {
            auto low = previousT;
            auto high = clampedT;
            for (int iteration = 0; iteration < 10; ++iteration) {
                const auto middle = (low + high) * 0.5F;
                const auto middlePoint = localRayOrigin + localRayDirection * middle;
                const auto middleHeight = sampleHeightUnchecked(heightmap, settings, middlePoint.x, middlePoint.z);
                if (middlePoint.y - middleHeight > 0.0F) {
                    low = middle;
                } else {
                    high = middle;
                }
            }
            const auto hit = localRayOrigin + localRayDirection * ((low + high) * 0.5F);
            return math::Vec3 {hit.x, sampleHeightUnchecked(heightmap, settings, hit.x, hit.z), hit.z};
        }
        previousT = clampedT;
        previousDelta = delta;
    }
    return std::nullopt;
}

std::optional<float> TerrainGenerator::sampleHeight(
    std::span<const float> heightmap,
    const TerrainSettings& settings,
    float localX,
    float localZ)
{
    const auto expectedSize = static_cast<std::size_t>(settings.resolution) * settings.resolution;
    if (heightmap.size() != expectedSize || settings.width <= 0.0F || settings.length <= 0.0F
        || settings.resolution < 2U) {
        return std::nullopt;
    }
    const auto height = sampleHeightUnchecked(heightmap, settings, localX, localZ);
    return std::isfinite(height) ? std::optional<float> {height} : std::nullopt;
}

math::Vec3 TerrainGenerator::surfaceNormal(
    std::span<const float> heightmap,
    const TerrainSettings& settings,
    float localX,
    float localZ)
{
    const auto spacing = std::max(0.001F, std::min(
        settings.width / static_cast<float>(std::max(1U, settings.resolution - 1U)),
        settings.length / static_cast<float>(std::max(1U, settings.resolution - 1U))));
    const auto left = sampleHeight(heightmap, settings, localX - spacing, localZ);
    const auto right = sampleHeight(heightmap, settings, localX + spacing, localZ);
    const auto down = sampleHeight(heightmap, settings, localX, localZ - spacing);
    const auto up = sampleHeight(heightmap, settings, localX, localZ + spacing);
    if (!left.has_value() || !right.has_value() || !down.has_value() || !up.has_value()) {
        return {0.0F, 1.0F, 0.0F};
    }
    return math::Vec3 {
        (*left - *right) / (2.0F * spacing),
        1.0F,
        (*down - *up) / (2.0F * spacing),
    }.normalized(0.000001F);
}

} // namespace projectunity::terrain
