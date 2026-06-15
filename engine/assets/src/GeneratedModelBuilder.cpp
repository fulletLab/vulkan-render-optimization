#include <projectunity/assets/GeneratedModelBuilder.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace projectunity::assets {
namespace {

constexpr float kPi = 3.14159265358979323846F;

void updateBounds(MeshPrimitive& primitive)
{
    if (primitive.vertices.empty()) {
        return;
    }
    auto minimum = math::Vec3 {
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
    };
    auto maximum = math::Vec3 {
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
    };
    for (const auto& vertex : primitive.vertices) {
        minimum.x = std::min(minimum.x, vertex.position.x);
        minimum.y = std::min(minimum.y, vertex.position.y);
        minimum.z = std::min(minimum.z, vertex.position.z);
        maximum.x = std::max(maximum.x, vertex.position.x);
        maximum.y = std::max(maximum.y, vertex.position.y);
        maximum.z = std::max(maximum.z, vertex.position.z);
    }
    primitive.bounds.minimum = minimum;
    primitive.bounds.maximum = maximum;
    primitive.bounds.center = (minimum + maximum) * 0.5F;
    for (const auto& vertex : primitive.vertices) {
        primitive.bounds.radius = std::max(
            primitive.bounds.radius,
            std::sqrt(math::distanceSquared(primitive.bounds.center, vertex.position)));
    }
}

[[nodiscard]] MaterialAsset defaultMaterial(std::string name)
{
    MaterialAsset material;
    material.name = std::move(name);
    material.baseColor = {0.72F, 0.72F, 0.72F, 1.0F};
    material.metallicFactor = 0.0F;
    material.roughnessFactor = 0.8F;
    return material;
}

void appendFace(
    MeshPrimitive& primitive,
    math::Vec3 normal,
    math::Vec3 tangent,
    std::array<math::Vec3, 4> positions)
{
    const auto first = static_cast<std::uint32_t>(primitive.vertices.size());
    const std::array<std::array<float, 2>, 4> texCoords {{{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}}};
    for (std::size_t index = 0; index < positions.size(); ++index) {
        MeshVertex vertex;
        vertex.position = positions[index];
        vertex.normal = normal;
        vertex.tangent = tangent;
        vertex.texCoord = texCoords[index];
        primitive.vertices.push_back(vertex);
    }
    primitive.indices.insert(primitive.indices.end(), {
        first, first + 1U, first + 2U,
        first, first + 2U, first + 3U,
    });
}

[[nodiscard]] MaterialAsset terrainMaterial(
    const terrain::TerrainMaterialLayer& layer,
    ModelAsset& model,
    const IAssetManager* assetManager)
{
    auto material = defaultMaterial(layer.name);
    material.baseColor = {0.36F, 0.62F, 0.25F, 1.0F};
    material.metallicFactor = layer.metallic;
    material.roughnessFactor = layer.roughness;
    if (assetManager != nullptr && layer.baseColorTextureId.isValid()) {
        if (const auto texture = assetManager->texture(layer.baseColorTextureId)) {
            material.baseColorTexture = model.textures.size();
            model.textures.push_back(*texture);
        }
    }
    if (assetManager != nullptr && layer.normalTextureId.isValid()) {
        if (const auto texture = assetManager->texture(layer.normalTextureId)) {
            material.normalTexture = model.textures.size();
            model.textures.push_back(*texture);
        }
    }
    return material;
}

} // namespace

ModelAsset makeCubeModel(std::string name)
{
    ModelAsset model;
    model.name = std::move(name);
    auto& primitive = model.primitives.emplace_back();
    appendFace(primitive, {0.0F, 0.0F, 1.0F}, {1.0F, 0.0F, 0.0F}, {{{-0.5F, -0.5F, 0.5F}, {0.5F, -0.5F, 0.5F}, {0.5F, 0.5F, 0.5F}, {-0.5F, 0.5F, 0.5F}}});
    appendFace(primitive, {0.0F, 0.0F, -1.0F}, {-1.0F, 0.0F, 0.0F}, {{{0.5F, -0.5F, -0.5F}, {-0.5F, -0.5F, -0.5F}, {-0.5F, 0.5F, -0.5F}, {0.5F, 0.5F, -0.5F}}});
    appendFace(primitive, {1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, -1.0F}, {{{0.5F, -0.5F, 0.5F}, {0.5F, -0.5F, -0.5F}, {0.5F, 0.5F, -0.5F}, {0.5F, 0.5F, 0.5F}}});
    appendFace(primitive, {-1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {{{-0.5F, -0.5F, -0.5F}, {-0.5F, -0.5F, 0.5F}, {-0.5F, 0.5F, 0.5F}, {-0.5F, 0.5F, -0.5F}}});
    appendFace(primitive, {0.0F, 1.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {{{-0.5F, 0.5F, 0.5F}, {0.5F, 0.5F, 0.5F}, {0.5F, 0.5F, -0.5F}, {-0.5F, 0.5F, -0.5F}}});
    appendFace(primitive, {0.0F, -1.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {{{-0.5F, -0.5F, -0.5F}, {0.5F, -0.5F, -0.5F}, {0.5F, -0.5F, 0.5F}, {-0.5F, -0.5F, 0.5F}}});
    updateBounds(primitive);
    model.materials.push_back(defaultMaterial("Default"));
    return model;
}

ModelAsset makeSphereModel(std::string name, std::uint32_t segments, std::uint32_t rings)
{
    segments = std::max(segments, 3U);
    rings = std::max(rings, 2U);
    ModelAsset model;
    model.name = std::move(name);
    auto& primitive = model.primitives.emplace_back();
    for (std::uint32_t ring = 0; ring <= rings; ++ring) {
        const auto v = static_cast<float>(ring) / static_cast<float>(rings);
        const auto phi = v * kPi;
        for (std::uint32_t segment = 0; segment <= segments; ++segment) {
            const auto u = static_cast<float>(segment) / static_cast<float>(segments);
            const auto theta = u * 2.0F * kPi;
            const auto normal = math::Vec3 {
                std::sin(phi) * std::cos(theta),
                std::cos(phi),
                std::sin(phi) * std::sin(theta),
            }.normalized();
            MeshVertex vertex;
            vertex.position = normal * 0.5F;
            vertex.normal = normal;
            vertex.tangent = math::Vec3 {-std::sin(theta), 0.0F, std::cos(theta)}.normalized();
            vertex.texCoord = {u, v};
            primitive.vertices.push_back(vertex);
        }
    }
    const auto stride = segments + 1U;
    for (std::uint32_t ring = 0; ring < rings; ++ring) {
        for (std::uint32_t segment = 0; segment < segments; ++segment) {
            const auto i0 = ring * stride + segment;
            const auto i1 = i0 + 1U;
            const auto i2 = i0 + stride;
            const auto i3 = i2 + 1U;
            primitive.indices.insert(primitive.indices.end(), {i0, i2, i1, i1, i2, i3});
        }
    }
    updateBounds(primitive);
    model.materials.push_back(defaultMaterial("Default"));
    return model;
}

ModelAsset makePlaneModel(std::string name)
{
    ModelAsset model;
    model.name = std::move(name);
    auto& primitive = model.primitives.emplace_back();
    appendFace(primitive, {0.0F, 1.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {{{-0.5F, 0.0F, -0.5F}, {-0.5F, 0.0F, 0.5F}, {0.5F, 0.0F, 0.5F}, {0.5F, 0.0F, -0.5F}}});
    updateBounds(primitive);
    model.materials.push_back(defaultMaterial("Default"));
    return model;
}

ModelAsset makeTerrainModel(
    const terrain::TerrainGenerationResult& terrainResult,
    std::span<const terrain::TerrainMaterialLayer> layers,
    const IAssetManager* assetManager,
    std::string name)
{
    ModelAsset model;
    model.name = std::move(name);
    if (layers.empty()) {
        terrain::TerrainMaterialLayer defaultLayer;
        model.materials.push_back(terrainMaterial(defaultLayer, model, assetManager));
    } else {
        for (const auto& layer : layers) {
            model.materials.push_back(terrainMaterial(layer, model, assetManager));
        }
    }
    model.primitives.reserve(terrainResult.chunks.size());
    for (const auto& source : terrainResult.chunks) {
        auto& primitive = model.primitives.emplace_back();
        primitive.vertices.reserve(source.vertices.size());
        for (const auto& sourceVertex : source.vertices) {
            MeshVertex vertex;
            vertex.position = sourceVertex.position;
            vertex.normal = sourceVertex.normal;
            vertex.tangent = sourceVertex.tangent;
            vertex.texCoord = sourceVertex.texCoord;
            vertex.materialFactors = {0.0F, model.materials.front().roughnessFactor, 1.0F, 1.0F};
            primitive.vertices.push_back(vertex);
        }
        primitive.indices = source.indices;
        for (std::size_t level = 0; level < source.lodIndices.size(); ++level) {
            MeshLod lod;
            lod.indices = source.lodIndices[level];
            lod.error = static_cast<float>(1U << std::min<std::size_t>(level + 1U, 15U));
            primitive.lods.push_back(std::move(lod));
        }
        primitive.bounds = {
            source.bounds.minimum,
            source.bounds.maximum,
            source.bounds.center,
            source.bounds.radius,
        };
    }
    return model;
}

} // namespace projectunity::assets
