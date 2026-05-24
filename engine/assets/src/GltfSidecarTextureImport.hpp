#pragma once

#include <projectunity/assets/AssetManager.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace projectunity::assets::detail {

class GltfSidecarTextureCatalog final {
public:
    GltfSidecarTextureCatalog(std::filesystem::path baseDirectory, std::string sourceStem);

    [[nodiscard]] bool applyBaseColorTexture(
        ModelAsset& model,
        MeshPrimitive& primitive,
        std::string_view meshName,
        std::string_view materialName,
        std::string* errorMessage);

private:
    [[nodiscard]] std::filesystem::path chooseBaseColorTexture(
        std::string_view meshName,
        std::string_view materialName) const;

    std::filesystem::path baseDirectory_;
    std::string sourceStem_;
    std::vector<std::filesystem::path> textureFiles_;
    std::unordered_map<std::string, std::size_t> importedTextureByPath_;
};

} // namespace projectunity::assets::detail
