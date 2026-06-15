#pragma once

#include <projectunity/terrain/TerrainTypes.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace projectunity::assets {

struct TerrainAssetData {
    terrain::TerrainSettings settings;
    std::vector<terrain::TerrainMaterialLayer> materialLayers;
    std::vector<float> heightmap;
};

[[nodiscard]] bool saveTerrainAsset(
    const std::filesystem::path& path,
    const TerrainAssetData& asset,
    std::string* errorMessage = nullptr);
[[nodiscard]] bool loadTerrainAsset(
    const std::filesystem::path& path,
    TerrainAssetData& asset,
    std::string* errorMessage = nullptr);

} // namespace projectunity::assets
