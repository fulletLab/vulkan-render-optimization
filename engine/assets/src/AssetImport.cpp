#include <projectunity/assets/AssetManager.hpp>

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
    const auto extension = lowerExtension(sourcePath);
    if (extension == ".glb" || extension == ".gltf") {
        return importModel(sourcePath);
    }
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg") {
        return importTexture(sourcePath);
    }

    core::logWarning(core::LogCategory::Assets, "Asset import rejected an unsupported file extension");
    return {false, {}, "Unsupported asset extension"};
}

} // namespace projectunity::assets
