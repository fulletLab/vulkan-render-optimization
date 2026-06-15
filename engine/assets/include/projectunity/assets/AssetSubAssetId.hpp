#pragma once

#include <projectunity/core/StableId.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace projectunity::assets {

enum class SubAssetKind : std::uint8_t {
    Mesh = 1,
    Material = 2,
    Texture = 3,
    Node = 4,
};

[[nodiscard]] constexpr std::uint64_t makeSubAssetId(
    core::StableId assetId,
    SubAssetKind kind,
    std::uint32_t index) noexcept
{
    constexpr std::uint64_t offset = 1469598103934665603ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    auto value = offset;
    const std::array<std::uint64_t, 3> parts {
        assetId.value(),
        static_cast<std::uint64_t>(kind),
        static_cast<std::uint64_t>(index) + 1U,
    };
    for (const auto part : parts) {
        for (std::size_t byte = 0; byte < sizeof(part); ++byte) {
            value ^= (part >> (byte * 8U)) & 0xffU;
            value *= prime;
        }
    }
    return value == 0U ? 1U : value;
}

} // namespace projectunity::assets
