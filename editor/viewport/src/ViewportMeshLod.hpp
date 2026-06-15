#pragma once

#include <projectunity/assets/AssetManager.hpp>
#include <projectunity/math/Vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace projectunity::editor {

enum class ViewportMeshLodReason : std::uint8_t {
    FullResolution,
    ScreenError,
    HysteresisHold,
};

struct ViewportMeshLodSelection {
    std::uint32_t lodIndex {0};
    std::uint32_t previousLodIndex {0};
    float projectedErrorPixels {0.0F};
    bool hadPreviousSelection {false};
    bool hysteresisActive {false};
    ViewportMeshLodReason reason {ViewportMeshLodReason::FullResolution};
};

[[nodiscard]] float viewportLodDistanceToBounds(
    math::Vec3 eye,
    math::Vec3 forward,
    const std::array<math::Vec3, 8>& boundsCorners,
    float nearPlane) noexcept;

[[nodiscard]] std::size_t indexCountForViewportLod(
    const assets::MeshPrimitive& primitive,
    std::uint32_t lodIndex);

[[nodiscard]] ViewportMeshLodSelection evaluateViewportMeshLod(
    const assets::MeshPrimitive& primitive,
    float boundsRadius,
    float distanceToCenter,
    float verticalFovRadians,
    float viewportHeight,
    bool forceFullResolution = false,
    float lodBias = 1.0F,
    std::optional<std::uint32_t> previousLodIndex = std::nullopt,
    float hysteresisRatio = 0.15F) noexcept;

[[nodiscard]] std::uint32_t selectViewportMeshLod(
    const assets::MeshPrimitive& primitive,
    float boundsRadius,
    float depth,
    float verticalFovRadians,
    float viewportHeight,
    bool forceFullResolution = false,
    float lodBias = 1.0F) noexcept;

} // namespace projectunity::editor
