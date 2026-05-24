#pragma once

#include <projectunity/assets/AssetManager.hpp>

#include <cstddef>
#include <cstdint>

namespace projectunity::editor {

[[nodiscard]] std::size_t indexCountForViewportLod(
    const assets::MeshPrimitive& primitive,
    std::uint32_t lodIndex);

[[nodiscard]] std::uint32_t selectViewportMeshLod(
    const assets::MeshPrimitive& primitive,
    float boundsRadius,
    float depth,
    float verticalFovRadians,
    float viewportHeight) noexcept;

} // namespace projectunity::editor
