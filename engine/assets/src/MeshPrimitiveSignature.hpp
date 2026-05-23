#pragma once

#include <projectunity/assets/AssetManager.hpp>

#include <cstdint>

namespace projectunity::assets::detail {

[[nodiscard]] std::uint64_t meshPrimitiveSignature(const MeshPrimitive& primitive) noexcept;
[[nodiscard]] bool meshPrimitivesEqual(const MeshPrimitive& lhs, const MeshPrimitive& rhs) noexcept;

} // namespace projectunity::assets::detail
