#include <projectunity/assets/AssetManager.hpp>

#include "AssetImportUtils.hpp"
#include "FfultAssetFormat.hpp"

#include <projectunity/core/Log.hpp>

#include <algorithm>
#include <cctype>
#include <utility>

namespace projectunity::assets {
namespace {

[[nodiscard]] std::string lowerExtension(std::filesystem::path path)
{
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension;
}

} // namespace

AssetManager::AssetManager(std::filesystem::path cacheRoot)
    : cacheRoot_(std::move(cacheRoot))
{
}

AssetImportResult AssetManager::importAsset(const std::filesystem::path& sourcePath)
{
    return importAsset(sourcePath, {});
}

AssetImportResult AssetManager::importModel(const std::filesystem::path& sourcePath)
{
    return importModel(sourcePath, {});
}

AssetImportResult AssetManager::importTexture(const std::filesystem::path& sourcePath)
{
    return importTexture(sourcePath, {});
}

AssetImportResult AssetManager::importAsset(
    const std::filesystem::path& sourcePath,
    const AssetImportProgressCallback& progress)
{
    if (progress) {
        progress({1, "Classifying asset"});
    }
    const auto extension = lowerExtension(sourcePath);
    if (extension == ".glb" || extension == ".gltf") {
        return importModel(sourcePath, progress);
    }
    if (extension == ".ffult") {
        return importFfultAsset(sourcePath, progress);
    }
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".ktx" || extension == ".ktx2") {
        return importTexture(sourcePath, progress);
    }

    core::logWarning(core::LogCategory::Assets, "Asset import rejected an unsupported file extension");
    return {false, {}, "Unsupported asset extension"};
}

} // namespace projectunity::assets
