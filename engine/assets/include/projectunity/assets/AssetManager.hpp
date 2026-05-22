#pragma once

#include <projectunity/core/StableId.hpp>
#include <projectunity/math/Vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
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

struct MeshVertex {
    math::Vec3 position;
    math::Vec3 normal {0.0F, 1.0F, 0.0F};
    math::Vec3 tangent {1.0F, 0.0F, 0.0F};
    float tangentSign {1.0F};
    std::array<float, 2> texCoord {};
    std::array<float, 4> color {1.0F, 1.0F, 1.0F, 1.0F};
};

struct MeshLod {
    std::vector<std::uint32_t> indices;
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

struct TextureAsset {
    AssetId id;
    std::string name;
    std::uint32_t width {0};
    std::uint32_t height {0};
    std::vector<std::uint8_t> rgba8;
};

struct MaterialAsset {
    std::string name {"Default Material"};
    std::array<float, 4> baseColor {1.0F, 1.0F, 1.0F, 1.0F};
    std::array<float, 3> emissiveColor {0.0F, 0.0F, 0.0F};
    float metallicFactor {1.0F};
    float roughnessFactor {1.0F};
    std::optional<std::size_t> baseColorTexture;
};

struct ModelAsset {
    AssetId id;
    std::string name;
    std::vector<MeshPrimitive> primitives;
    std::vector<MaterialAsset> materials;
    std::vector<TextureAsset> textures;
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
