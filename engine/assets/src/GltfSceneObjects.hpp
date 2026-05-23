#pragma once

#include "GltfNodeTransforms.hpp"

#include <projectunity/assets/AssetManager.hpp>

#include <string>
#include <vector>

namespace tinygltf {
class Model;
} // namespace tinygltf

namespace projectunity::assets::detail {

[[nodiscard]] bool importGltfSceneObjects(
    const tinygltf::Model& gltf,
    int nodeIndex,
    GltfMatrix4 parentTransform,
    std::vector<ImportedLightAsset>& lights,
    std::vector<ImportedCameraAsset>& cameras,
    std::string* errorMessage,
    int depth = 0);

} // namespace projectunity::assets::detail
