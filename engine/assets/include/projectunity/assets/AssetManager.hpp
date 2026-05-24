#pragma once

#include <projectunity/core/StableId.hpp>
#include <projectunity/math/Vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace projectunity::assets {

using AssetId = core::StableId;

enum class AssetType : std::uint8_t {
    Texture2D,
    Model,
};

enum class MaterialAlphaMode : std::uint8_t {
    Opaque,
    Mask,
    Blend,
};

enum class TextureWrapMode : std::uint8_t {
    Repeat,
    MirroredRepeat,
    ClampToEdge,
};

enum class TextureFilterMode : std::uint8_t {
    Nearest,
    Linear,
};

enum class TextureGpuFormat : std::uint16_t {
    Rgba8Unorm,
    Rgba8Srgb,
    Bc1RgbUnorm,
    Bc1RgbSrgb,
    Bc1RgbaUnorm,
    Bc1RgbaSrgb,
    Bc2Unorm,
    Bc2Srgb,
    Bc3Unorm,
    Bc3Srgb,
    Bc5Unorm,
    Bc5Snorm,
    Bc7Unorm,
    Bc7Srgb,
    Etc2Rgba8Unorm,
    Etc2Rgba8Srgb,
    Astc4x4Unorm,
    Astc4x4Srgb,
    Astc5x4Unorm,
    Astc5x4Srgb,
    Astc5x5Unorm,
    Astc5x5Srgb,
    Astc6x5Unorm,
    Astc6x5Srgb,
    Astc6x6Unorm,
    Astc6x6Srgb,
    Astc8x5Unorm,
    Astc8x5Srgb,
    Astc8x6Unorm,
    Astc8x6Srgb,
    Astc8x8Unorm,
    Astc8x8Srgb,
    Astc10x5Unorm,
    Astc10x5Srgb,
    Astc10x6Unorm,
    Astc10x6Srgb,
    Astc10x8Unorm,
    Astc10x8Srgb,
    Astc10x10Unorm,
    Astc10x10Srgb,
    Astc12x10Unorm,
    Astc12x10Srgb,
    Astc12x12Unorm,
    Astc12x12Srgb,
};

enum class ImportedLightType : std::uint8_t {
    Directional,
    Point,
    Spot,
};

enum class ImportedCameraProjection : std::uint8_t {
    Perspective,
    Orthographic,
};

struct TextureSamplerAsset {
    TextureFilterMode magnificationFilter {TextureFilterMode::Linear};
    TextureFilterMode minificationFilter {TextureFilterMode::Linear};
    TextureFilterMode mipmapFilter {TextureFilterMode::Linear};
    TextureWrapMode wrapU {TextureWrapMode::Repeat};
    TextureWrapMode wrapV {TextureWrapMode::Repeat};
    bool useMipmaps {true};

    [[nodiscard]] bool operator==(const TextureSamplerAsset&) const noexcept = default;
};

struct TextureMipLevel {
    std::uint32_t width {0};
    std::uint32_t height {0};
    std::vector<std::uint8_t> bytes;

    [[nodiscard]] bool operator==(const TextureMipLevel&) const noexcept = default;
};

struct MeshVertex {
    math::Vec3 position;
    math::Vec3 normal {0.0F, 1.0F, 0.0F};
    math::Vec3 tangent {1.0F, 0.0F, 0.0F};
    float tangentSign {1.0F};
    std::array<float, 2> texCoord {};
    std::array<float, 4> color {1.0F, 1.0F, 1.0F, 1.0F};
    std::array<float, 4> materialFactors {1.0F, 1.0F, 1.0F, 1.0F};
};

struct MeshLod {
    std::vector<std::uint32_t> indices;
    float error {0.0F};
};

struct MeshBounds {
    math::Vec3 minimum;
    math::Vec3 maximum;
    math::Vec3 center;
    float radius {0.0F};
};

struct MeshPrimitive {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<MeshLod> lods;
    MeshBounds bounds;
    std::size_t materialIndex {0};
};

struct MeshPrimitiveInstance {
    std::uint32_t primitiveIndex {0};
    std::array<float, 16> transform {
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    };
    MeshBounds bounds;
    bool flipsWinding {false};
};

struct MeshPrimitiveCluster {
    std::vector<std::uint32_t> primitiveInstanceIndices;
    MeshBounds bounds;
};

struct TextureAsset {
    AssetId id;
    std::string name;
    std::uint32_t width {0};
    std::uint32_t height {0};
    TextureGpuFormat gpuFormat {TextureGpuFormat::Rgba8Unorm};
    TextureSamplerAsset sampler;
    std::vector<std::uint8_t> rgba8;
    std::vector<float> rgba32f;
    std::vector<TextureMipLevel> gpuMipLevels;
};

struct MaterialAsset {
    std::string name {"Default Material"};
    std::array<float, 4> baseColor {1.0F, 1.0F, 1.0F, 1.0F};
    std::array<float, 3> emissiveColor {0.0F, 0.0F, 0.0F};
    float metallicFactor {1.0F};
    float roughnessFactor {1.0F};
    float normalScale {1.0F};
    float occlusionStrength {1.0F};
    MaterialAlphaMode alphaMode {MaterialAlphaMode::Opaque};
    float alphaCutoff {0.5F};
    bool doubleSided {false};
    std::optional<std::size_t> baseColorTexture;
    std::optional<std::size_t> normalTexture;
    std::optional<std::size_t> metallicRoughnessTexture;
    std::optional<std::size_t> occlusionTexture;
    std::optional<std::size_t> emissiveTexture;
};

struct ImportedLightAsset {
    std::string name {"Light"};
    ImportedLightType type {ImportedLightType::Directional};
    math::Vec3 position;
    math::Vec3 direction {0.0F, 0.0F, -1.0F};
    std::array<float, 3> color {1.0F, 1.0F, 1.0F};
    float intensity {1.0F};
    float range {0.0F};
    float innerConeAngle {0.0F};
    float outerConeAngle {0.7853981634F};
};

struct ImportedCameraAsset {
    std::string name {"Camera"};
    ImportedCameraProjection projection {ImportedCameraProjection::Perspective};
    math::Vec3 position;
    math::Vec3 direction {0.0F, 0.0F, -1.0F};
    math::Vec3 right {1.0F, 0.0F, 0.0F};
    math::Vec3 up {0.0F, 1.0F, 0.0F};
    float verticalFovRadians {1.04719755F};
    float aspectRatio {0.0F};
    float xMagnitude {1.0F};
    float yMagnitude {1.0F};
    float nearPlane {0.05F};
    float farPlane {4000.0F};
};

struct ModelAsset {
    AssetId id;
    std::string name;
    std::vector<MeshPrimitive> primitives;
    std::vector<MeshPrimitiveInstance> primitiveInstances;
    std::vector<MeshPrimitiveCluster> primitiveClusters;
    std::vector<MaterialAsset> materials;
    std::vector<TextureAsset> textures;
    std::vector<ImportedLightAsset> lights;
    std::vector<ImportedCameraAsset> cameras;
};

struct AssetRecord {
    AssetId id;
    AssetType type {AssetType::Model};
    std::string displayName;
    std::string sourceName;
    std::string cacheFile;
    std::size_t vertexCount {0};
    std::size_t indexCount {0};
    std::size_t textureCount {0};
};

struct AssetImportResult {
    bool success {false};
    AssetRecord record;
    std::string error;
};

struct AssetImportProgress {
    int percent {1};
    std::string stage;
};

using AssetImportProgressCallback = std::function<void(const AssetImportProgress&)>;

class IAssetManager {
public:
    virtual ~IAssetManager() = default;

    [[nodiscard]] virtual std::shared_ptr<const ModelAsset> model(AssetId id) const = 0;
    [[nodiscard]] virtual std::shared_ptr<const TextureAsset> texture(AssetId id) const = 0;
    [[nodiscard]] virtual std::vector<AssetRecord> records() const = 0;
};

class AssetManager final : public IAssetManager {
public:
    explicit AssetManager(std::filesystem::path cacheRoot);

    [[nodiscard]] AssetImportResult importAsset(const std::filesystem::path& sourcePath);
    [[nodiscard]] AssetImportResult importModel(const std::filesystem::path& sourcePath);
    [[nodiscard]] AssetImportResult importTexture(const std::filesystem::path& sourcePath);
    [[nodiscard]] AssetImportResult importAsset(
        const std::filesystem::path& sourcePath,
        const AssetImportProgressCallback& progress);
    [[nodiscard]] AssetImportResult importModel(
        const std::filesystem::path& sourcePath,
        const AssetImportProgressCallback& progress);
    [[nodiscard]] AssetImportResult importTexture(
        const std::filesystem::path& sourcePath,
        const AssetImportProgressCallback& progress);

    [[nodiscard]] std::shared_ptr<const ModelAsset> model(AssetId id) const override;
    [[nodiscard]] std::shared_ptr<const TextureAsset> texture(AssetId id) const override;
    [[nodiscard]] std::vector<AssetRecord> records() const override;
    [[nodiscard]] const std::filesystem::path& cacheRoot() const noexcept;

private:
    [[nodiscard]] bool writeCacheRecord(const AssetRecord& record, std::string* errorMessage) const;
    void storeRecord(const AssetRecord& record);

    std::filesystem::path cacheRoot_;
    mutable std::mutex mutex_;
    std::vector<AssetRecord> records_;
    std::vector<std::shared_ptr<const ModelAsset>> models_;
    std::vector<std::shared_ptr<const TextureAsset>> textures_;
};

[[nodiscard]] const char* toString(AssetType type) noexcept;

} // namespace projectunity::assets
