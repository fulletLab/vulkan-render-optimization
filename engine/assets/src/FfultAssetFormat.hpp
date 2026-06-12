#pragma once

#include <projectunity/assets/AssetManager.hpp>

#include <filesystem>
#include <memory>
#include <span>
#include <string>

namespace projectunity::assets::detail {

struct FfultModelAsset {
    std::shared_ptr<ModelAsset> asset;
    AssetRecord record;
};

struct FfultTextureAsset {
    std::shared_ptr<TextureAsset> asset;
    AssetRecord record;
};

[[nodiscard]] bool isFfultAssetExtension(const std::filesystem::path& path);
[[nodiscard]] bool writeFfultModel(
    const std::filesystem::path& path,
    const ModelAsset& asset,
    std::string* errorMessage);
[[nodiscard]] bool writeFfultTexture(
    const std::filesystem::path& path,
    const TextureAsset& asset,
    std::string* errorMessage);
[[nodiscard]] AssetType readFfultAssetType(std::span<const std::uint8_t> bytes, std::string* errorMessage);
[[nodiscard]] FfultModelAsset readFfultModel(
    const std::filesystem::path& sourcePath,
    std::span<const std::uint8_t> bytes,
    std::string* errorMessage);
[[nodiscard]] FfultTextureAsset readFfultTexture(
    const std::filesystem::path& sourcePath,
    std::span<const std::uint8_t> bytes,
    std::string* errorMessage);

} // namespace projectunity::assets::detail
