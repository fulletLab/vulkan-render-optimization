#include "FfultAssetFormat.hpp"
#include "AssetImportUtils.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <system_error>
#include <utility>

namespace projectunity::assets::detail {
namespace {

constexpr std::array<std::uint8_t, 8> kMagic {{'F', 'F', 'U', 'L', 'T', 'A', 'S', 'T'}};
constexpr std::uint32_t kVersion = 1;
constexpr std::uint64_t kMaxElements = 128ULL * 1024ULL * 1024ULL;

[[nodiscard]] std::string lowerExtension(std::filesystem::path path)
{
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension;
}

class Writer final {
public:
    explicit Writer(std::filesystem::path path)
        : path_(std::move(path))
    {
    }

    [[nodiscard]] bool open(std::string* errorMessage)
    {
        std::error_code createError;
        std::filesystem::create_directories(path_.parent_path(), createError);
        if (createError) {
            setError(errorMessage, "Unable to create FFULT output directory: " + createError.message());
            return false;
        }
        out_.open(path_, std::ios::binary | std::ios::trunc);
        if (!out_) {
            setError(errorMessage, "Unable to open FFULT output file");
            return false;
        }
        return true;
    }

    void raw(std::span<const std::uint8_t> bytes)
    {
        out_.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    void u8(std::uint8_t value)
    {
        raw({&value, 1});
    }

    void u32(std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8) {
            u8(static_cast<std::uint8_t>((value >> shift) & 0xffU));
        }
    }

    void u64(std::uint64_t value)
    {
        for (int shift = 0; shift < 64; shift += 8) {
            u8(static_cast<std::uint8_t>((value >> shift) & 0xffU));
        }
    }

    void f32(float value)
    {
        static_assert(sizeof(float) == sizeof(std::uint32_t));
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        u32(bits);
    }

    void boolean(bool value)
    {
        u8(value ? 1U : 0U);
    }

    void string(const std::string& value)
    {
        u64(value.size());
        raw(std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(value.data()),
            value.size()));
    }

    void bytes(const std::vector<std::uint8_t>& value)
    {
        u64(value.size());
        raw(value);
    }

    [[nodiscard]] bool good(std::string* errorMessage)
    {
        if (!out_.good()) {
            setError(errorMessage, "Unable to write FFULT asset file");
            return false;
        }
        return true;
    }

private:
    std::filesystem::path path_;
    std::ofstream out_;
};

class Reader final {
public:
    explicit Reader(std::span<const std::uint8_t> bytes)
        : bytes_(bytes)
    {
    }

    [[nodiscard]] bool raw(std::size_t size, const std::uint8_t*& data, std::string* errorMessage)
    {
        if (size > bytes_.size() - offset_) {
            setError(errorMessage, "FFULT asset is truncated");
            return false;
        }
        data = bytes_.data() + offset_;
        offset_ += size;
        return true;
    }

    [[nodiscard]] bool u8(std::uint8_t& value, std::string* errorMessage)
    {
        const std::uint8_t* data = nullptr;
        if (!raw(1, data, errorMessage)) {
            return false;
        }
        value = *data;
        return true;
    }

    [[nodiscard]] bool u32(std::uint32_t& value, std::string* errorMessage)
    {
        value = 0;
        for (int shift = 0; shift < 32; shift += 8) {
            std::uint8_t byte = 0;
            if (!u8(byte, errorMessage)) {
                return false;
            }
            value |= static_cast<std::uint32_t>(byte) << shift;
        }
        return true;
    }

    [[nodiscard]] bool u64(std::uint64_t& value, std::string* errorMessage)
    {
        value = 0;
        for (int shift = 0; shift < 64; shift += 8) {
            std::uint8_t byte = 0;
            if (!u8(byte, errorMessage)) {
                return false;
            }
            value |= static_cast<std::uint64_t>(byte) << shift;
        }
        return true;
    }

    [[nodiscard]] bool f32(float& value, std::string* errorMessage)
    {
        std::uint32_t bits = 0;
        if (!u32(bits, errorMessage)) {
            return false;
        }
        std::memcpy(&value, &bits, sizeof(value));
        return true;
    }

    [[nodiscard]] bool boolean(bool& value, std::string* errorMessage)
    {
        std::uint8_t byte = 0;
        if (!u8(byte, errorMessage)) {
            return false;
        }
        value = byte != 0;
        return true;
    }

    [[nodiscard]] bool string(std::string& value, std::string* errorMessage)
    {
        std::uint64_t size = 0;
        if (!u64(size, errorMessage) || !checkCount(size, errorMessage)) {
            return false;
        }
        const std::uint8_t* data = nullptr;
        if (!raw(static_cast<std::size_t>(size), data, errorMessage)) {
            return false;
        }
        value.assign(reinterpret_cast<const char*>(data), static_cast<std::size_t>(size));
        return true;
    }

    [[nodiscard]] bool bytes(std::vector<std::uint8_t>& value, std::string* errorMessage)
    {
        std::uint64_t size = 0;
        if (!u64(size, errorMessage) || !checkCount(size, errorMessage)) {
            return false;
        }
        const std::uint8_t* data = nullptr;
        if (!raw(static_cast<std::size_t>(size), data, errorMessage)) {
            return false;
        }
        value.assign(data, data + size);
        return true;
    }

private:
    [[nodiscard]] bool checkCount(std::uint64_t count, std::string* errorMessage) const
    {
        if (count > kMaxElements || count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
            setError(errorMessage, "FFULT asset declares an unsupported element count");
            return false;
        }
        return true;
    }

    std::span<const std::uint8_t> bytes_;
    std::size_t offset_ {0};
};

void writeHeader(Writer& writer, AssetType type, AssetId id)
{
    writer.raw(kMagic);
    writer.u32(kVersion);
    writer.u32(static_cast<std::uint32_t>(type));
    writer.u64(id.value());
}

[[nodiscard]] bool readHeader(Reader& reader, AssetType& type, AssetId& id, std::string* errorMessage)
{
    const std::uint8_t* magic = nullptr;
    if (!reader.raw(kMagic.size(), magic, errorMessage) || !std::equal(kMagic.begin(), kMagic.end(), magic)) {
        setError(errorMessage, "FFULT asset has an invalid header");
        return false;
    }
    std::uint32_t version = 0;
    std::uint32_t rawType = 0;
    std::uint64_t rawId = 0;
    if (!reader.u32(version, errorMessage) || !reader.u32(rawType, errorMessage) || !reader.u64(rawId, errorMessage)) {
        return false;
    }
    if (version != kVersion || rawId == 0) {
        setError(errorMessage, "FFULT asset version or id is unsupported");
        return false;
    }
    if (rawType == static_cast<std::uint32_t>(AssetType::Model)) {
        type = AssetType::Model;
    } else if (rawType == static_cast<std::uint32_t>(AssetType::Texture2D)) {
        type = AssetType::Texture2D;
    } else {
        setError(errorMessage, "FFULT asset type is unsupported");
        return false;
    }
    id = AssetId(rawId);
    return true;
}

void writeVec3(Writer& writer, const math::Vec3& value)
{
    writer.f32(value.x);
    writer.f32(value.y);
    writer.f32(value.z);
}

[[nodiscard]] bool readVec3(Reader& reader, math::Vec3& value, std::string* errorMessage)
{
    return reader.f32(value.x, errorMessage)
        && reader.f32(value.y, errorMessage)
        && reader.f32(value.z, errorMessage);
}

template<std::size_t Count>
void writeArray(Writer& writer, const std::array<float, Count>& value)
{
    for (const auto item : value) {
        writer.f32(item);
    }
}

template<std::size_t Count>
[[nodiscard]] bool readArray(Reader& reader, std::array<float, Count>& value, std::string* errorMessage)
{
    for (auto& item : value) {
        if (!reader.f32(item, errorMessage)) {
            return false;
        }
    }
    return true;
}

void writeOptionalIndex(Writer& writer, std::optional<std::size_t> value)
{
    writer.boolean(value.has_value());
    writer.u64(value.value_or(0U));
}

[[nodiscard]] bool readOptionalIndex(Reader& reader, std::optional<std::size_t>& value, std::string* errorMessage)
{
    bool hasValue = false;
    std::uint64_t raw = 0;
    if (!reader.boolean(hasValue, errorMessage) || !reader.u64(raw, errorMessage)) {
        return false;
    }
    value = hasValue ? std::optional<std::size_t>(static_cast<std::size_t>(raw)) : std::nullopt;
    return true;
}

void writeBounds(Writer& writer, const MeshBounds& value)
{
    writeVec3(writer, value.minimum);
    writeVec3(writer, value.maximum);
    writeVec3(writer, value.center);
    writer.f32(value.radius);
}

[[nodiscard]] bool readBounds(Reader& reader, MeshBounds& value, std::string* errorMessage)
{
    return readVec3(reader, value.minimum, errorMessage)
        && readVec3(reader, value.maximum, errorMessage)
        && readVec3(reader, value.center, errorMessage)
        && reader.f32(value.radius, errorMessage);
}

void writeTexture(Writer& writer, const TextureAsset& value)
{
    writer.u64(value.id.value());
    writer.string(value.name);
    writer.u32(value.width);
    writer.u32(value.height);
    writer.u32(static_cast<std::uint32_t>(value.gpuFormat));
    writer.u32(static_cast<std::uint32_t>(value.sampler.magnificationFilter));
    writer.u32(static_cast<std::uint32_t>(value.sampler.minificationFilter));
    writer.u32(static_cast<std::uint32_t>(value.sampler.mipmapFilter));
    writer.u32(static_cast<std::uint32_t>(value.sampler.wrapU));
    writer.u32(static_cast<std::uint32_t>(value.sampler.wrapV));
    writer.boolean(value.sampler.useMipmaps);
    writer.bytes(value.rgba8);
    writer.u64(value.rgba32f.size());
    for (const auto item : value.rgba32f) {
        writer.f32(item);
    }
    writer.u64(value.gpuMipLevels.size());
    for (const auto& mip : value.gpuMipLevels) {
        writer.u32(mip.width);
        writer.u32(mip.height);
        writer.bytes(mip.bytes);
    }
}

[[nodiscard]] bool readTexture(Reader& reader, TextureAsset& value, std::string* errorMessage)
{
    std::uint64_t rawId = 0;
    std::uint32_t rawFormat = 0;
    std::uint32_t mag = 0;
    std::uint32_t min = 0;
    std::uint32_t mipFilter = 0;
    std::uint32_t wrapU = 0;
    std::uint32_t wrapV = 0;
    std::uint64_t floatCount = 0;
    std::uint64_t mipCount = 0;
    if (!reader.u64(rawId, errorMessage)
        || !reader.string(value.name, errorMessage)
        || !reader.u32(value.width, errorMessage)
        || !reader.u32(value.height, errorMessage)
        || !reader.u32(rawFormat, errorMessage)
        || !reader.u32(mag, errorMessage)
        || !reader.u32(min, errorMessage)
        || !reader.u32(mipFilter, errorMessage)
        || !reader.u32(wrapU, errorMessage)
        || !reader.u32(wrapV, errorMessage)
        || !reader.boolean(value.sampler.useMipmaps, errorMessage)
        || !reader.bytes(value.rgba8, errorMessage)
        || !reader.u64(floatCount, errorMessage)) {
        return false;
    }
    value.id = AssetId(rawId);
    value.gpuFormat = static_cast<TextureGpuFormat>(rawFormat);
    value.sampler.magnificationFilter = static_cast<TextureFilterMode>(mag);
    value.sampler.minificationFilter = static_cast<TextureFilterMode>(min);
    value.sampler.mipmapFilter = static_cast<TextureFilterMode>(mipFilter);
    value.sampler.wrapU = static_cast<TextureWrapMode>(wrapU);
    value.sampler.wrapV = static_cast<TextureWrapMode>(wrapV);
    if (floatCount > kMaxElements) {
        setError(errorMessage, "FFULT texture float payload is too large");
        return false;
    }
    value.rgba32f.resize(static_cast<std::size_t>(floatCount));
    for (auto& item : value.rgba32f) {
        if (!reader.f32(item, errorMessage)) {
            return false;
        }
    }
    if (!reader.u64(mipCount, errorMessage) || mipCount > kMaxElements) {
        setError(errorMessage, "FFULT texture mip count is unsupported");
        return false;
    }
    value.gpuMipLevels.resize(static_cast<std::size_t>(mipCount));
    for (auto& mip : value.gpuMipLevels) {
        if (!reader.u32(mip.width, errorMessage)
            || !reader.u32(mip.height, errorMessage)
            || !reader.bytes(mip.bytes, errorMessage)) {
            return false;
        }
    }
    return value.id.isValid();
}

void writeVertex(Writer& writer, const MeshVertex& value)
{
    writeVec3(writer, value.position);
    writeVec3(writer, value.normal);
    writeVec3(writer, value.tangent);
    writer.f32(value.tangentSign);
    writeArray(writer, value.texCoord);
    writeArray(writer, value.color);
    writeArray(writer, value.materialFactors);
}

[[nodiscard]] bool readVertex(Reader& reader, MeshVertex& value, std::string* errorMessage)
{
    return readVec3(reader, value.position, errorMessage)
        && readVec3(reader, value.normal, errorMessage)
        && readVec3(reader, value.tangent, errorMessage)
        && reader.f32(value.tangentSign, errorMessage)
        && readArray(reader, value.texCoord, errorMessage)
        && readArray(reader, value.color, errorMessage)
        && readArray(reader, value.materialFactors, errorMessage);
}

template<typename T, typename WriteItem>
void writeVector(Writer& writer, const std::vector<T>& values, WriteItem writeItem)
{
    writer.u64(values.size());
    for (const auto& value : values) {
        writeItem(value);
    }
}

template<typename T, typename ReadItem>
[[nodiscard]] bool readVector(Reader& reader, std::vector<T>& values, ReadItem readItem, std::string* errorMessage)
{
    std::uint64_t count = 0;
    if (!reader.u64(count, errorMessage) || count > kMaxElements) {
        setError(errorMessage, "FFULT vector count is unsupported");
        return false;
    }
    values.resize(static_cast<std::size_t>(count));
    for (auto& value : values) {
        if (!readItem(value)) {
            return false;
        }
    }
    return true;
}

void writePrimitive(Writer& writer, const MeshPrimitive& value)
{
    writeVector(writer, value.vertices, [&writer](const MeshVertex& vertex) { writeVertex(writer, vertex); });
    writeVector(writer, value.indices, [&writer](std::uint32_t index) { writer.u32(index); });
    writeVector(writer, value.lods, [&writer](const MeshLod& lod) {
        writeVector(writer, lod.indices, [&writer](std::uint32_t index) { writer.u32(index); });
        writer.f32(lod.error);
    });
    writeBounds(writer, value.bounds);
    writer.u64(value.materialIndex);
}

[[nodiscard]] bool readPrimitive(Reader& reader, MeshPrimitive& value, std::string* errorMessage)
{
    return readVector(reader, value.vertices, [&reader, errorMessage](MeshVertex& vertex) {
        return readVertex(reader, vertex, errorMessage);
    }, errorMessage)
        && readVector(reader, value.indices, [&reader, errorMessage](std::uint32_t& index) {
            return reader.u32(index, errorMessage);
        }, errorMessage)
        && readVector(reader, value.lods, [&reader, errorMessage](MeshLod& lod) {
            return readVector(reader, lod.indices, [&reader, errorMessage](std::uint32_t& index) {
                return reader.u32(index, errorMessage);
            }, errorMessage) && reader.f32(lod.error, errorMessage);
        }, errorMessage)
        && readBounds(reader, value.bounds, errorMessage)
        && [&reader, &value, errorMessage] {
            std::uint64_t materialIndex = 0;
            if (!reader.u64(materialIndex, errorMessage)) {
                return false;
            }
            value.materialIndex = static_cast<std::size_t>(materialIndex);
            return true;
        }();
}

void writePrimitiveInstance(Writer& writer, const MeshPrimitiveInstance& value)
{
    writer.u32(value.primitiveIndex);
    writeArray(writer, value.transform);
    writeBounds(writer, value.bounds);
    writer.boolean(value.flipsWinding);
}

[[nodiscard]] bool readPrimitiveInstance(Reader& reader, MeshPrimitiveInstance& value, std::string* errorMessage)
{
    return reader.u32(value.primitiveIndex, errorMessage)
        && readArray(reader, value.transform, errorMessage)
        && readBounds(reader, value.bounds, errorMessage)
        && reader.boolean(value.flipsWinding, errorMessage);
}

void writeEditorInstance(Writer& writer, const MeshEditorInstance& value)
{
    writer.string(value.name);
    writer.u32(value.sourcePrimitiveIndex);
    writeArray(writer, value.transform);
    writeBounds(writer, value.bounds);
    writer.boolean(value.flipsWinding);
}

[[nodiscard]] bool readEditorInstance(Reader& reader, MeshEditorInstance& value, std::string* errorMessage)
{
    return reader.string(value.name, errorMessage)
        && reader.u32(value.sourcePrimitiveIndex, errorMessage)
        && readArray(reader, value.transform, errorMessage)
        && readBounds(reader, value.bounds, errorMessage)
        && reader.boolean(value.flipsWinding, errorMessage);
}

void writeCluster(Writer& writer, const MeshPrimitiveCluster& value)
{
    writeVector(writer, value.primitiveInstanceIndices, [&writer](std::uint32_t index) { writer.u32(index); });
    writeBounds(writer, value.bounds);
}

[[nodiscard]] bool readCluster(Reader& reader, MeshPrimitiveCluster& value, std::string* errorMessage)
{
    return readVector(reader, value.primitiveInstanceIndices, [&reader, errorMessage](std::uint32_t& index) {
        return reader.u32(index, errorMessage);
    }, errorMessage) && readBounds(reader, value.bounds, errorMessage);
}

void writeMaterial(Writer& writer, const MaterialAsset& value)
{
    writer.string(value.name);
    writeArray(writer, value.baseColor);
    writeArray(writer, value.emissiveColor);
    writer.f32(value.metallicFactor);
    writer.f32(value.roughnessFactor);
    writer.f32(value.normalScale);
    writer.f32(value.occlusionStrength);
    writer.u32(static_cast<std::uint32_t>(value.alphaMode));
    writer.f32(value.alphaCutoff);
    writer.boolean(value.doubleSided);
    writeOptionalIndex(writer, value.baseColorTexture);
    writeOptionalIndex(writer, value.normalTexture);
    writeOptionalIndex(writer, value.metallicRoughnessTexture);
    writeOptionalIndex(writer, value.occlusionTexture);
    writeOptionalIndex(writer, value.emissiveTexture);
}

[[nodiscard]] bool readMaterial(Reader& reader, MaterialAsset& value, std::string* errorMessage)
{
    std::uint32_t alphaMode = 0;
    if (!reader.string(value.name, errorMessage)
        || !readArray(reader, value.baseColor, errorMessage)
        || !readArray(reader, value.emissiveColor, errorMessage)
        || !reader.f32(value.metallicFactor, errorMessage)
        || !reader.f32(value.roughnessFactor, errorMessage)
        || !reader.f32(value.normalScale, errorMessage)
        || !reader.f32(value.occlusionStrength, errorMessage)
        || !reader.u32(alphaMode, errorMessage)
        || !reader.f32(value.alphaCutoff, errorMessage)
        || !reader.boolean(value.doubleSided, errorMessage)) {
        return false;
    }
    value.alphaMode = static_cast<MaterialAlphaMode>(alphaMode);
    return readOptionalIndex(reader, value.baseColorTexture, errorMessage)
        && readOptionalIndex(reader, value.normalTexture, errorMessage)
        && readOptionalIndex(reader, value.metallicRoughnessTexture, errorMessage)
        && readOptionalIndex(reader, value.occlusionTexture, errorMessage)
        && readOptionalIndex(reader, value.emissiveTexture, errorMessage);
}

void writeLight(Writer& writer, const ImportedLightAsset& value)
{
    writer.string(value.name);
    writer.u32(static_cast<std::uint32_t>(value.type));
    writeVec3(writer, value.position);
    writeVec3(writer, value.direction);
    writeArray(writer, value.color);
    writer.f32(value.intensity);
    writer.f32(value.range);
    writer.f32(value.innerConeAngle);
    writer.f32(value.outerConeAngle);
}

[[nodiscard]] bool readLight(Reader& reader, ImportedLightAsset& value, std::string* errorMessage)
{
    std::uint32_t type = 0;
    if (!reader.string(value.name, errorMessage)
        || !reader.u32(type, errorMessage)
        || !readVec3(reader, value.position, errorMessage)
        || !readVec3(reader, value.direction, errorMessage)
        || !readArray(reader, value.color, errorMessage)
        || !reader.f32(value.intensity, errorMessage)
        || !reader.f32(value.range, errorMessage)
        || !reader.f32(value.innerConeAngle, errorMessage)
        || !reader.f32(value.outerConeAngle, errorMessage)) {
        return false;
    }
    value.type = static_cast<ImportedLightType>(type);
    return true;
}

void writeCamera(Writer& writer, const ImportedCameraAsset& value)
{
    writer.string(value.name);
    writer.u32(static_cast<std::uint32_t>(value.projection));
    writeVec3(writer, value.position);
    writeVec3(writer, value.direction);
    writeVec3(writer, value.right);
    writeVec3(writer, value.up);
    writer.f32(value.verticalFovRadians);
    writer.f32(value.aspectRatio);
    writer.f32(value.xMagnitude);
    writer.f32(value.yMagnitude);
    writer.f32(value.nearPlane);
    writer.f32(value.farPlane);
}

[[nodiscard]] bool readCamera(Reader& reader, ImportedCameraAsset& value, std::string* errorMessage)
{
    std::uint32_t projection = 0;
    if (!reader.string(value.name, errorMessage)
        || !reader.u32(projection, errorMessage)
        || !readVec3(reader, value.position, errorMessage)
        || !readVec3(reader, value.direction, errorMessage)
        || !readVec3(reader, value.right, errorMessage)
        || !readVec3(reader, value.up, errorMessage)
        || !reader.f32(value.verticalFovRadians, errorMessage)
        || !reader.f32(value.aspectRatio, errorMessage)
        || !reader.f32(value.xMagnitude, errorMessage)
        || !reader.f32(value.yMagnitude, errorMessage)
        || !reader.f32(value.nearPlane, errorMessage)
        || !reader.f32(value.farPlane, errorMessage)) {
        return false;
    }
    value.projection = static_cast<ImportedCameraProjection>(projection);
    return true;
}

[[nodiscard]] AssetRecord modelRecord(const std::filesystem::path& sourcePath, const ModelAsset& model)
{
    AssetRecord record;
    record.id = model.id;
    record.type = AssetType::Model;
    record.displayName = model.name;
    record.sourceName = sourcePath.filename().string();
    record.cacheFile = std::to_string(record.id.value()) + ".asset.json";
    record.textureCount = model.textures.size();
    for (const auto& primitive : model.primitives) {
        record.vertexCount += primitive.vertices.size();
        record.indexCount += primitive.indices.size();
    }
    return record;
}

[[nodiscard]] AssetRecord textureRecord(const std::filesystem::path& sourcePath, const TextureAsset& texture)
{
    AssetRecord record;
    record.id = texture.id;
    record.type = AssetType::Texture2D;
    record.displayName = texture.name;
    record.sourceName = sourcePath.filename().string();
    record.cacheFile = std::to_string(record.id.value()) + ".asset.json";
    return record;
}

} // namespace

bool isFfultAssetExtension(const std::filesystem::path& path)
{
    return lowerExtension(path) == ".ffult";
}

bool writeFfultModel(const std::filesystem::path& path, const ModelAsset& asset, std::string* errorMessage)
{
    Writer writer(path);
    if (!writer.open(errorMessage)) {
        return false;
    }
    writeHeader(writer, AssetType::Model, asset.id);
    writer.string(asset.name);
    writeVector(writer, asset.primitives, [&writer](const MeshPrimitive& value) { writePrimitive(writer, value); });
    writeVector(writer, asset.primitiveInstances, [&writer](const MeshPrimitiveInstance& value) {
        writePrimitiveInstance(writer, value);
    });
    writeVector(writer, asset.editorInstances, [&writer](const MeshEditorInstance& value) {
        writeEditorInstance(writer, value);
    });
    writeVector(writer, asset.primitiveClusters, [&writer](const MeshPrimitiveCluster& value) { writeCluster(writer, value); });
    writeVector(writer, asset.materials, [&writer](const MaterialAsset& value) { writeMaterial(writer, value); });
    writeVector(writer, asset.textures, [&writer](const TextureAsset& value) { writeTexture(writer, value); });
    writeVector(writer, asset.lights, [&writer](const ImportedLightAsset& value) { writeLight(writer, value); });
    writeVector(writer, asset.cameras, [&writer](const ImportedCameraAsset& value) { writeCamera(writer, value); });
    return writer.good(errorMessage);
}

bool writeFfultTexture(const std::filesystem::path& path, const TextureAsset& asset, std::string* errorMessage)
{
    Writer writer(path);
    if (!writer.open(errorMessage)) {
        return false;
    }
    writeHeader(writer, AssetType::Texture2D, asset.id);
    writeTexture(writer, asset);
    return writer.good(errorMessage);
}

AssetType readFfultAssetType(std::span<const std::uint8_t> bytes, std::string* errorMessage)
{
    Reader reader(bytes);
    AssetType type {};
    AssetId id;
    if (!readHeader(reader, type, id, errorMessage)) {
        return AssetType::Model;
    }
    return type;
}

FfultModelAsset readFfultModel(const std::filesystem::path& sourcePath, std::span<const std::uint8_t> bytes, std::string* errorMessage)
{
    Reader reader(bytes);
    AssetType type {};
    AssetId id;
    if (!readHeader(reader, type, id, errorMessage) || type != AssetType::Model) {
        setError(errorMessage, "FFULT asset is not a model");
        return {};
    }
    auto model = std::make_shared<ModelAsset>();
    model->id = id;
    if (!reader.string(model->name, errorMessage)
        || !readVector(reader, model->primitives, [&reader, errorMessage](MeshPrimitive& value) {
            return readPrimitive(reader, value, errorMessage);
        }, errorMessage)
        || !readVector(reader, model->primitiveInstances, [&reader, errorMessage](MeshPrimitiveInstance& value) {
            return readPrimitiveInstance(reader, value, errorMessage);
        }, errorMessage)
        || !readVector(reader, model->editorInstances, [&reader, errorMessage](MeshEditorInstance& value) {
            return readEditorInstance(reader, value, errorMessage);
        }, errorMessage)
        || !readVector(reader, model->primitiveClusters, [&reader, errorMessage](MeshPrimitiveCluster& value) {
            return readCluster(reader, value, errorMessage);
        }, errorMessage)
        || !readVector(reader, model->materials, [&reader, errorMessage](MaterialAsset& value) {
            return readMaterial(reader, value, errorMessage);
        }, errorMessage)
        || !readVector(reader, model->textures, [&reader, errorMessage](TextureAsset& value) {
            return readTexture(reader, value, errorMessage);
        }, errorMessage)
        || !readVector(reader, model->lights, [&reader, errorMessage](ImportedLightAsset& value) {
            return readLight(reader, value, errorMessage);
        }, errorMessage)
        || !readVector(reader, model->cameras, [&reader, errorMessage](ImportedCameraAsset& value) {
            return readCamera(reader, value, errorMessage);
        }, errorMessage)) {
        return {};
    }
    return {model, modelRecord(sourcePath, *model)};
}

FfultTextureAsset readFfultTexture(const std::filesystem::path& sourcePath, std::span<const std::uint8_t> bytes, std::string* errorMessage)
{
    Reader reader(bytes);
    AssetType type {};
    AssetId id;
    if (!readHeader(reader, type, id, errorMessage) || type != AssetType::Texture2D) {
        setError(errorMessage, "FFULT asset is not a texture");
        return {};
    }
    auto texture = std::make_shared<TextureAsset>();
    if (!readTexture(reader, *texture, errorMessage)) {
        return {};
    }
    texture->id = id;
    return {texture, textureRecord(sourcePath, *texture)};
}

} // namespace projectunity::assets::detail
