#include <projectunity/assets/TerrainAsset.hpp>

#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace projectunity::assets {
namespace {

constexpr int kTerrainAssetVersion = 1;

void setError(std::string* errorMessage, std::string message)
{
    if (errorMessage != nullptr) {
        *errorMessage = std::move(message);
    }
}

[[nodiscard]] const char* noiseTypeName(terrain::TerrainNoiseType type) noexcept
{
    return type == terrain::TerrainNoiseType::Ridged ? "ridged" : "value";
}

[[nodiscard]] bool readNoiseType(const nlohmann::json& value, terrain::TerrainNoiseType& output)
{
    if (!value.is_string()) {
        return false;
    }
    const auto name = value.get<std::string>();
    if (name == "value") {
        output = terrain::TerrainNoiseType::Value;
        return true;
    }
    if (name == "ridged") {
        output = terrain::TerrainNoiseType::Ridged;
        return true;
    }
    return false;
}

[[nodiscard]] nlohmann::json settingsJson(const terrain::TerrainSettings& settings)
{
    return {
        {"width", settings.width},
        {"length", settings.length},
        {"heightScale", settings.heightScale},
        {"resolution", settings.resolution},
        {"chunkSize", settings.chunkSize},
        {"seed", settings.seed},
        {"noiseType", noiseTypeName(settings.noiseType)},
        {"frequency", settings.frequency},
        {"octaves", settings.octaves},
        {"persistence", settings.persistence},
        {"lacunarity", settings.lacunarity},
        {"generateNormals", settings.generateNormals},
        {"generateTangents", settings.generateTangents},
        {"lodLevels", settings.lodLevels},
        {"generateCollider", settings.generateCollider},
    };
}

[[nodiscard]] bool readSettings(const nlohmann::json& value, terrain::TerrainSettings& settings)
{
    if (!value.is_object() || !readNoiseType(value.at("noiseType"), settings.noiseType)) {
        return false;
    }
    settings.width = value.value("width", settings.width);
    settings.length = value.value("length", settings.length);
    settings.heightScale = value.value("heightScale", settings.heightScale);
    settings.resolution = value.value("resolution", settings.resolution);
    settings.chunkSize = value.value("chunkSize", settings.chunkSize);
    settings.seed = value.value("seed", settings.seed);
    settings.frequency = value.value("frequency", settings.frequency);
    settings.octaves = value.value("octaves", settings.octaves);
    settings.persistence = value.value("persistence", settings.persistence);
    settings.lacunarity = value.value("lacunarity", settings.lacunarity);
    settings.generateNormals = value.value("generateNormals", settings.generateNormals);
    settings.generateTangents = value.value("generateTangents", settings.generateTangents);
    settings.lodLevels = value.value("lodLevels", settings.lodLevels);
    settings.generateCollider = value.value("generateCollider", settings.generateCollider);
    return settings.width > 0.0F && settings.length > 0.0F && settings.heightScale >= 0.0F
        && settings.resolution >= 2U && settings.chunkSize > 0U && settings.octaves > 0U;
}

[[nodiscard]] nlohmann::json layerJson(const terrain::TerrainMaterialLayer& layer)
{
    return {
        {"name", layer.name},
        {"baseColorTextureId", layer.baseColorTextureId.value()},
        {"normalTextureId", layer.normalTextureId.value()},
        {"metallic", layer.metallic},
        {"roughness", layer.roughness},
        {"tiling", layer.tiling},
        {"strength", layer.strength},
        {"heightRange", {layer.heightRange[0], layer.heightRange[1]}},
        {"slopeRange", {layer.slopeRange[0], layer.slopeRange[1]}},
    };
}

[[nodiscard]] bool readRange(const nlohmann::json& value, std::array<float, 2>& output)
{
    if (!value.is_array() || value.size() != 2U || !value[0].is_number() || !value[1].is_number()) {
        return false;
    }
    output = {value[0].get<float>(), value[1].get<float>()};
    return output[0] <= output[1];
}

[[nodiscard]] bool readLayer(const nlohmann::json& value, terrain::TerrainMaterialLayer& layer)
{
    if (!value.is_object()) {
        return false;
    }
    layer.name = value.value("name", std::string {"Layer"});
    layer.baseColorTextureId = core::StableId(value.value("baseColorTextureId", std::uint64_t {0}));
    layer.normalTextureId = core::StableId(value.value("normalTextureId", std::uint64_t {0}));
    layer.metallic = value.value("metallic", layer.metallic);
    layer.roughness = value.value("roughness", layer.roughness);
    layer.tiling = value.value("tiling", layer.tiling);
    layer.strength = value.value("strength", layer.strength);
    return readRange(value.at("heightRange"), layer.heightRange)
        && readRange(value.at("slopeRange"), layer.slopeRange)
        && !layer.name.empty() && layer.tiling > 0.0F && layer.strength >= 0.0F;
}

} // namespace

bool saveTerrainAsset(const std::filesystem::path& path, const TerrainAssetData& asset, std::string* errorMessage)
{
    try {
        nlohmann::json root {
            {"version", kTerrainAssetVersion},
            {"settings", settingsJson(asset.settings)},
            {"materialLayers", nlohmann::json::array()},
            {"heightmap", asset.heightmap},
        };
        for (const auto& layer : asset.materialLayers) {
            root["materialLayers"].push_back(layerJson(layer));
        }
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file) {
            setError(errorMessage, "Unable to open terrain asset for writing");
            return false;
        }
        file << root.dump(2);
        if (!file.good()) {
            setError(errorMessage, "Unable to write terrain asset");
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        setError(errorMessage, exception.what());
        return false;
    }
}

bool loadTerrainAsset(const std::filesystem::path& path, TerrainAssetData& asset, std::string* errorMessage)
{
    try {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            setError(errorMessage, "Unable to open terrain asset for reading");
            return false;
        }
        std::ostringstream buffer;
        buffer << file.rdbuf();
        const auto root = nlohmann::json::parse(buffer.str());
        if (!root.is_object() || root.value("version", 0) != kTerrainAssetVersion) {
            setError(errorMessage, "Unsupported terrain asset version");
            return false;
        }
        TerrainAssetData loaded;
        if (!readSettings(root.at("settings"), loaded.settings) || !root.at("materialLayers").is_array()) {
            setError(errorMessage, "Terrain asset settings are invalid");
            return false;
        }
        for (const auto& value : root.at("materialLayers")) {
            terrain::TerrainMaterialLayer layer;
            if (!readLayer(value, layer)) {
                setError(errorMessage, "Terrain material layer is invalid");
                return false;
            }
            loaded.materialLayers.push_back(std::move(layer));
        }
        if (root.contains("heightmap")) {
            if (!root.at("heightmap").is_array()) {
                setError(errorMessage, "Terrain heightmap must be an array");
                return false;
            }
            loaded.heightmap = root.at("heightmap").get<std::vector<float>>();
            const auto expectedHeightCount = static_cast<std::size_t>(loaded.settings.resolution)
                * loaded.settings.resolution;
            if (!loaded.heightmap.empty() && loaded.heightmap.size() != expectedHeightCount) {
                setError(errorMessage, "Terrain heightmap size does not match resolution");
                return false;
            }
        }
        if (loaded.materialLayers.size() > 8U) {
            setError(errorMessage, "Terrain asset supports at most eight material layers");
            return false;
        }
        asset = std::move(loaded);
        return true;
    } catch (const std::exception& exception) {
        setError(errorMessage, exception.what());
        return false;
    }
}

} // namespace projectunity::assets
