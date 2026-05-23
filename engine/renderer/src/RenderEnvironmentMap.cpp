#include <projectunity/renderer/RenderEnvironmentMap.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace projectunity::renderer {
namespace {
constexpr float kPi = 3.14159265358979323846F;

struct Vec3 {
    float x {0.0F};
    float y {0.0F};
    float z {0.0F};
};

[[nodiscard]] Vec3 operator*(Vec3 value, float scalar)
{
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

[[nodiscard]] Vec3 operator+(Vec3 lhs, Vec3 rhs)
{
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

[[nodiscard]] float dot(Vec3 lhs, Vec3 rhs)
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

[[nodiscard]] Vec3 normalize(Vec3 value)
{
    const auto length = std::sqrt(std::max(dot(value, value), 0.000001F));
    return value * (1.0F / length);
}

[[nodiscard]] float smoothStep(float edge0, float edge1, float value)
{
    const auto t = std::clamp((value - edge0) / std::max(edge1 - edge0, 0.0001F), 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

[[nodiscard]] Vec3 mix(Vec3 lhs, Vec3 rhs, float amount)
{
    return lhs * (1.0F - amount) + rhs * amount;
}

[[nodiscard]] bool textureUsable(const assets::TextureAsset* texture)
{
    return texture != nullptr
        && texture->id.isValid()
        && texture->width > 0
        && texture->height > 0
        && texture->rgba8.size() >= static_cast<std::size_t>(texture->width) * texture->height * 4U;
}

[[nodiscard]] Vec3 toVec3(const std::array<float, 3>& value)
{
    return {value[0], value[1], value[2]};
}

[[nodiscard]] Vec3 faceDirection(std::uint32_t face, float u, float v)
{
    switch (face) {
    case 0:
        return normalize({1.0F, -v, -u});
    case 1:
        return normalize({-1.0F, -v, u});
    case 2:
        return normalize({u, 1.0F, v});
    case 3:
        return normalize({u, -1.0F, -v});
    case 4:
        return normalize({u, -v, 1.0F});
    default:
        return normalize({-u, -v, -1.0F});
    }
}

[[nodiscard]] Vec3 sampleTextureNearest(const assets::TextureAsset& texture, float u, float v)
{
    u = u - std::floor(u);
    v = std::clamp(v, 0.0F, 1.0F);
    const auto x = std::min(
        static_cast<std::uint32_t>(u * static_cast<float>(texture.width)),
        texture.width - 1U);
    const auto y = std::min(
        static_cast<std::uint32_t>(v * static_cast<float>(texture.height)),
        texture.height - 1U);
    const auto offset = (static_cast<std::size_t>(y) * texture.width + x) * 4U;
    return {
        static_cast<float>(texture.rgba8[offset]) / 255.0F,
        static_cast<float>(texture.rgba8[offset + 1U]) / 255.0F,
        static_cast<float>(texture.rgba8[offset + 2U]) / 255.0F,
    };
}

[[nodiscard]] Vec3 sampleEquirectangular(
    const assets::TextureAsset& texture,
    Vec3 direction,
    float roughness)
{
    const auto u = (std::atan2(direction.x, direction.z) / (2.0F * kPi)) + 0.5F;
    const auto v = std::acos(std::clamp(direction.y, -1.0F, 1.0F)) / kPi;
    const auto radius = roughness * 0.04F;
    auto color = sampleTextureNearest(texture, u, v) * 0.5F;
    color = color + sampleTextureNearest(texture, u + radius, v) * 0.125F;
    color = color + sampleTextureNearest(texture, u - radius, v) * 0.125F;
    color = color + sampleTextureNearest(texture, u, v + radius) * 0.125F;
    color = color + sampleTextureNearest(texture, u, v - radius) * 0.125F;
    return color;
}

[[nodiscard]] Vec3 proceduralEnvironment(
    Vec3 direction,
    float roughness,
    const RenderEnvironmentSettings& settings)
{
    if (textureUsable(settings.sourceTexture)) {
        return sampleEquirectangular(*settings.sourceTexture, direction, roughness)
            * std::max(settings.intensity, 0.0F);
    }
    const auto sky = toVec3(settings.skyColor) * std::max(settings.intensity, 0.0F);
    const auto ground = toVec3(settings.groundColor) * std::max(settings.intensity, 0.0F);
    const auto skyWeight = smoothStep(-0.18F, 0.78F, direction.y);
    const auto horizonWeight = std::pow(std::clamp(1.0F - std::fabs(direction.y), 0.0F, 1.0F), 2.0F);
    const auto roughSky = sky * std::lerp(1.35F, 0.82F, roughness);
    const auto roughGround = ground * std::lerp(1.12F, 0.92F, roughness);
    const auto horizon = mix(roughGround, roughSky, 0.5F) * 1.22F;
    return mix(mix(roughGround, roughSky, skyWeight), horizon, horizonWeight * 0.45F);
}

void appendCubeMip(
    RenderCubeMap& cube,
    std::uint32_t faceSize,
    float roughness,
    const RenderEnvironmentSettings& settings)
{
    RenderCubeMip mip;
    mip.faceSize = faceSize;
    mip.rgba8.resize(static_cast<std::size_t>(faceSize) * faceSize * 6U * 4U);
    for (std::uint32_t face = 0; face < 6U; ++face) {
        for (std::uint32_t y = 0; y < faceSize; ++y) {
            for (std::uint32_t x = 0; x < faceSize; ++x) {
                const auto u = ((static_cast<float>(x) + 0.5F) / static_cast<float>(faceSize)) * 2.0F - 1.0F;
                const auto v = ((static_cast<float>(y) + 0.5F) / static_cast<float>(faceSize)) * 2.0F - 1.0F;
                const auto color = proceduralEnvironment(faceDirection(face, u, v), roughness, settings);
                const auto offset = ((static_cast<std::size_t>(face) * faceSize * faceSize)
                    + static_cast<std::size_t>(y) * faceSize + x)
                    * 4U;
                mip.rgba8[offset] = static_cast<std::uint8_t>(std::clamp(color.x, 0.0F, 1.0F) * 255.0F + 0.5F);
                mip.rgba8[offset + 1U] = static_cast<std::uint8_t>(std::clamp(color.y, 0.0F, 1.0F) * 255.0F + 0.5F);
                mip.rgba8[offset + 2U] = static_cast<std::uint8_t>(std::clamp(color.z, 0.0F, 1.0F) * 255.0F + 0.5F);
                mip.rgba8[offset + 3U] = 255U;
            }
        }
    }
    cube.mips.push_back(std::move(mip));
}
} // namespace

RenderCubeMap generateProceduralIrradianceCube(std::uint32_t faceSize)
{
    return generateProceduralIrradianceCube(faceSize, {});
}

RenderCubeMap generateProceduralIrradianceCube(
    std::uint32_t faceSize,
    const RenderEnvironmentSettings& settings)
{
    RenderCubeMap cube;
    appendCubeMip(cube, std::clamp(faceSize, 4U, 128U), 1.0F, settings);
    return cube;
}

RenderCubeMap generateProceduralPrefilteredCube(std::uint32_t baseFaceSize, std::uint32_t mipCount)
{
    return generateProceduralPrefilteredCube(baseFaceSize, mipCount, {});
}

RenderCubeMap generateProceduralPrefilteredCube(
    std::uint32_t baseFaceSize,
    std::uint32_t mipCount,
    const RenderEnvironmentSettings& settings)
{
    RenderCubeMap cube;
    const auto baseSize = std::clamp(baseFaceSize, 4U, 256U);
    const auto levels = std::clamp(mipCount, 1U, 8U);
    for (std::uint32_t mip = 0; mip < levels; ++mip) {
        const auto divisor = 1U << mip;
        const auto faceSize = std::max(baseSize / divisor, 1U);
        const auto roughness = levels <= 1U ? 1.0F : static_cast<float>(mip) / static_cast<float>(levels - 1U);
        appendCubeMip(cube, faceSize, roughness, settings);
    }
    return cube;
}

} // namespace projectunity::renderer
