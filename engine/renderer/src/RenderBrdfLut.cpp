#include <projectunity/renderer/RenderBrdfLut.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace projectunity::renderer {
namespace {
constexpr float kPi = 3.14159265358979323846F;

struct Vec3 {
    float x {0.0F};
    float y {0.0F};
    float z {0.0F};
};

[[nodiscard]] Vec3 operator+(Vec3 lhs, Vec3 rhs)
{
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

[[nodiscard]] Vec3 operator*(Vec3 value, float scalar)
{
    return {value.x * scalar, value.y * scalar, value.z * scalar};
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

[[nodiscard]] float radicalInverse(std::uint32_t bits)
{
    bits = (bits << 16U) | (bits >> 16U);
    bits = ((bits & 0x55555555U) << 1U) | ((bits & 0xAAAAAAAAU) >> 1U);
    bits = ((bits & 0x33333333U) << 2U) | ((bits & 0xCCCCCCCCU) >> 2U);
    bits = ((bits & 0x0F0F0F0FU) << 4U) | ((bits & 0xF0F0F0F0U) >> 4U);
    bits = ((bits & 0x00FF00FFU) << 8U) | ((bits & 0xFF00FF00U) >> 8U);
    return static_cast<float>(bits) * 2.3283064365386963e-10F;
}

[[nodiscard]] Vec3 importanceSampleGgx(float xiX, float xiY, float roughness)
{
    const auto alpha = roughness * roughness;
    const auto phi = 2.0F * kPi * xiX;
    const auto cosTheta = std::sqrt((1.0F - xiY) / std::max(1.0F + (alpha * alpha - 1.0F) * xiY, 0.0001F));
    const auto sinTheta = std::sqrt(std::max(1.0F - cosTheta * cosTheta, 0.0F));
    return {std::cos(phi) * sinTheta, std::sin(phi) * sinTheta, cosTheta};
}

[[nodiscard]] float geometrySchlickGgx(float nDotV, float roughness)
{
    const auto k = (roughness * roughness) * 0.5F;
    return nDotV / std::max(nDotV * (1.0F - k) + k, 0.0001F);
}

[[nodiscard]] std::array<float, 2> integrateBrdf(float nDotV, float roughness, std::uint32_t sampleCount)
{
    const Vec3 view {std::sqrt(std::max(1.0F - nDotV * nDotV, 0.0F)), 0.0F, nDotV};
    float scale = 0.0F;
    float bias = 0.0F;
    for (std::uint32_t sample = 0; sample < sampleCount; ++sample) {
        const auto xiX = (static_cast<float>(sample) + 0.5F) / static_cast<float>(sampleCount);
        const auto xiY = radicalInverse(sample);
        const auto halfVector = importanceSampleGgx(xiX, xiY, roughness);
        const auto light = normalize(halfVector * (2.0F * dot(view, halfVector)) + view * -1.0F);
        const auto nDotL = std::max(light.z, 0.0F);
        const auto nDotH = std::max(halfVector.z, 0.0F);
        const auto vDotH = std::max(dot(view, halfVector), 0.0F);
        if (nDotL <= 0.0F) {
            continue;
        }
        const auto geometry = geometrySchlickGgx(nDotL, roughness) * geometrySchlickGgx(nDotV, roughness);
        const auto geometryVisible = geometry * vDotH / std::max(nDotH * nDotV, 0.0001F);
        const auto fresnel = std::pow(1.0F - vDotH, 5.0F);
        scale += (1.0F - fresnel) * geometryVisible;
        bias += fresnel * geometryVisible;
    }
    const auto invSamples = 1.0F / static_cast<float>(sampleCount);
    return {scale * invSamples, bias * invSamples};
}
} // namespace

RenderBrdfLut generateBrdfIntegrationLut(std::uint32_t size, std::uint32_t sampleCount)
{
    const auto dimension = std::clamp(size, 4U, 512U);
    const auto samples = std::clamp(sampleCount, 16U, 2048U);
    RenderBrdfLut lut;
    lut.width = dimension;
    lut.height = dimension;
    lut.rgba8.resize(static_cast<std::size_t>(dimension) * dimension * 4U);
    for (std::uint32_t y = 0; y < dimension; ++y) {
        for (std::uint32_t x = 0; x < dimension; ++x) {
            const auto nDotV = (static_cast<float>(x) + 0.5F) / static_cast<float>(dimension);
            const auto roughness = (static_cast<float>(y) + 0.5F) / static_cast<float>(dimension);
            const auto integrated = integrateBrdf(nDotV, roughness, samples);
            const auto offset = (static_cast<std::size_t>(y) * dimension + x) * 4U;
            lut.rgba8[offset] = static_cast<std::uint8_t>(std::clamp(integrated[0], 0.0F, 1.0F) * 255.0F + 0.5F);
            lut.rgba8[offset + 1U] = static_cast<std::uint8_t>(std::clamp(integrated[1], 0.0F, 1.0F) * 255.0F + 0.5F);
            lut.rgba8[offset + 2U] = 0U;
            lut.rgba8[offset + 3U] = 255U;
        }
    }
    return lut;
}

} // namespace projectunity::renderer
