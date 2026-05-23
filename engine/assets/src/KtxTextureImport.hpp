#pragma once

#include <projectunity/assets/AssetManager.hpp>

#include <filesystem>
#include <span>
#include <string>

namespace projectunity::assets::detail {

[[nodiscard]] bool isKtxTextureExtension(const std::filesystem::path& path);

[[nodiscard]] TextureAsset importKtxTexture(
    const std::filesystem::path& sourcePath,
    std::span<const std::uint8_t> bytes,
    std::string* errorMessage);

[[nodiscard]] TextureAsset importKtxTextureFromMemory(
    std::string name,
    std::span<const std::uint8_t> bytes,
    std::string* errorMessage);

} // namespace projectunity::assets::detail
