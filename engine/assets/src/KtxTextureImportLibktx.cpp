#include "KtxTextureImportLibktx.hpp"

#include "AssetImportUtils.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#if PROJECTUNITY_HAS_LIBKTX
#include <ktx.h>
#endif

namespace projectunity::assets::detail {
namespace {

#if PROJECTUNITY_HAS_LIBKTX

struct KtxTextureHandle {
    ktxTexture* texture {nullptr};

    KtxTextureHandle() = default;
    explicit KtxTextureHandle(ktxTexture* value)
        : texture(value)
    {
    }
    KtxTextureHandle(const KtxTextureHandle&) = delete;
    KtxTextureHandle& operator=(const KtxTextureHandle&) = delete;
    KtxTextureHandle(KtxTextureHandle&& other) noexcept
        : texture(other.texture)
    {
        other.texture = nullptr;
    }
    KtxTextureHandle& operator=(KtxTextureHandle&& other) noexcept
    {
        if (this != &other) {
            if (texture != nullptr) {
                ktxTexture_Destroy(texture);
            }
            texture = other.texture;
            other.texture = nullptr;
        }
        return *this;
    }
    ~KtxTextureHandle()
    {
        if (texture != nullptr) {
            ktxTexture_Destroy(texture);
        }
    }
};

[[nodiscard]] std::optional<TextureGpuFormat> mapKtx2VkFormat(std::uint32_t value)
{
    switch (value) {
    case 37U:
        return TextureGpuFormat::Rgba8Unorm;
    case 43U:
        return TextureGpuFormat::Rgba8Srgb;
    case 131U:
        return TextureGpuFormat::Bc1RgbUnorm;
    case 132U:
        return TextureGpuFormat::Bc1RgbSrgb;
    case 133U:
        return TextureGpuFormat::Bc1RgbaUnorm;
    case 134U:
        return TextureGpuFormat::Bc1RgbaSrgb;
    case 135U:
        return TextureGpuFormat::Bc2Unorm;
    case 136U:
        return TextureGpuFormat::Bc2Srgb;
    case 137U:
        return TextureGpuFormat::Bc3Unorm;
    case 138U:
        return TextureGpuFormat::Bc3Srgb;
    case 141U:
        return TextureGpuFormat::Bc5Unorm;
    case 142U:
        return TextureGpuFormat::Bc5Snorm;
    case 145U:
        return TextureGpuFormat::Bc7Unorm;
    case 146U:
        return TextureGpuFormat::Bc7Srgb;
    case 151U:
        return TextureGpuFormat::Etc2Rgba8Unorm;
    case 152U:
        return TextureGpuFormat::Etc2Rgba8Srgb;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] TextureGpuFormat transcodeRgbaFormat(ktxTexture2* texture)
{
    return ktxTexture2_GetOETF_e(texture) == KHR_DF_TRANSFER_SRGB
        ? TextureGpuFormat::Rgba8Srgb
        : TextureGpuFormat::Rgba8Unorm;
}

[[nodiscard]] TextureGpuFormat transcodeBc7Format(ktxTexture2* texture)
{
    return ktxTexture2_GetOETF_e(texture) == KHR_DF_TRANSFER_SRGB
        ? TextureGpuFormat::Bc7Srgb
        : TextureGpuFormat::Bc7Unorm;
}

[[nodiscard]] bool isPlainRgba8(TextureGpuFormat format)
{
    return format == TextureGpuFormat::Rgba8Unorm || format == TextureGpuFormat::Rgba8Srgb;
}

[[nodiscard]] bool validate2DTexture(const ktxTexture2& texture, std::string* errorMessage)
{
    if (texture.baseWidth == 0U
        || texture.baseHeight == 0U
        || texture.baseDepth > 1U
        || texture.numDimensions != 2U
        || texture.isArray
        || texture.isCubemap
        || texture.numFaces != 1U
        || texture.numLayers > 1U
        || texture.numLevels == 0U) {
        setError(errorMessage, "libktx import supports only non-array 2D KTX2 textures");
        return false;
    }
    return true;
}

[[nodiscard]] TextureAsset buildTextureFromKtx(
    std::string name,
    ktxTexture2& texture,
    TextureGpuFormat format,
    std::string* errorMessage)
{
    const auto* data = ktxTexture_GetData(ktxTexture(&texture));
    if (data == nullptr) {
        setError(errorMessage, "libktx returned no image data");
        return {};
    }
    TextureAsset output;
    output.name = name.empty() ? "KTX2 Texture" : std::move(name);
    output.width = texture.baseWidth;
    output.height = texture.baseHeight;
    output.gpuFormat = format;
    output.gpuMipLevels.reserve(texture.numLevels);
    for (std::uint32_t level = 0; level < texture.numLevels; ++level) {
        ktx_size_t offset = 0;
        const auto offsetResult = ktxTexture_GetImageOffset(ktxTexture(&texture), level, 0, 0, &offset);
        const auto imageSize = ktxTexture_GetImageSize(ktxTexture(&texture), level);
        if (offsetResult != KTX_SUCCESS || imageSize == 0U || offset + imageSize > texture.dataSize) {
            setError(errorMessage, "libktx KTX2 mip level data is invalid");
            return {};
        }
        TextureMipLevel mip;
        mip.width = std::max(texture.baseWidth >> level, 1U);
        mip.height = std::max(texture.baseHeight >> level, 1U);
        mip.bytes.assign(data + static_cast<std::ptrdiff_t>(offset),
            data + static_cast<std::ptrdiff_t>(offset + imageSize));
        output.gpuMipLevels.push_back(std::move(mip));
    }
    if (isPlainRgba8(format)
        && !output.gpuMipLevels.empty()
        && output.gpuMipLevels.front().width == output.width
        && output.gpuMipLevels.front().height == output.height
        && output.gpuMipLevels.front().bytes.size() >= static_cast<std::size_t>(output.width) * output.height * 4U) {
        output.rgba8.assign(
            output.gpuMipLevels.front().bytes.begin(),
            output.gpuMipLevels.front().bytes.begin()
                + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(output.width) * output.height * 4U));
    }
    output.id = makeTextureId(output);
    return output;
}

[[nodiscard]] KtxTextureHandle loadKtx2Texture(
    std::span<const std::uint8_t> bytes,
    std::string* errorMessage)
{
    ktxTexture* rawTexture = nullptr;
    constexpr auto flags = KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT;
    const auto createResult = ktxTexture_CreateFromMemory(
        bytes.data(),
        static_cast<ktx_size_t>(bytes.size()),
        flags,
        &rawTexture);
    KtxTextureHandle handle {rawTexture};
    if (createResult != KTX_SUCCESS || rawTexture == nullptr) {
        setError(errorMessage, std::string("libktx could not parse KTX2 texture: ") + ktxErrorString(createResult));
        return {};
    }
    if (rawTexture->classId != ktxTexture2_c) {
        setError(errorMessage, "libktx parsed a non-KTX2 texture for the KTX2 path");
        return {};
    }
    return handle;
}

#endif

} // namespace

TextureAsset importKtx2WithLibktx(
    std::string name,
    std::span<const std::uint8_t> bytes,
    std::string* errorMessage)
{
#if PROJECTUNITY_HAS_LIBKTX
    auto handle = loadKtx2Texture(bytes, errorMessage);
    if (handle.texture == nullptr) {
        return {};
    }
    auto* texture = reinterpret_cast<ktxTexture2*>(handle.texture);
    if (!validate2DTexture(*texture, errorMessage)) {
        return {};
    }
    if (ktxTexture2_NeedsTranscoding(texture)) {
        const auto rgbaFormat = transcodeRgbaFormat(texture);
        const auto transcodeResult = ktxTexture2_TranscodeBasis(texture, KTX_TTF_RGBA32, KTX_TF_HIGH_QUALITY);
        if (transcodeResult != KTX_SUCCESS) {
            setError(errorMessage, std::string("libktx KTX2 Basis/UASTC transcode failed: ") + ktxErrorString(transcodeResult));
            return {};
        }
        auto fallback = buildTextureFromKtx(name, *texture, rgbaFormat, errorMessage);
        if (!fallback.id.isValid()) {
            return {};
        }
        std::string compressedError;
        auto compressedHandle = loadKtx2Texture(bytes, &compressedError);
        if (compressedHandle.texture != nullptr) {
            auto* compressedTexture = reinterpret_cast<ktxTexture2*>(compressedHandle.texture);
            if (validate2DTexture(*compressedTexture, nullptr)) {
                const auto compressedFormat = transcodeBc7Format(compressedTexture);
                const auto compressedResult = ktxTexture2_TranscodeBasis(compressedTexture, KTX_TTF_BC7_RGBA, KTX_TF_HIGH_QUALITY);
                if (compressedResult == KTX_SUCCESS) {
                    auto compressed = buildTextureFromKtx(std::move(name), *compressedTexture, compressedFormat, nullptr);
                    if (compressed.id.isValid()) {
                        compressed.rgba8 = std::move(fallback.rgba8);
                        compressed.id = makeTextureId(compressed);
                        return compressed;
                    }
                }
            }
        }
        return fallback;
    }
    const auto format = mapKtx2VkFormat(texture->vkFormat);
    if (!format.has_value()) {
        setError(errorMessage, "libktx KTX2 texture uses an unsupported VkFormat");
        return {};
    }
    return buildTextureFromKtx(std::move(name), *texture, *format, errorMessage);
#else
    (void) name;
    (void) bytes;
    setError(errorMessage, "KTX2 BasisLZ/Zstd supercompression requires libktx support");
    return {};
#endif
}

} // namespace projectunity::assets::detail
