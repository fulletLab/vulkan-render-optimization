#pragma once

#include <projectunity/assets/AssetManager.hpp>

namespace projectunity::assets::detail {

void rebuildSimplificationLods(MeshPrimitive& primitive);
void rebuildMissingSimplificationLods(ModelAsset& model);

} // namespace projectunity::assets::detail
