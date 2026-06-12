#include <projectunity/renderer/RenderShadowCache.hpp>

#include <projectunity/renderer/RenderDrawOrdering.hpp>

#include <bit>
#include <cstdint>

namespace projectunity::renderer {
namespace {

[[nodiscard]] std::uint64_t mixHash(std::uint64_t seed, std::uint64_t value) noexcept
{
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
    return seed;
}

[[nodiscard]] std::uint64_t floatBits(float value) noexcept
{
    return std::bit_cast<std::uint32_t>(value);
}

[[nodiscard]] std::uint64_t hashMatrix(std::uint64_t hash, const RenderMatrix4& matrix) noexcept
{
    for (const auto value : matrix.values) {
        hash = mixHash(hash, floatBits(value));
    }
    return hash;
}

[[nodiscard]] std::uint64_t pointerBits(const void* pointer) noexcept
{
    return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(pointer));
}

} // namespace

std::uint64_t renderShadowContentSignature(const RenderFrame& frame) noexcept
{
    auto hash = std::uint64_t {0xcbf29ce484222325ULL};
    hash = mixHash(hash, frame.shadowsEnabled ? 1U : 0U);
    hash = mixHash(hash, static_cast<std::uint64_t>(frame.shadowMode));
    hash = mixHash(hash, frame.shadowLightIndex);
    hash = mixHash(hash, frame.shadowViewCount);
    hash = mixHash(hash, frame.shadowCascadeCount);
    hash = mixHash(hash, floatBits(frame.shadowDepthFarPlane));
    hash = hashMatrix(hash, frame.shadowViewProjection);
    for (std::uint32_t index = 0; index < frame.shadowViewCount && index < frame.shadowViewProjections.size(); ++index) {
        hash = hashMatrix(hash, frame.shadowViewProjections[index]);
    }
    for (std::uint32_t index = 0; index < frame.shadowCascadeCount && index < frame.shadowCascadeSplits.size(); ++index) {
        hash = mixHash(hash, floatBits(frame.shadowCascadeSplits[index]));
    }

    for (const auto& draw : frame.shadowMeshDraws) {
        if (!draw.castsShadow || isTransparentMeshDraw(draw)) {
            continue;
        }
        hash = mixHash(hash, draw.modelAssetId.value());
        hash = mixHash(hash, draw.primitiveIndex);
        hash = mixHash(hash, draw.lodIndex);
        hash = mixHash(hash, draw.renderInstanceId);
        hash = mixHash(hash, draw.sceneNodeId);
        hash = mixHash(hash, pointerBits(draw.primitive));
        hash = mixHash(hash, pointerBits(draw.material));
        hash = mixHash(hash, pointerBits(draw.baseColorTexture));
        hash = hashMatrix(hash, draw.modelMatrix);
        for (const auto value : draw.worldBoundsCenter) {
            hash = mixHash(hash, floatBits(value));
        }
        hash = mixHash(hash, floatBits(draw.worldBoundsRadius));
        hash = mixHash(hash, draw.flipsWinding ? 1U : 0U);
        if (draw.material != nullptr) {
            hash = mixHash(hash, static_cast<std::uint64_t>(draw.material->alphaMode));
            hash = mixHash(hash, floatBits(draw.material->alphaCutoff));
        }
    }
    return hash == 0U ? 1U : hash;
}

} // namespace projectunity::renderer
