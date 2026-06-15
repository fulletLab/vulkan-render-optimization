#include <projectunity/assets/GeneratedModelBuilder.hpp>
#include <projectunity/assets/TerrainAsset.hpp>
#include <projectunity/terrain/TerrainGenerator.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace {

int fail(const char* message)
{
    std::cerr << message << '\n';
    return EXIT_FAILURE;
}

} // namespace

int main()
{
    using namespace projectunity;

    const auto cube = assets::makeCubeModel();
    const auto sphere = assets::makeSphereModel("Sphere", 8U, 4U);
    const auto plane = assets::makePlaneModel();
    if (cube.primitives.size() != 1U || cube.primitives.front().vertices.size() != 24U
        || sphere.primitives.front().indices.empty() || plane.primitives.front().indices.size() != 6U) {
        return fail("generated primitive model geometry is invalid");
    }

    terrain::TerrainSettings settings;
    settings.resolution = 9U;
    settings.chunkSize = 4U;
    const auto generatedTerrain = terrain::TerrainGenerator::generate(settings);
    const std::vector<terrain::TerrainMaterialLayer> layers {
        terrain::TerrainMaterialLayer {"Grass"},
        terrain::TerrainMaterialLayer {"Rock"},
    };
    auto terrainModel = assets::makeTerrainModel(generatedTerrain, layers);
    if (terrainModel.primitives.size() != generatedTerrain.chunks.size() || terrainModel.materials.size() != 2U
        || terrainModel.primitives.front().lods.empty()) {
        return fail("terrain model did not preserve chunks, LODs, and material layers");
    }

    assets::AssetManager manager(std::filesystem::temp_directory_path() / "projectunity_generated_asset_test");
    const auto record = manager.registerGeneratedModel(std::move(terrainModel));
    const auto stored = manager.model(record.id);
    if (!record.id.isValid() || stored == nullptr || stored->primitives.size() != 4U
        || record.sourceName != "Generated") {
        return fail("generated model registration failed");
    }

    const auto assetPath = std::filesystem::temp_directory_path() / "projectunity_terrain_test.terrain.json";
    assets::TerrainAssetData terrainAsset {settings, layers, generatedTerrain.heightmap};
    std::string error;
    if (!assets::saveTerrainAsset(assetPath, terrainAsset, &error)) {
        return fail("terrain asset save failed");
    }
    assets::TerrainAssetData loadedTerrainAsset;
    if (!assets::loadTerrainAsset(assetPath, loadedTerrainAsset, &error)
        || loadedTerrainAsset.settings != settings || loadedTerrainAsset.materialLayers != layers) {
        return fail("terrain asset roundtrip failed");
    }
    std::filesystem::remove(assetPath);

    return EXIT_SUCCESS;
}
