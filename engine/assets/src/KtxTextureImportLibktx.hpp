#pragma once

#include <projectunity/assets/AssetManager.hpp>

#include <span>
#include <string>

namespace projectunity::assets::detail {

[[nodiscard]] TextureAsset importKtx2WithLibktx(
    std::string name,
    std::span<const std::uint8_t> bytes,
    std::string* errorMessage);

} // namespace projectunity::assets::detail
