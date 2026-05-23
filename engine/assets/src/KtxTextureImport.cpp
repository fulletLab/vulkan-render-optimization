#include "KtxTextureImport.hpp"

#include "AssetImportUtils.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace projectunity::assets::detail {
namespace {

constexpr std::array<std::uint8_t, 12> kKtx1Identifier {{
    0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A,
}};
constexpr std::array<std::uint8_t, 12> kKtx2Identifier {{
    0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A,
}};

constexpr std::uint32_t kKtxLittleEndian = 0x04030201U;
constexpr std::uint32_t kKtx2NoSupercompression = 0U;

[[nodiscard]] std::string lowerExtension(std::filesystem::path path)
{
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension;
}

[[nodiscard]] bool hasIdentifier(std::span<const std::uint8_t> bytes, const std::array<std::uint8_t, 12>& identifier)
{
    return bytes.size() >= identifier.size()
        && std::equal(identifier.begin(), identifier.end(), bytes.begin());
}

[[nodiscard]] std::uint32_t readU32(std::span<const std::uint8_t> bytes, std::size_t offset)
{
    if (offset + 4U > bytes.size()) {
        return 0;
    }
    return static_cast<std::uint32_t>(bytes[offset])
        | (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U)
        | (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U)
        | (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

[[nodiscard]] std::uint64_t readU64(std::span<const std::uint8_t> bytes, std::size_t offset)
{
    std::uint64_t value = 0;
    for (std::size_t byte = 0; byte < 8U && offset + byte < bytes.size(); ++byte) {
        value |= static_cast<std::uint64_t>(bytes[offset + byte]) << (byte * 8U);
    }
    return value;
}

[[nodiscard]] std::optional<TextureGpuFormat> mapAstcFormat(std::uint32_t value, std::uint32_t base, bool srgb)
{
    constexpr std::array<TextureGpuFormat, 14> unorm {{
        TextureGpuFormat::Astc4x4Unorm,
        TextureGpuFormat::Astc5x4Unorm,
        TextureGpuFormat::Astc5x5Unorm,
        TextureGpuFormat::Astc6x5Unorm,
        TextureGpuFormat::Astc6x6Unorm,
        TextureGpuFormat::Astc8x5Unorm,
        TextureGpuFormat::Astc8x6Unorm,
        TextureGpuFormat::Astc8x8Unorm,
        TextureGpuFormat::Astc10x5Unorm,
        TextureGpuFormat::Astc10x6Unorm,
        TextureGpuFormat::Astc10x8Unorm,
        TextureGpuFormat::Astc10x10Unorm,
        TextureGpuFormat::Astc12x10Unorm,
        TextureGpuFormat::Astc12x12Unorm,
    }};
    constexpr std::array<TextureGpuFormat, 14> srgbFormats {{
        TextureGpuFormat::Astc4x4Srgb,
        TextureGpuFormat::Astc5x4Srgb,
        TextureGpuFormat::Astc5x5Srgb,
        TextureGpuFormat::Astc6x5Srgb,
        TextureGpuFormat::Astc6x6Srgb,
        TextureGpuFormat::Astc8x5Srgb,
        TextureGpuFormat::Astc8x6Srgb,
        TextureGpuFormat::Astc8x8Srgb,
        TextureGpuFormat::Astc10x5Srgb,
        TextureGpuFormat::Astc10x6Srgb,
        TextureGpuFormat::Astc10x8Srgb,
        TextureGpuFormat::Astc10x10Srgb,
        TextureGpuFormat::Astc12x10Srgb,
        TextureGpuFormat::Astc12x12Srgb,
    }};
    const auto index = value - base;
    if (index >= unorm.size()) {
        return std::nullopt;
    }
    return srgb ? srgbFormats[index] : unorm[index];
}

[[nodiscard]] std::optional<TextureGpuFormat> mapKtx1GlInternalFormat(std::uint32_t value)
{
    if (value >= 0x93B0U && value <= 0x93BDU) {
        return mapAstcFormat(value, 0x93B0U, false);
    }
    if (value >= 0x93D0U && value <= 0x93DDU) {
        return mapAstcFormat(value, 0x93D0U, true);
    }
    switch (value) {
    case 0x8058U:
        return TextureGpuFormat::Rgba8Unorm;
    case 0x8C43U:
        return TextureGpuFormat::Rgba8Srgb;
    case 0x83F0U:
        return TextureGpuFormat::Bc1RgbUnorm;
    case 0x8C4CU:
        return TextureGpuFormat::Bc1RgbSrgb;
    case 0x83F1U:
        return TextureGpuFormat::Bc1RgbaUnorm;
    case 0x8C4DU:
        return TextureGpuFormat::Bc1RgbaSrgb;
    case 0x83F2U:
        return TextureGpuFormat::Bc2Unorm;
    case 0x8C4EU:
        return TextureGpuFormat::Bc2Srgb;
    case 0x83F3U:
        return TextureGpuFormat::Bc3Unorm;
    case 0x8C4FU:
        return TextureGpuFormat::Bc3Srgb;
    case 0x8E8CU:
        return TextureGpuFormat::Bc7Unorm;
    case 0x8E8DU:
        return TextureGpuFormat::Bc7Srgb;
    case 0x9278U:
        return TextureGpuFormat::Etc2Rgba8Unorm;
    case 0x9279U:
        return TextureGpuFormat::Etc2Rgba8Srgb;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<TextureGpuFormat> mapKtx2VkFormat(std::uint32_t value)
{
    if (value >= 157U && value <= 184U) {
        const auto base = 157U + ((value - 157U) / 2U);
        return mapAstcFormat(base, 157U, ((value - 157U) % 2U) != 0U);
    }
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

[[nodiscard]] TextureAsset finalizeTexture(
    std::string name,
    std::uint32_t width,
    std::uint32_t height,
    TextureGpuFormat format,
    std::vector<TextureMipLevel> levels)
{
    TextureAsset texture;
    texture.name = name.empty() ? "KTX Texture" : std::move(name);
    texture.width = width;
    texture.height = height;
    texture.gpuFormat = format;
    texture.gpuMipLevels = std::move(levels);
    texture.id = makeTextureId(texture);
    return texture;
}

[[nodiscard]] TextureAsset importKtx1(
    std::string name,
    std::span<const std::uint8_t> bytes,
    std::string* errorMessage)
{
    if (bytes.size() < 64U || readU32(bytes, 12U) != kKtxLittleEndian) {
        setError(errorMessage, "KTX1 header is invalid or not little-endian");
        return {};
    }
    const auto glType = readU32(bytes, 16U);
    const auto glFormat = readU32(bytes, 24U);
    const auto glInternalFormat = readU32(bytes, 28U);
    const auto width = readU32(bytes, 36U);
    const auto height = readU32(bytes, 40U);
    const auto depth = readU32(bytes, 44U);
    const auto arrayElements = readU32(bytes, 48U);
    const auto faces = readU32(bytes, 52U);
    const auto requestedLevels = readU32(bytes, 56U);
    const auto keyValueBytes = readU32(bytes, 60U);
    if (width == 0U || height == 0U || depth != 0U || arrayElements != 0U || faces != 1U) {
        setError(errorMessage, "KTX1 import supports only non-array 2D textures");
        return {};
    }
    const auto format = mapKtx1GlInternalFormat(glInternalFormat);
    if (!format.has_value()) {
        setError(errorMessage, "KTX1 texture uses an unsupported GPU format");
        return {};
    }
    if (*format != TextureGpuFormat::Rgba8Unorm && *format != TextureGpuFormat::Rgba8Srgb
        && (glType != 0U || glFormat != 0U)) {
        setError(errorMessage, "KTX1 compressed textures must have glType/glFormat set to zero");
        return {};
    }
    auto offset = 64ULL + static_cast<std::uint64_t>(keyValueBytes);
    const auto levels = std::max(requestedLevels, 1U);
    std::vector<TextureMipLevel> mipLevels;
    mipLevels.reserve(levels);
    for (std::uint32_t level = 0; level < levels; ++level) {
        if (offset + 4ULL > bytes.size()) {
            setError(errorMessage, "KTX1 mip level header is truncated");
            return {};
        }
        const auto imageSize = readU32(bytes, static_cast<std::size_t>(offset));
        offset += 4ULL;
        if (imageSize == 0U || offset + imageSize > bytes.size()) {
            setError(errorMessage, "KTX1 mip level data is truncated");
            return {};
        }
        TextureMipLevel mip;
        mip.width = std::max(width >> level, 1U);
        mip.height = std::max(height >> level, 1U);
        mip.bytes.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
            bytes.begin() + static_cast<std::ptrdiff_t>(offset + imageSize));
        mipLevels.push_back(std::move(mip));
        offset += imageSize;
        offset = (offset + 3ULL) & ~3ULL;
    }
    return finalizeTexture(std::move(name), width, height, *format, std::move(mipLevels));
}

[[nodiscard]] TextureAsset importKtx2(
    std::string name,
    std::span<const std::uint8_t> bytes,
    std::string* errorMessage)
{
    if (bytes.size() < 80U) {
        setError(errorMessage, "KTX2 header is truncated");
        return {};
    }
    const auto vkFormat = readU32(bytes, 12U);
    const auto width = readU32(bytes, 20U);
    const auto height = readU32(bytes, 24U);
    const auto depth = readU32(bytes, 28U);
    const auto layers = readU32(bytes, 32U);
    const auto faces = readU32(bytes, 36U);
    const auto levelCount = readU32(bytes, 40U);
    const auto supercompression = readU32(bytes, 44U);
    if (width == 0U || height == 0U || depth != 0U || layers > 1U || faces != 1U || levelCount == 0U) {
        setError(errorMessage, "KTX2 import supports only non-array 2D textures with explicit levels");
        return {};
    }
    if (supercompression != kKtx2NoSupercompression) {
        setError(errorMessage, "KTX2 BasisLZ/Zstd supercompression is not supported by this importer yet");
        return {};
    }
    const auto format = mapKtx2VkFormat(vkFormat);
    if (!format.has_value()) {
        setError(errorMessage, "KTX2 texture uses an unsupported VkFormat");
        return {};
    }
    if (80ULL + static_cast<std::uint64_t>(levelCount) * 24ULL > bytes.size()) {
        setError(errorMessage, "KTX2 level index is truncated");
        return {};
    }
    std::vector<TextureMipLevel> mipLevels;
    mipLevels.reserve(levelCount);
    for (std::uint32_t level = 0; level < levelCount; ++level) {
        const auto indexOffset = 80U + static_cast<std::size_t>(level) * 24U;
        const auto byteOffset = readU64(bytes, indexOffset);
        const auto byteLength = readU64(bytes, indexOffset + 8U);
        if (byteLength == 0ULL || byteOffset + byteLength > bytes.size()) {
            setError(errorMessage, "KTX2 mip level data is truncated");
            return {};
        }
        TextureMipLevel mip;
        mip.width = std::max(width >> level, 1U);
        mip.height = std::max(height >> level, 1U);
        mip.bytes.assign(bytes.begin() + static_cast<std::ptrdiff_t>(byteOffset),
            bytes.begin() + static_cast<std::ptrdiff_t>(byteOffset + byteLength));
        mipLevels.push_back(std::move(mip));
    }
    return finalizeTexture(std::move(name), width, height, *format, std::move(mipLevels));
}

} // namespace

bool isKtxTextureExtension(const std::filesystem::path& path)
{
    const auto extension = lowerExtension(path);
    return extension == ".ktx" || extension == ".ktx2";
}

TextureAsset importKtxTexture(
    const std::filesystem::path& sourcePath,
    std::span<const std::uint8_t> bytes,
    std::string* errorMessage)
{
    auto name = sourcePath.stem().string();
    return importKtxTextureFromMemory(std::move(name), bytes, errorMessage);
}

TextureAsset importKtxTextureFromMemory(
    std::string name,
    std::span<const std::uint8_t> bytes,
    std::string* errorMessage)
{
    if (hasIdentifier(bytes, kKtx1Identifier)) {
        return importKtx1(std::move(name), bytes, errorMessage);
    }
    if (hasIdentifier(bytes, kKtx2Identifier)) {
        return importKtx2(std::move(name), bytes, errorMessage);
    }
    setError(errorMessage, "Texture is not a KTX1/KTX2 file");
    return {};
}

} // namespace projectunity::assets::detail
