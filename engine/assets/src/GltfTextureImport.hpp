#pragma once

#include <projectunity/assets/AssetManager.hpp>

#include <filesystem>
#include <string>

namespace tinygltf {
class Image;
class Model;
}

namespace projectunity::assets::detail {

[[nodiscard]] bool importGltfTexture(
    const tinygltf::Model& gltf,
    const tinygltf::Image& image,
    std::string name,
    const std::filesystem::path& baseDirectory,
    TextureAsset& output,
    std::string* errorMessage);

} // namespace projectunity::assets::detail
