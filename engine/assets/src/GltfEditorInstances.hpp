#pragma once

#include <projectunity/assets/AssetManager.hpp>

#include <cstddef>
#include <string>
#include <string_view>

namespace projectunity::assets::detail {

[[nodiscard]] std::string gltfEditorInstanceName(
    std::string_view nodeName,
    std::string_view meshName,
    std::size_t primitiveIndex,
    std::size_t primitiveCount);

void appendMeshEditorInstance(ModelAsset& model, MeshPrimitiveInstance instance, std::string name);

} // namespace projectunity::assets::detail
