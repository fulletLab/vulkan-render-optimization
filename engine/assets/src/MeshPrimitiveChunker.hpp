#pragma once

#include <projectunity/assets/AssetManager.hpp>

namespace projectunity::assets::detail {

void splitLargePrimitivesIntoSpatialChunks(ModelAsset& model);

} // namespace projectunity::assets::detail
