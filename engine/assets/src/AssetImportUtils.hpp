#pragma once

#include <projectunity/assets/AssetManager.hpp>

#include <array>
#include <cstdint>
#include <fstream>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace projectunity::assets::detail {

constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

inline void setError(std::string* errorMessage, std::string message)
{
    if (errorMessage != nullptr) {
        *errorMessage = std::move(message);
    }
}

[[nodiscard]] inline std::vector<std::uint8_t> readBytes(const std::filesystem::path& path, std::string* errorMessage)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        setError(errorMessage, "Unable to open asset source file");
        return {};
    }

    file.seekg(0, std::ios::end);
    const auto end = file.tellg();
    if (end < 0) {
        setError(errorMessage, "Unable to measure asset source file");
        return {};
    }
    file.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    if (bytes.empty()) {
        setError(errorMessage, "Asset source file is empty");
        return {};
    }
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file.good() && !file.eof()) {
        setError(errorMessage, "Unable to read asset source file");
        return {};
    }
    return bytes;
}

[[nodiscard]] inline std::uint64_t hashBytes(std::span<const std::uint8_t> bytes, std::uint64_t seed)
{
    auto hash = seed;
    for (const auto byte : bytes) {
        hash ^= byte;
        hash *= kFnvPrime;
    }
    return hash == 0 ? 1 : hash;
}

[[nodiscard]] inline AssetId makeId(std::span<const std::uint8_t> bytes, AssetType type)
{
    const std::array<std::uint8_t, 1> typeBytes {static_cast<std::uint8_t>(type)};
    return AssetId(hashBytes(bytes, hashBytes(typeBytes, kFnvOffsetBasis)));
}

[[nodiscard]] inline AssetId makeTextureId(const TextureAsset& texture)
{
    auto hash = hashBytes(texture.rgba8, hashBytes(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(texture.name.data()), texture.name.size()),
        kFnvOffsetBasis));
    hash = hashBytes(
        std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(texture.rgba32f.data()),
            texture.rgba32f.size() * sizeof(float)),
        hash);
    for (const auto& mip : texture.gpuMipLevels) {
        hash = hashBytes(mip.bytes, hash);
    }
    const std::array<std::uint8_t, 8> dimensionBytes {{
        static_cast<std::uint8_t>(texture.width & 0xffU),
        static_cast<std::uint8_t>((texture.width >> 8U) & 0xffU),
        static_cast<std::uint8_t>((texture.width >> 16U) & 0xffU),
        static_cast<std::uint8_t>((texture.width >> 24U) & 0xffU),
        static_cast<std::uint8_t>(texture.height & 0xffU),
        static_cast<std::uint8_t>((texture.height >> 8U) & 0xffU),
        static_cast<std::uint8_t>((texture.height >> 16U) & 0xffU),
        static_cast<std::uint8_t>((texture.height >> 24U) & 0xffU),
    }};
    hash = hashBytes(dimensionBytes, hash);
    const std::array<std::uint8_t, 2> formatBytes {{
        static_cast<std::uint8_t>(static_cast<std::uint16_t>(texture.gpuFormat) & 0xffU),
        static_cast<std::uint8_t>((static_cast<std::uint16_t>(texture.gpuFormat) >> 8U) & 0xffU),
    }};
    hash = hashBytes(formatBytes, hash);
    return AssetId(hash == 0 ? 1 : hash);
}

} // namespace projectunity::assets::detail
