#include "StbTextureImport.hpp"

#include "AssetImportUtils.hpp"

#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace projectunity::assets::detail {
namespace {

[[nodiscard]] std::uint8_t floatToPreviewByte(float value)
{
    if (!std::isfinite(value)) {
        return value > 0.0F ? 255U : 0U;
    }
    const auto mapped = value / (1.0F + std::max(value, 0.0F));
    return static_cast<std::uint8_t>(std::clamp(mapped, 0.0F, 1.0F) * 255.0F + 0.5F);
}

} // namespace

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

    TextureAsset texture;
    texture.id = makeId(sourceBytes, AssetType::Texture2D);
    texture.name = sourcePath.stem().string().empty() ? "Imported Texture" : sourcePath.stem().string();

    if (stbi_is_hdr_from_memory(sourceBytes.data(), static_cast<int>(sourceBytes.size())) != 0) {
        auto* hdrPixels = stbi_loadf_from_memory(
            sourceBytes.data(),
            static_cast<int>(sourceBytes.size()),
            &width,
            &height,
            &sourceChannels,
            STBI_rgb_alpha);
        if (hdrPixels == nullptr || width <= 0 || height <= 0) {
            setError(errorMessage, stbi_failure_reason() == nullptr ? "stb_image failed to decode HDR texture" : stbi_failure_reason());
            if (hdrPixels != nullptr) {
                stbi_image_free(hdrPixels);
            }
            return {};
        }
        const auto pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        texture.width = static_cast<std::uint32_t>(width);
        texture.height = static_cast<std::uint32_t>(height);
        texture.rgba32f.assign(hdrPixels, hdrPixels + pixelCount * 4U);
        texture.rgba8.resize(pixelCount * 4U);
        for (std::size_t pixel = 0; pixel < pixelCount; ++pixel) {
            const auto source = pixel * 4U;
            texture.rgba8[source] = floatToPreviewByte(texture.rgba32f[source]);
            texture.rgba8[source + 1U] = floatToPreviewByte(texture.rgba32f[source + 1U]);
            texture.rgba8[source + 2U] = floatToPreviewByte(texture.rgba32f[source + 2U]);
            texture.rgba8[source + 3U] = static_cast<std::uint8_t>(std::clamp(texture.rgba32f[source + 3U], 0.0F, 1.0F) * 255.0F + 0.5F);
        }
        stbi_image_free(hdrPixels);
        return texture;
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

    texture.width = static_cast<std::uint32_t>(width);
    texture.height = static_cast<std::uint32_t>(height);
    texture.rgba8.assign(pixels, pixels + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
    stbi_image_free(pixels);
    return texture;
}

} // namespace projectunity::assets::detail
