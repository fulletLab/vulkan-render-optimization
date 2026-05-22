#include "StbTextureImport.hpp"

#include "AssetImportUtils.hpp"

#include <stb_image.h>

#include <limits>

namespace projectunity::assets::detail {

TextureAsset importStbTexture(
    const std::filesystem::path& sourcePath,
    std::span<const std::uint8_t> sourceBytes,
    std::string* errorMessage)
{
    int width = 0;
    int height = 0;
    int sourceChannels = 0;
    if (sourceBytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        setError(errorMessage, "Texture source file is too large for stb_image memory import");
        return {};
    }

    auto* pixels = stbi_load_from_memory(
        sourceBytes.data(),
        static_cast<int>(sourceBytes.size()),
        &width,
        &height,
        &sourceChannels,
        STBI_rgb_alpha);
    if (pixels == nullptr || width <= 0 || height <= 0) {
        setError(errorMessage, stbi_failure_reason() == nullptr ? "stb_image failed to decode texture" : stbi_failure_reason());
        if (pixels != nullptr) {
            stbi_image_free(pixels);
        }
        return {};
    }

    TextureAsset texture;
    texture.id = makeId(sourceBytes, AssetType::Texture2D);
    texture.name = sourcePath.stem().string().empty() ? "Imported Texture" : sourcePath.stem().string();
    texture.width = static_cast<std::uint32_t>(width);
    texture.height = static_cast<std::uint32_t>(height);
    texture.rgba8.assign(pixels, pixels + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
    stbi_image_free(pixels);
    return texture;
}

} // namespace projectunity::assets::detail
