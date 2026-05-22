#pragma once

#include <projectunity/assets/AssetManager.hpp>

#include <filesystem>
#include <span>
#include <string>

namespace projectunity::assets::detail {

[[nodiscard]] TextureAsset importStbTexture(
    const std::filesystem::path& sourcePath,
    std::span<const std::uint8_t> sourceBytes,
    std::string* errorMessage);

} // namespace projectunity::assets::detail
