#include "MeshPrimitiveBatcher.hpp"

#include "GltfNodeTransforms.hpp"
#include "MeshBounds.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace projectunity::assets::detail {
namespace {

constexpr std::size_t kBatchThreshold = 512;

struct BatchTarget {
    std::size_t materialIndex {0};
    std::uint32_t primitiveIndex {0};
};

[[nodiscard]] GltfMatrix4 toMatrix(const std::array<float, 16>& values)
{
    GltfMatrix4 result {};
    std::transform(values.begin(), values.end(), result.begin(), [](float value) {
        return static_cast<double>(value);
    });
    return result;
}

[[nodiscard]] MeshPrimitiveInstance identityInstance(std::uint32_t primitiveIndex, const MeshBounds& bounds)
{
    MeshPrimitiveInstance instance;
    instance.primitiveIndex = primitiveIndex;
    instance.bounds = bounds;
    return instance;
}

[[nodiscard]] bool sameMaterial(const MaterialAsset& lhs, const MaterialAsset& rhs) noexcept
{
    return lhs.baseColor == rhs.baseColor
        && lhs.emissiveColor == rhs.emissiveColor
        && lhs.metallicFactor == rhs.metallicFactor
        && lhs.roughnessFactor == rhs.roughnessFactor
        && lhs.normalScale == rhs.normalScale
        && lhs.occlusionStrength == rhs.occlusionStrength
        && lhs.alphaMode == rhs.alphaMode
        && lhs.alphaCutoff == rhs.alphaCutoff
        && lhs.doubleSided == rhs.doubleSided
        && lhs.baseColorTexture == rhs.baseColorTexture
        && lhs.normalTexture == rhs.normalTexture
        && lhs.metallicRoughnessTexture == rhs.metallicRoughnessTexture
        && lhs.occlusionTexture == rhs.occlusionTexture
        && lhs.emissiveTexture == rhs.emissiveTexture;
}
[[nodiscard]] bool sameTexture(const TextureAsset& lhs, const TextureAsset& rhs) noexcept
{
    return lhs.width == rhs.width
        && lhs.height == rhs.height
        && lhs.sampler == rhs.sampler
        && lhs.rgba8 == rhs.rgba8;
}
void remapTexture(std::optional<std::size_t>& index, const std::vector<std::size_t>& remap)
{
    if (index.has_value() && *index < remap.size()) {
        index = remap[*index];
    }
}
void deduplicateTextures(ModelAsset& model)
{
    std::vector<TextureAsset> unique;
    std::vector<std::size_t> remap(model.textures.size(), 0U);
    for (std::size_t index = 0; index < model.textures.size(); ++index) {
        const auto existing = std::find_if(unique.begin(), unique.end(), [&model, index](const TextureAsset& texture) {
            return sameTexture(texture, model.textures[index]);
        });
        if (existing == unique.end()) {
            remap[index] = unique.size();
            unique.push_back(model.textures[index]);
        } else {
            remap[index] = static_cast<std::size_t>(std::distance(unique.begin(), existing));
        }
    }
    for (auto& material : model.materials) {
        remapTexture(material.baseColorTexture, remap);
        remapTexture(material.normalTexture, remap);
        remapTexture(material.metallicRoughnessTexture, remap);
        remapTexture(material.occlusionTexture, remap);
        remapTexture(material.emissiveTexture, remap);
    }
    model.textures = std::move(unique);
}

void bakeFlatBaseColors(ModelAsset& model)
{
    for (auto& primitive : model.primitives) {
        if (primitive.materialIndex >= model.materials.size()) {
            continue;
        }
        const auto& material = model.materials[primitive.materialIndex];
        for (auto& vertex : primitive.vertices) {
            vertex.color[0] *= material.baseColor[0];
            vertex.color[1] *= material.baseColor[1];
            vertex.color[2] *= material.baseColor[2];
            vertex.color[3] *= material.baseColor[3];
            vertex.materialFactors[0] *= material.metallicFactor;
            vertex.materialFactors[1] *= material.roughnessFactor;
            vertex.materialFactors[2] *= material.normalScale;
            vertex.materialFactors[3] *= material.occlusionStrength;
        }
    }
    for (auto& material : model.materials) {
        material.baseColor = {1.0F, 1.0F, 1.0F, 1.0F};
        material.metallicFactor = 1.0F;
        material.roughnessFactor = 1.0F;
        material.normalScale = 1.0F;
        material.occlusionStrength = 1.0F;
    }
}

void deduplicateMaterials(ModelAsset& model)
{
    std::vector<MaterialAsset> unique;
    std::vector<std::size_t> remap(model.materials.size(), 0U);
    for (std::size_t index = 0; index < model.materials.size(); ++index) {
        const auto existing = std::find_if(unique.begin(), unique.end(), [&model, index](const MaterialAsset& material) {
            return sameMaterial(material, model.materials[index]);
        });
        if (existing == unique.end()) {
            remap[index] = unique.size();
            unique.push_back(model.materials[index]);
        } else {
            remap[index] = static_cast<std::size_t>(std::distance(unique.begin(), existing));
        }
    }
    for (auto& primitive : model.primitives) {
        if (primitive.materialIndex < remap.size()) {
            primitive.materialIndex = remap[primitive.materialIndex];
        }
    }
    model.materials = std::move(unique);
}

} // namespace

void batchModelPrimitives(ModelAsset& model)
{
    deduplicateTextures(model);
    if (model.primitives.size() <= kBatchThreshold || model.primitiveInstances.size() <= kBatchThreshold) {
        deduplicateMaterials(model);
        return;
    }
    bakeFlatBaseColors(model);
    deduplicateMaterials(model);

    std::vector<MeshPrimitive> batched;
    std::vector<BatchTarget> targets;
    for (const auto& instance : model.primitiveInstances) {
        if (instance.primitiveIndex >= model.primitives.size()) {
            continue;
        }
        const auto& source = model.primitives[instance.primitiveIndex];
        auto target = std::find_if(targets.begin(), targets.end(), [&source](const BatchTarget& value) {
            return value.materialIndex == source.materialIndex;
        });
        if (target == targets.end()) {
            const auto outputIndex = static_cast<std::uint32_t>(batched.size());
            MeshPrimitive next;
            next.materialIndex = source.materialIndex;
            batched.push_back(std::move(next));
            targets.push_back({source.materialIndex, outputIndex});
            target = targets.end() - 1;
        }

        auto transformed = source;
        applyGltfTransform(transformed, toMatrix(instance.transform));
        auto& destination = batched[target->primitiveIndex];
        if (destination.vertices.size() + transformed.vertices.size() > std::numeric_limits<std::uint32_t>::max()) {
            return;
        }
        const auto vertexOffset = static_cast<std::uint32_t>(destination.vertices.size());
        destination.vertices.insert(destination.vertices.end(), transformed.vertices.begin(), transformed.vertices.end());
        destination.indices.reserve(destination.indices.size() + transformed.indices.size());
        for (const auto index : transformed.indices) {
            destination.indices.push_back(vertexOffset + index);
        }
    }

    std::vector<MeshPrimitiveInstance> instances;
    instances.reserve(batched.size());
    for (std::uint32_t index = 0; index < batched.size(); ++index) {
        updateMeshBounds(batched[index]);
        instances.push_back(identityInstance(index, batched[index].bounds));
    }
    model.primitives = std::move(batched);
    model.primitiveInstances = std::move(instances);
}

} // namespace projectunity::assets::detail
