#pragma once

#include <projectunity/renderer/RendererTypes.hpp>

#include <cstdint>
#include <vector>

namespace projectunity::renderer {

struct RenderCubeMip {
    std::uint32_t faceSize {0};
    std::vector<std::uint8_t> rgba8;
};

struct RenderCubeMap {
    std::vector<RenderCubeMip> mips;
};

[[nodiscard]] RenderCubeMap generateProceduralIrradianceCube(std::uint32_t faceSize);
[[nodiscard]] RenderCubeMap generateProceduralIrradianceCube(
    std::uint32_t faceSize,
    const RenderEnvironmentSettings& settings);
[[nodiscard]] RenderCubeMap generateProceduralPrefilteredCube(std::uint32_t baseFaceSize, std::uint32_t mipCount);
[[nodiscard]] RenderCubeMap generateProceduralPrefilteredCube(
    std::uint32_t baseFaceSize,
    std::uint32_t mipCount,
    const RenderEnvironmentSettings& settings);

} // namespace projectunity::renderer
