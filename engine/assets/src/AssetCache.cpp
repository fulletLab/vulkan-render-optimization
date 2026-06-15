#include <projectunity/assets/AssetManager.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <mutex>
#include <system_error>

namespace projectunity::assets {
namespace {

void setError(std::string* errorMessage, std::string message)
{
    if (errorMessage != nullptr) {
        *errorMessage = std::move(message);
    }
}

} // namespace

std::shared_ptr<const ModelAsset> AssetManager::model(AssetId id) const
{
    std::scoped_lock lock(mutex_);
    const auto it = std::find_if(models_.begin(), models_.end(), [id](const auto& asset) {
        return asset != nullptr && asset->id == id;
    });
    return it == models_.end() ? nullptr : *it;
}

std::shared_ptr<const TextureAsset> AssetManager::texture(AssetId id) const
{
    std::scoped_lock lock(mutex_);
    const auto it = std::find_if(textures_.begin(), textures_.end(), [id](const auto& asset) {
        return asset != nullptr && asset->id == id;
    });
    return it == textures_.end() ? nullptr : *it;
}

std::vector<AssetRecord> AssetManager::records() const
{
    std::scoped_lock lock(mutex_);
    return records_;
}

const std::filesystem::path& AssetManager::cacheRoot() const noexcept
{
    return cacheRoot_;
}

AssetRecord AssetManager::registerGeneratedModel(ModelAsset model)
{
    std::scoped_lock lock(mutex_);
    if (!model.id.isValid()) {
        do {
            model.id = AssetId(nextGeneratedAssetId_++);
        } while (std::any_of(models_.begin(), models_.end(), [&model](const auto& existing) {
            return existing != nullptr && existing->id == model.id;
        }));
    }

    auto stored = std::make_shared<ModelAsset>(std::move(model));
    auto existing = std::find_if(models_.begin(), models_.end(), [&stored](const auto& candidate) {
        return candidate != nullptr && candidate->id == stored->id;
    });
    if (existing == models_.end()) {
        models_.push_back(stored);
    } else {
        *existing = stored;
    }

    AssetRecord record;
    record.id = stored->id;
    record.type = AssetType::Model;
    record.displayName = stored->name;
    record.sourceName = "Generated";
    record.textureCount = stored->textures.size();
    for (const auto& primitive : stored->primitives) {
        record.vertexCount += primitive.vertices.size();
        record.indexCount += primitive.indices.size();
    }
    auto recordIt = std::find_if(records_.begin(), records_.end(), [&record](const auto& item) {
        return item.id == record.id && item.type == record.type;
    });
    if (recordIt == records_.end()) {
        records_.push_back(record);
    } else {
        *recordIt = record;
    }
    return record;
}

bool AssetManager::writeCacheRecord(const AssetRecord& record, std::string* errorMessage) const
{
    if (cacheRoot_.empty()) {
        setError(errorMessage, "Asset cache root was not configured");
        return false;
    }

    std::error_code createError;
    std::filesystem::create_directories(cacheRoot_, createError);
    if (createError) {
        setError(errorMessage, "Unable to create asset cache directory: " + createError.message());
        return false;
    }

    nlohmann::json root {
        {"version", 1},
        {"id", record.id.value()},
        {"type", toString(record.type)},
        {"displayName", record.displayName},
        {"sourceName", record.sourceName},
        {"vertices", record.vertexCount},
        {"indices", record.indexCount},
        {"textures", record.textureCount},
    };

    std::ofstream cacheFile(cacheRoot_ / record.cacheFile, std::ios::binary | std::ios::trunc);
    if (!cacheFile) {
        setError(errorMessage, "Unable to open asset cache record");
        return false;
    }

    cacheFile << root.dump(2);
    if (!cacheFile.good()) {
        setError(errorMessage, "Unable to write asset cache record");
        return false;
    }
    return true;
}

void AssetManager::storeRecord(const AssetRecord& record)
{
    std::scoped_lock lock(mutex_);
    auto it = std::find_if(records_.begin(), records_.end(), [&record](const auto& item) {
        return item.id == record.id && item.type == record.type;
    });
    if (it == records_.end()) {
        records_.push_back(record);
        return;
    }
    *it = record;
}

const char* toString(AssetType type) noexcept
{
    switch (type) {
    case AssetType::Texture2D:
        return "Texture2D";
    case AssetType::Model:
        return "Model";
    }
    return "Unknown";
}

} // namespace projectunity::assets
