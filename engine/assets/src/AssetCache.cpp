#include <projectunity/assets/AssetManager.hpp>
#include <projectunity/core/Log.hpp>

#include "AssetImportUtils.hpp"
#include "FfultAssetFormat.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <mutex>
#include <optional>
#include <system_error>

namespace projectunity::assets {
namespace {

void setError(std::string* errorMessage, std::string message)
{
    if (errorMessage != nullptr) {
        *errorMessage = std::move(message);
    }
}

[[nodiscard]] std::optional<AssetType> assetTypeFromString(const std::string& type)
{
    if (type == "Model") {
        return AssetType::Model;
    }
    if (type == "Texture2D") {
        return AssetType::Texture2D;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<AssetRecord> readCacheRecord(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(file, nullptr, true, true);
    } catch (const std::exception& exception) {
        core::logWarning(core::LogCategory::Assets, std::string("Ignoring invalid asset cache record: ") + exception.what());
        return std::nullopt;
    }

    const auto type = assetTypeFromString(root.value("type", std::string {}));
    AssetRecord record;
    record.id = AssetId(root.value("id", std::uint64_t {0}));
    if (!record.id.isValid() || !type.has_value()) {
        core::logWarning(core::LogCategory::Assets, "Ignoring asset cache record with invalid id or type");
        return std::nullopt;
    }

    record.type = *type;
    record.displayName = root.value("displayName", std::string {});
    record.sourceName = root.value("sourceName", std::string {});
    record.cacheFile = path.filename().string();
    record.vertexCount = root.value("vertices", std::size_t {0});
    record.indexCount = root.value("indices", std::size_t {0});
    record.textureCount = root.value("textures", std::size_t {0});
    if (record.displayName.empty()) {
        record.displayName = record.sourceName.empty() ? std::to_string(record.id.value()) : record.sourceName;
    }
    return record;
}

} // namespace

std::shared_ptr<const ModelAsset> AssetManager::model(AssetId id) const
{
    {
        std::scoped_lock lock(mutex_);
        const auto it = std::find_if(models_.begin(), models_.end(), [id](const auto& asset) {
            return asset != nullptr && asset->id == id;
        });
        if (it != models_.end()) {
            return *it;
        }
    }
    return loadCachedModel(id);
}

std::shared_ptr<const TextureAsset> AssetManager::texture(AssetId id) const
{
    {
        std::scoped_lock lock(mutex_);
        const auto it = std::find_if(textures_.begin(), textures_.end(), [id](const auto& asset) {
            return asset != nullptr && asset->id == id;
        });
        if (it != textures_.end()) {
            return *it;
        }
    }
    return loadCachedTexture(id);
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

void AssetManager::loadCacheRecords()
{
    if (cacheRoot_.empty()) {
        return;
    }

    std::error_code error;
    if (!std::filesystem::exists(cacheRoot_, error) || error) {
        return;
    }

    std::vector<AssetRecord> loaded;
    for (const auto& entry : std::filesystem::directory_iterator(cacheRoot_, error)) {
        if (error) {
            core::logWarning(core::LogCategory::Assets, "Asset cache scan stopped: " + error.message());
            break;
        }
        if (!entry.is_regular_file(error) || entry.path().extension() != ".json") {
            continue;
        }
        if (auto record = readCacheRecord(entry.path())) {
            loaded.push_back(std::move(*record));
        }
    }

    std::sort(loaded.begin(), loaded.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.displayName == rhs.displayName
            ? lhs.id.value() < rhs.id.value()
            : lhs.displayName < rhs.displayName;
    });

    std::scoped_lock lock(mutex_);
    records_ = std::move(loaded);
}

std::shared_ptr<const ModelAsset> AssetManager::loadCachedModel(AssetId id) const
{
    const auto path = cacheRoot_ / (std::to_string(id.value()) + ".ffult");
    if (!id.isValid() || !std::filesystem::exists(path)) {
        return nullptr;
    }

    std::string error;
    const auto bytes = detail::readBytes(path, &error);
    auto imported = bytes.empty() ? detail::FfultModelAsset {} : detail::readFfultModel(path, bytes, &error);
    if (imported.asset == nullptr || imported.asset->id != id) {
        core::logWarning(core::LogCategory::Assets, error.empty() ? "Unable to load cached model asset" : error);
        return nullptr;
    }

    std::scoped_lock lock(mutex_);
    auto existing = std::find_if(models_.begin(), models_.end(), [&imported](const auto& model) {
        return model != nullptr && model->id == imported.asset->id;
    });
    if (existing == models_.end()) {
        models_.push_back(imported.asset);
    } else {
        *existing = imported.asset;
    }
    auto record = imported.record;
    record.cacheFile = std::to_string(record.id.value()) + ".asset.json";
    auto recordIt = std::find_if(records_.begin(), records_.end(), [&record](const auto& item) {
        return item.id == record.id && item.type == record.type;
    });
    if (recordIt == records_.end()) {
        records_.push_back(std::move(record));
    }
    return imported.asset;
}

std::shared_ptr<const TextureAsset> AssetManager::loadCachedTexture(AssetId id) const
{
    const auto path = cacheRoot_ / (std::to_string(id.value()) + ".ffult");
    if (!id.isValid() || !std::filesystem::exists(path)) {
        return nullptr;
    }

    std::string error;
    const auto bytes = detail::readBytes(path, &error);
    auto imported = bytes.empty() ? detail::FfultTextureAsset {} : detail::readFfultTexture(path, bytes, &error);
    if (imported.asset == nullptr || imported.asset->id != id) {
        core::logWarning(core::LogCategory::Assets, error.empty() ? "Unable to load cached texture asset" : error);
        return nullptr;
    }

    std::scoped_lock lock(mutex_);
    auto existing = std::find_if(textures_.begin(), textures_.end(), [&imported](const auto& texture) {
        return texture != nullptr && texture->id == imported.asset->id;
    });
    if (existing == textures_.end()) {
        textures_.push_back(imported.asset);
    } else {
        *existing = imported.asset;
    }
    auto record = imported.record;
    record.cacheFile = std::to_string(record.id.value()) + ".asset.json";
    auto recordIt = std::find_if(records_.begin(), records_.end(), [&record](const auto& item) {
        return item.id == record.id && item.type == record.type;
    });
    if (recordIt == records_.end()) {
        records_.push_back(std::move(record));
    }
    return imported.asset;
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
