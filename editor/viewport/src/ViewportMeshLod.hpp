#pragma once

#include <projectunity/assets/AssetManager.hpp>
#include <projectunity/math/Vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace projectunity::editor {

[[nodiscard]] float viewportLodDistanceToBounds(
    math::Vec3 eye,
    math::Vec3 forward,
    const std::array<math::Vec3, 8>& boundsCorners,
    float nearPlane) noexcept;

[[nodiscard]] std::size_t indexCountForViewportLod(
    const assets::MeshPrimitive& primitive,
    std::uint32_t lodIndex);

[[nodiscard]] std::uint32_t selectViewportMeshLod(
    const assets::MeshPrimitive& primitive,
    float boundsRadius,
    float depth,
    float verticalFovRadians,
    float viewportHeight,
    bool forceFullResolution = false) noexcept;

} // namespace projectunity::editor
