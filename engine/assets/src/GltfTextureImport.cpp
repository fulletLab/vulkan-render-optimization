#include "GltfTextureImport.hpp"

#include "AssetImportUtils.hpp"
#include "KtxTextureImport.hpp"
#include "StbTextureImport.hpp"

#include <tiny_gltf.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

namespace projectunity::assets::detail {
namespace {

[[nodiscard]] std::string lowerExtension(std::filesystem::path path)
{
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension;
}

[[nodiscard]] bool isKtxMimeType(const std::string& mimeType)
{
    return mimeType == "image/ktx" || mimeType == "image/ktx2";
}

[[nodiscard]] bool hasDecodedPixels(const tinygltf::Image& image)
{
    return image.width > 0 && image.height > 0 && image.component > 0 && !image.image.empty();
}

[[nodiscard]] bool convertDecodedImage(
    const tinygltf::Image& image,
    std::string name,
    TextureAsset& output,
    std::string* errorMessage)
{
    if (image.width <= 0 || image.height <= 0 || image.component <= 0 || image.image.empty()) {
        setError(errorMessage, "glTF texture image is empty or invalid");
        return false;
    }
    const auto width = static_cast<std::uint32_t>(image.width);
    const auto height = static_cast<std::uint32_t>(image.height);
    const auto pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const auto component = static_cast<std::size_t>(image.component);
    if (component > 4 || image.image.size() < pixels * component) {
        setError(errorMessage, "glTF texture image channel data is invalid");
        return false;
    }
    output.name = name.empty() ? "Texture" : std::move(name);
    output.width = width;
    output.height = height;
    output.gpuFormat = TextureGpuFormat::Rgba8Unorm;
    output.rgba8.assign(pixels * 4U, 255U);
    for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
        output.rgba8[pixel * 4U] = image.image[pixel * component];
        output.rgba8[pixel * 4U + 1U] = component > 1 ? image.image[pixel * component + 1U] : image.image[pixel * component];
        output.rgba8[pixel * 4U + 2U] = component > 2 ? image.image[pixel * component + 2U] : image.image[pixel * component];
        output.rgba8[pixel * 4U + 3U] = component > 3 ? image.image[pixel * component + 3U] : 255U;
    }
    output.id = makeTextureId(output);
    return true;
}

[[nodiscard]] bool importBufferViewKtx(
    const tinygltf::Model& gltf,
    const tinygltf::Image& image,
    std::string name,
    TextureAsset& output,
    std::string* errorMessage)
{
    if (image.bufferView < 0 || static_cast<std::size_t>(image.bufferView) >= gltf.bufferViews.size()) {
        return false;
    }
    const auto& view = gltf.bufferViews[static_cast<std::size_t>(image.bufferView)];
    if (view.buffer < 0 || static_cast<std::size_t>(view.buffer) >= gltf.buffers.size()) {
        setError(errorMessage, "glTF KTX texture buffer view references an invalid buffer");
        return false;
    }
    const auto& buffer = gltf.buffers[static_cast<std::size_t>(view.buffer)];
    if (view.byteOffset + view.byteLength > buffer.data.size()) {
        setError(errorMessage, "glTF KTX texture buffer view is out of range");
        return false;
    }
    const std::span<const std::uint8_t> bytes {
        buffer.data.data() + static_cast<std::ptrdiff_t>(view.byteOffset),
        view.byteLength,
    };
    output = importKtxTextureFromMemory(std::move(name), bytes, errorMessage);
    return output.id.isValid();
}

[[nodiscard]] bool importBufferViewStb(
    const tinygltf::Model& gltf,
    const tinygltf::Image& image,
    std::string name,
    TextureAsset& output,
    std::string* errorMessage)
{
    if (image.bufferView < 0 || static_cast<std::size_t>(image.bufferView) >= gltf.bufferViews.size()) {
        return false;
    }
    const auto& view = gltf.bufferViews[static_cast<std::size_t>(image.bufferView)];
    if (view.buffer < 0 || static_cast<std::size_t>(view.buffer) >= gltf.buffers.size()) {
        setError(errorMessage, "glTF texture buffer view references an invalid buffer");
        return false;
    }
    const auto& buffer = gltf.buffers[static_cast<std::size_t>(view.buffer)];
    if (view.byteOffset + view.byteLength > buffer.data.size()) {
        setError(errorMessage, "glTF texture buffer view is out of range");
        return false;
    }
    const std::span<const std::uint8_t> bytes {
        buffer.data.data() + static_cast<std::ptrdiff_t>(view.byteOffset),
        view.byteLength,
    };
    output = importStbTexture(std::filesystem::path(name.empty() ? std::string {"Texture"} : std::move(name)), bytes, errorMessage);
    return output.id.isValid();
}

} // namespace

bool importGltfTexture(
    const tinygltf::Model& gltf,
    const tinygltf::Image& image,
    std::string name,
    const std::filesystem::path& baseDirectory,
    TextureAsset& output,
    std::string* errorMessage)
{
    const auto uriExtension = lowerExtension(image.uri);
    if (isKtxMimeType(image.mimeType) || uriExtension == ".ktx" || uriExtension == ".ktx2") {
        if (!image.uri.empty() && image.uri.rfind("data:", 0) != 0) {
            const auto texturePath = baseDirectory / std::filesystem::path(image.uri);
            const auto bytes = readBytes(texturePath, errorMessage);
            if (bytes.empty()) {
                return false;
            }
            output = importKtxTextureFromMemory(name.empty() ? texturePath.stem().string() : std::move(name), bytes, errorMessage);
            return output.id.isValid();
        }
        if (!image.image.empty()) {
            output = importKtxTextureFromMemory(std::move(name), image.image, errorMessage);
            return output.id.isValid();
        }
        return importBufferViewKtx(gltf, image, std::move(name), output, errorMessage);
    }
    if (hasDecodedPixels(image)) {
        return convertDecodedImage(image, std::move(name), output, errorMessage);
    }
    if (!image.image.empty()) {
        const auto decodedName = name.empty() ? (image.uri.empty() ? std::string {"Texture"} : image.uri) : std::move(name);
        output = importStbTexture(std::filesystem::path(decodedName), image.image, errorMessage);
        return output.id.isValid();
    }
    if (image.bufferView >= 0 && importBufferViewStb(gltf, image, std::move(name), output, errorMessage)) {
        return true;
    }
    if (image.bufferView >= 0 && importBufferViewKtx(gltf, image, std::move(name), output, errorMessage)) {
        return true;
    }
    setError(errorMessage, "glTF texture image has no decoded pixels or supported KTX payload");
    return false;
}

} // namespace projectunity::assets::detail
