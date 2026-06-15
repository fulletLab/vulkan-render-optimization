#pragma once

#include <projectunity/assets/AssetManager.hpp>
#include <projectunity/terrain/TerrainTypes.hpp>

#include <span>
#include <string>

namespace projectunity::assets {

[[nodiscard]] ModelAsset makeCubeModel(std::string name = "Cube");
[[nodiscard]] ModelAsset makeSphereModel(std::string name = "Sphere", std::uint32_t segments = 24U, std::uint32_t rings = 16U);
[[nodiscard]] ModelAsset makePlaneModel(std::string name = "Plane");
[[nodiscard]] ModelAsset makeTerrainModel(
    const terrain::TerrainGenerationResult& terrain,
    std::span<const terrain::TerrainMaterialLayer> layers,
    const IAssetManager* assetManager = nullptr,
    std::string name = "Terrain");

} // namespace projectunity::assets
