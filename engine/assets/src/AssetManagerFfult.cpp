#include <projectunity/assets/AssetManager.hpp>

#include "AssetImportUtils.hpp"
#include "FfultAssetFormat.hpp"

#include <projectunity/core/Log.hpp>

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace projectunity::assets {
namespace {

void reportProgress(const AssetImportProgressCallback& progress, int percent, std::string stage)
{
    if (progress) {
        progress({std::clamp(percent, 1, 100), std::move(stage)});
    }
}

[[nodiscard]] std::filesystem::path ffultCachePath(const std::filesystem::path& cacheRoot, AssetId id)
{
    return cacheRoot / (std::to_string(id.value()) + ".ffult");
}

} // namespace

bool AssetManager::writeFfultModelCache(const ModelAsset& asset, std::string* errorMessage) const
{
    if (cacheRoot_.empty()) {
        detail::setError(errorMessage, "Asset cache root was not configured");
        return false;
    }
    return detail::writeFfultModel(ffultCachePath(cacheRoot_, asset.id), asset, errorMessage);
}

bool AssetManager::writeFfultTextureCache(const TextureAsset& asset, std::string* errorMessage) const
{
    if (cacheRoot_.empty()) {
        detail::setError(errorMessage, "Asset cache root was not configured");
        return false;
    }
    return detail::writeFfultTexture(ffultCachePath(cacheRoot_, asset.id), asset, errorMessage);
}

std::optional<AssetImportResult> AssetManager::tryImportFfultModelCache(
    const std::filesystem::path& sourcePath,
    AssetId sourceId,
    const AssetImportProgressCallback& progress)
{
    const auto path = ffultCachePath(cacheRoot_, sourceId);
    if (!std::filesystem::exists(path)) {
        return std::nullopt;
    }
    std::string error;
    reportProgress(progress, 18, "Loading FFULT model cache");
    const auto bytes = detail::readBytes(path, &error);
    auto imported = bytes.empty() ? detail::FfultModelAsset {} : detail::readFfultModel(sourcePath, bytes, &error);
    if (imported.asset == nullptr || imported.asset->id != sourceId) {
        core::logWarning(core::LogCategory::Assets, error.empty() ? "Ignoring invalid FFULT model cache" : error);
        return std::nullopt;
    }
    if (!writeCacheRecord(imported.record, &error)) {
        core::logError(core::LogCategory::Assets, error);
        return AssetImportResult {false, {}, std::move(error)};
    }
    {
        std::scoped_lock lock(mutex_);
        auto existing = std::find_if(models_.begin(), models_.end(), [&imported](const auto& model) {
            return model != nullptr && model->id == imported.asset->id;
        });
        if (existing == models_.end()) {
            models_.push_back(imported.asset);
        } else {
            *existing = imported.asset;
        }
    }
    storeRecord(imported.record);
    reportProgress(progress, 100, "FFULT model cache loaded");
    return AssetImportResult {true, imported.record, {}};
}

std::optional<AssetImportResult> AssetManager::tryImportFfultTextureCache(
    const std::filesystem::path& sourcePath,
    AssetId sourceId,
    const AssetImportProgressCallback& progress)
{
    const auto path = ffultCachePath(cacheRoot_, sourceId);
    if (!std::filesystem::exists(path)) {
        return std::nullopt;
    }
    std::string error;
    reportProgress(progress, 18, "Loading FFULT texture cache");
    const auto bytes = detail::readBytes(path, &error);
    auto imported = bytes.empty() ? detail::FfultTextureAsset {} : detail::readFfultTexture(sourcePath, bytes, &error);
    if (imported.asset == nullptr || imported.asset->id != sourceId) {
        core::logWarning(core::LogCategory::Assets, error.empty() ? "Ignoring invalid FFULT texture cache" : error);
        return std::nullopt;
    }
    if (!writeCacheRecord(imported.record, &error)) {
        core::logError(core::LogCategory::Assets, error);
        return AssetImportResult {false, {}, std::move(error)};
    }
    {
        std::scoped_lock lock(mutex_);
        auto existing = std::find_if(textures_.begin(), textures_.end(), [&imported](const auto& texture) {
            return texture != nullptr && texture->id == imported.asset->id;
        });
        if (existing == textures_.end()) {
            textures_.push_back(imported.asset);
        } else {
            *existing = imported.asset;
        }
    }
    storeRecord(imported.record);
    reportProgress(progress, 100, "FFULT texture cache loaded");
    return AssetImportResult {true, imported.record, {}};
}

AssetImportResult AssetManager::importFfultAsset(
    const std::filesystem::path& sourcePath,
    const AssetImportProgressCallback& progress)
{
    std::string error;
    reportProgress(progress, 5, "Reading FFULT asset");
    const auto bytes = detail::readBytes(sourcePath, &error);
    if (bytes.empty()) {
        core::logError(core::LogCategory::Assets, error);
        return {false, {}, std::move(error)};
    }
    const auto type = detail::readFfultAssetType(bytes, &error);
    if (!error.empty()) {
        core::logError(core::LogCategory::Assets, error);
        return {false, {}, std::move(error)};
    }
    return type == AssetType::Model
        ? importFfultModel(sourcePath, progress)
        : importFfultTexture(sourcePath, progress);
}

AssetImportResult AssetManager::importFfultModel(
    const std::filesystem::path& sourcePath,
    const AssetImportProgressCallback& progress)
{
    std::string error;
    reportProgress(progress, 5, "Reading FFULT model");
    const auto bytes = detail::readBytes(sourcePath, &error);
    if (bytes.empty()) {
        core::logError(core::LogCategory::Assets, error);
        return {false, {}, std::move(error)};
    }
    reportProgress(progress, 45, "Loading cooked model");
    auto imported = detail::readFfultModel(sourcePath, bytes, &error);
    if (imported.asset == nullptr) {
        core::logError(core::LogCategory::Assets, error);
        return {false, {}, std::move(error)};
    }
    reportProgress(progress, 88, "Writing asset cache");
    if (!writeCacheRecord(imported.record, &error)) {
        core::logError(core::LogCategory::Assets, error);
        return {false, {}, std::move(error)};
    }
    {
        std::scoped_lock lock(mutex_);
        auto existing = std::find_if(models_.begin(), models_.end(), [&imported](const auto& model) {
            return model != nullptr && model->id == imported.asset->id;
        });
        if (existing == models_.end()) {
            models_.push_back(imported.asset);
        } else {
            *existing = imported.asset;
        }
    }
    storeRecord(imported.record);
    reportProgress(progress, 100, "FFULT model imported");
    core::logInfo(core::LogCategory::Assets, "FFULT model asset imported");
    return {true, imported.record, {}};
}

AssetImportResult AssetManager::importFfultTexture(
    const std::filesystem::path& sourcePath,
    const AssetImportProgressCallback& progress)
{
    std::string error;
    reportProgress(progress, 5, "Reading FFULT texture");
    const auto bytes = detail::readBytes(sourcePath, &error);
    if (bytes.empty()) {
        core::logError(core::LogCategory::Assets, error);
        return {false, {}, std::move(error)};
    }
    reportProgress(progress, 45, "Loading cooked texture");
    auto imported = detail::readFfultTexture(sourcePath, bytes, &error);
    if (imported.asset == nullptr) {
        core::logError(core::LogCategory::Assets, error);
        return {false, {}, std::move(error)};
    }
    reportProgress(progress, 88, "Writing asset cache");
    if (!writeCacheRecord(imported.record, &error)) {
        core::logError(core::LogCategory::Assets, error);
        return {false, {}, std::move(error)};
    }
    {
        std::scoped_lock lock(mutex_);
        auto existing = std::find_if(textures_.begin(), textures_.end(), [&imported](const auto& texture) {
            return texture != nullptr && texture->id == imported.asset->id;
        });
        if (existing == textures_.end()) {
            textures_.push_back(imported.asset);
        } else {
            *existing = imported.asset;
        }
    }
    storeRecord(imported.record);
    reportProgress(progress, 100, "FFULT texture imported");
    core::logInfo(core::LogCategory::Assets, "FFULT texture asset imported");
    return {true, imported.record, {}};
}

} // namespace projectunity::assets
