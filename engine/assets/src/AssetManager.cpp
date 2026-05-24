#include <projectunity/assets/AssetManager.hpp>
#include "AssetImportUtils.hpp"
#include "GltfAttributeReader.hpp"
#include "GltfImageLoader.hpp"
#include "GltfNodeTransforms.hpp"
#include "GltfSceneObjects.hpp"
#include "GltfTextureImport.hpp"
#include "KtxTextureImport.hpp"
#include "MeshBounds.hpp"
#include "MeshLodGenerator.hpp"
#include "MeshPrimitiveChunker.hpp"
#include "MeshPrimitiveBatcher.hpp"
#include "MeshPrimitiveSignature.hpp"
#include "StbTextureImport.hpp"
#include <projectunity/core/Log.hpp>
#include <meshoptimizer.h>
#include <mikktspace.h>
#include <tiny_gltf.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <sstream>
#include <span>
#include <utility>
namespace projectunity::assets {
namespace {
constexpr float kTangentEpsilon = 0.000001F;
using detail::makeId;
using detail::readBytes; using detail::readColorAttribute;
using detail::readFloatAttribute; using detail::readIndices;
using detail::setError; using detail::GltfMatrix4;
using detail::applyGltfTransform;
using detail::gltfToEngineMatrix;
using detail::gltfToEngineInstanceMatrix;
using detail::gltfNodeMatrix;
using detail::identityGltfMatrix;
using detail::multiplyGltfMatrices;
struct ImportedModel {
    std::shared_ptr<ModelAsset> asset;
    AssetRecord record;
};
struct ImportedPrimitiveKey {
    int meshIndex {-1};
    int primitiveIndex {-1};
    std::uint32_t outputIndex {0};
};
struct ImportedPrimitiveSignature {
    std::uint64_t signature {0};
    std::uint32_t outputIndex {0};
};
[[nodiscard]] std::string lowerExtension(std::filesystem::path path)
{
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension;
}
void reportProgress(const AssetImportProgressCallback& progress, int percent, std::string stage)
{
    if (progress) {
        progress({std::clamp(percent, 1, 100), std::move(stage)});
    }
}
[[nodiscard]] bool normalize(math::Vec3& value)
{
    const auto length = value.length();
    if (length <= kTangentEpsilon || !std::isfinite(length)) {
        return false;
    }
    value = value / length;
    return true;
}
void generateNormals(MeshPrimitive& primitive)
{
    for (auto& vertex : primitive.vertices) {
        vertex.normal = {};
    }
    for (std::size_t index = 2; index < primitive.indices.size(); index += 3) {
        const auto i0 = primitive.indices[index - 2];
        const auto i1 = primitive.indices[index - 1];
        const auto i2 = primitive.indices[index];
        auto normal = math::cross(
            primitive.vertices[i1].position - primitive.vertices[i0].position,
            primitive.vertices[i2].position - primitive.vertices[i0].position);
        if (!normalize(normal)) {
            continue;
        }
        primitive.vertices[i0].normal += normal;
        primitive.vertices[i1].normal += normal;
        primitive.vertices[i2].normal += normal;
    }
    for (auto& vertex : primitive.vertices) {
        if (!normalize(vertex.normal)) {
            vertex.normal = {0.0F, 1.0F, 0.0F};
        }
    }
}
struct TangentContext {
    MeshPrimitive* primitive {nullptr};
};
[[nodiscard]] const MeshVertex& tangentVertex(const SMikkTSpaceContext* context, int face, int vertex)
{
    const auto* tangentContext = static_cast<const TangentContext*>(context->m_pUserData);
    const auto index = tangentContext->primitive->indices[static_cast<std::size_t>(face * 3 + vertex)];
    return tangentContext->primitive->vertices[index];
}
[[nodiscard]] MeshVertex& tangentVertex(SMikkTSpaceContext* context, int face, int vertex)
{
    auto* tangentContext = static_cast<TangentContext*>(context->m_pUserData);
    const auto index = tangentContext->primitive->indices[static_cast<std::size_t>(face * 3 + vertex)];
    return tangentContext->primitive->vertices[index];
}
[[nodiscard]] int tangentFaceCount(const SMikkTSpaceContext* context)
{
    const auto* tangentContext = static_cast<const TangentContext*>(context->m_pUserData);
    return static_cast<int>(tangentContext->primitive->indices.size() / 3U);
}
[[nodiscard]] int tangentVerticesPerFace(const SMikkTSpaceContext*, int)
{
    return 3;
}
void tangentPosition(const SMikkTSpaceContext* context, float output[], int face, int vertex)
{
    const auto& value = tangentVertex(context, face, vertex).position;
    output[0] = value.x;
    output[1] = value.y;
    output[2] = value.z;
}
void tangentNormal(const SMikkTSpaceContext* context, float output[], int face, int vertex)
{
    const auto& value = tangentVertex(context, face, vertex).normal;
    output[0] = value.x;
    output[1] = value.y;
    output[2] = value.z;
}
void tangentTexCoord(const SMikkTSpaceContext* context, float output[], int face, int vertex)
{
    const auto& value = tangentVertex(context, face, vertex).texCoord;
    output[0] = value[0];
    output[1] = value[1];
}
void tangentWriteBasic(
    const SMikkTSpaceContext* context,
    const float tangent[],
    float sign,
    int face,
    int vertex)
{
    auto& value = tangentVertex(const_cast<SMikkTSpaceContext*>(context), face, vertex);
    value.tangent = {tangent[0], tangent[1], tangent[2]};
    value.tangentSign = sign;
}
void fallbackTangents(MeshPrimitive& primitive)
{
    for (auto& vertex : primitive.vertices) {
        const auto axis = std::fabs(vertex.normal.x) < 0.85F
            ? math::Vec3 {1.0F, 0.0F, 0.0F}
            : math::Vec3 {0.0F, 0.0F, 1.0F};
        auto tangent = math::cross(axis, vertex.normal);
        if (!normalize(tangent)) {
            tangent = {1.0F, 0.0F, 0.0F};
        }
        vertex.tangent = tangent;
        vertex.tangentSign = 1.0F;
    }
}
void generateTangents(MeshPrimitive& primitive)
{
    TangentContext tangentData {&primitive};
    SMikkTSpaceInterface interfaceData {};
    interfaceData.m_getNumFaces = tangentFaceCount;
    interfaceData.m_getNumVerticesOfFace = tangentVerticesPerFace;
    interfaceData.m_getPosition = tangentPosition;
    interfaceData.m_getNormal = tangentNormal;
    interfaceData.m_getTexCoord = tangentTexCoord;
    interfaceData.m_setTSpaceBasic = tangentWriteBasic;
    SMikkTSpaceContext context {};
    context.m_pInterface = &interfaceData;
    context.m_pUserData = &tangentData;
    if (genTangSpaceDefault(&context) == 0) {
        fallbackTangents(primitive);
        core::logWarning(core::LogCategory::Assets, "MikkTSpace tangent generation used a fallback tangent basis");
    }
}

void optimizePrimitive(MeshPrimitive& primitive)
{
    if (primitive.vertices.empty() || primitive.indices.size() < 3) {
        return;
    }
    meshopt_optimizeVertexCache(
        primitive.indices.data(),
        primitive.indices.data(),
        primitive.indices.size(),
        primitive.vertices.size());
    meshopt_optimizeOverdraw(
        primitive.indices.data(),
        primitive.indices.data(),
        primitive.indices.size(),
        &primitive.vertices.front().position.x,
        primitive.vertices.size(),
        sizeof(MeshVertex),
        1.05F);
    std::vector<MeshVertex> reordered(primitive.vertices.size());
    meshopt_optimizeVertexFetch(
        reordered.data(),
        primitive.indices.data(),
        primitive.indices.size(),
        primitive.vertices.data(),
        primitive.vertices.size(),
        sizeof(MeshVertex));
    primitive.vertices = std::move(reordered);
    detail::rebuildSimplificationLods(primitive);
}
[[nodiscard]] std::array<float, 16> toFloatMatrix(GltfMatrix4 matrix)
{
    std::array<float, 16> result {};
    std::transform(matrix.begin(), matrix.end(), result.begin(), [](double value) {
        return static_cast<float>(value);
    });
    return result;
}
[[nodiscard]] MeshBounds transformBounds(const MeshBounds& bounds, GltfMatrix4 transform)
{
    const std::array<math::Vec3, 8> corners {{
        {bounds.minimum.x, bounds.minimum.y, bounds.minimum.z},
        {bounds.maximum.x, bounds.minimum.y, bounds.minimum.z},
        {bounds.minimum.x, bounds.maximum.y, bounds.minimum.z},
        {bounds.maximum.x, bounds.maximum.y, bounds.minimum.z},
        {bounds.minimum.x, bounds.minimum.y, bounds.maximum.z},
        {bounds.maximum.x, bounds.minimum.y, bounds.maximum.z},
        {bounds.minimum.x, bounds.maximum.y, bounds.maximum.z},
        {bounds.maximum.x, bounds.maximum.y, bounds.maximum.z},
    }};
    MeshPrimitive primitive;
    primitive.vertices.reserve(corners.size());
    for (const auto& corner : corners) {
        primitive.vertices.push_back({detail::transformGltfPoint(transform, corner)});
    }
    detail::updateMeshBounds(primitive);
    return primitive.bounds;
}
[[nodiscard]] TextureWrapMode textureWrapMode(int value)
{
    if (value == TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT) {
        return TextureWrapMode::MirroredRepeat;
    }
    if (value == TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE) {
        return TextureWrapMode::ClampToEdge;
    }
    return TextureWrapMode::Repeat;
}
[[nodiscard]] TextureSamplerAsset textureSampler(const tinygltf::Model& gltf, const tinygltf::Texture& texture)
{
    TextureSamplerAsset sampler;
    if (texture.sampler < 0 || static_cast<std::size_t>(texture.sampler) >= gltf.samplers.size()) {
        return sampler;
    }
    const auto& source = gltf.samplers[static_cast<std::size_t>(texture.sampler)];
    sampler.wrapU = textureWrapMode(source.wrapS);
    sampler.wrapV = textureWrapMode(source.wrapT);
    sampler.magnificationFilter = source.magFilter == TINYGLTF_TEXTURE_FILTER_NEAREST
        ? TextureFilterMode::Nearest
        : TextureFilterMode::Linear;
    switch (source.minFilter) {
    case TINYGLTF_TEXTURE_FILTER_NEAREST:
        sampler.minificationFilter = TextureFilterMode::Nearest;
        sampler.useMipmaps = false;
        break;
    case TINYGLTF_TEXTURE_FILTER_LINEAR:
        sampler.minificationFilter = TextureFilterMode::Linear;
        sampler.useMipmaps = false;
        break;
    case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
        sampler.minificationFilter = TextureFilterMode::Nearest;
        sampler.mipmapFilter = TextureFilterMode::Nearest;
        break;
    case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
        sampler.mipmapFilter = TextureFilterMode::Nearest;
        break;
    case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
        sampler.minificationFilter = TextureFilterMode::Nearest;
        break;
    default:
        break;
    }
    return sampler;
}
[[nodiscard]] MaterialAsset defaultMaterial()
{
    return {};
}
[[nodiscard]] MaterialAlphaMode materialAlphaMode(const std::string& gltfAlphaMode)
{
    if (gltfAlphaMode == "MASK") {
        return MaterialAlphaMode::Mask;
    }
    if (gltfAlphaMode == "BLEND") {
        return MaterialAlphaMode::Blend;
    }
    return MaterialAlphaMode::Opaque;
}
[[nodiscard]] bool importPrimitive(
    const tinygltf::Model& gltf,
    const tinygltf::Primitive& source,
    std::size_t materialCount,
    MeshPrimitive& output,
    std::string* errorMessage)
{
    if (source.mode != TINYGLTF_MODE_TRIANGLES) {
        setError(errorMessage, "Only triangle glTF mesh primitives are supported");
        return false;
    }
    const auto positionIt = source.attributes.find("POSITION");
    if (positionIt == source.attributes.end()) {
        setError(errorMessage, "glTF mesh primitive is missing POSITION");
        return false;
    }
    std::vector<float> positions;
    if (!readFloatAttribute(gltf, positionIt->second, 3, positions, errorMessage)) {
        return false;
    }
    const auto vertexCount = positions.size() / 3U;
    if (vertexCount == 0 || vertexCount > std::numeric_limits<std::uint32_t>::max()) {
        setError(errorMessage, "glTF mesh primitive has an invalid vertex count");
        return false;
    }
    output.vertices.resize(vertexCount);
    for (std::size_t index = 0; index < vertexCount; ++index) {
        output.vertices[index].position = {
            positions[index * 3U],
            positions[index * 3U + 1U],
            positions[index * 3U + 2U],
        };
    }
    std::vector<float> normals;
    const auto normalIt = source.attributes.find("NORMAL");
    if (normalIt != source.attributes.end()) {
        if (!readFloatAttribute(gltf, normalIt->second, 3, normals, errorMessage)
            || normals.size() / 3U != vertexCount) {
            setError(errorMessage, "glTF mesh normals do not match positions");
            return false;
        }
        for (std::size_t index = 0; index < vertexCount; ++index) {
            output.vertices[index].normal = {
                normals[index * 3U],
                normals[index * 3U + 1U],
                normals[index * 3U + 2U],
            };
            if (!normalize(output.vertices[index].normal)) {
                output.vertices[index].normal = {0.0F, 1.0F, 0.0F};
            }
        }
    }
    std::vector<float> texCoords;
    const auto texCoordIt = source.attributes.find("TEXCOORD_0");
    if (texCoordIt != source.attributes.end()) {
        if (!readFloatAttribute(gltf, texCoordIt->second, 2, texCoords, errorMessage)
            || texCoords.size() / 2U != vertexCount) {
            setError(errorMessage, "glTF mesh texcoords do not match positions");
            return false;
        }
        for (std::size_t index = 0; index < vertexCount; ++index) {
            output.vertices[index].texCoord = {
                texCoords[index * 2U],
                texCoords[index * 2U + 1U],
            };
        }
    }
    std::vector<float> colors;
    const auto colorIt = source.attributes.find("COLOR_0");
    if (colorIt != source.attributes.end()) {
        if (!readColorAttribute(gltf, colorIt->second, colors, errorMessage)
            || colors.size() / 4U != vertexCount) {
            setError(errorMessage, "glTF mesh vertex colors do not match positions");
            return false;
        }
        for (std::size_t index = 0; index < vertexCount; ++index) {
            output.vertices[index].color = {
                colors[index * 4U],
                colors[index * 4U + 1U],
                colors[index * 4U + 2U],
                colors[index * 4U + 3U],
            };
        }
    }
    if (!readIndices(gltf, source.indices, vertexCount, output.indices, errorMessage)
        || output.indices.size() % 3U != 0) {
        setError(errorMessage, "glTF mesh triangle indices are invalid");
        return false;
    }
    if (normalIt == source.attributes.end()) {
        generateNormals(output);
    }
    generateTangents(output);
    optimizePrimitive(output);
    detail::updateMeshBounds(output);
    output.materialIndex = source.material >= 0 && static_cast<std::size_t>(source.material) < materialCount
        ? static_cast<std::size_t>(source.material)
        : 0U;
    return true;
}
[[nodiscard]] bool ensureImportedPrimitive(
    const tinygltf::Model& gltf,
    int meshIndex,
    int primitiveIndex,
    std::size_t materialCount,
    ModelAsset& model,
    std::vector<ImportedPrimitiveKey>& primitiveMap,
    std::vector<ImportedPrimitiveSignature>& signatures,
    std::string* errorMessage,
    std::uint32_t& outputIndex)
{
    for (const auto& entry : primitiveMap) {
        if (entry.meshIndex == meshIndex && entry.primitiveIndex == primitiveIndex) {
            outputIndex = entry.outputIndex;
            return true;
        }
    }
    if (meshIndex < 0 || primitiveIndex < 0
        || static_cast<std::size_t>(meshIndex) >= gltf.meshes.size()
        || static_cast<std::size_t>(primitiveIndex) >= gltf.meshes[static_cast<std::size_t>(meshIndex)].primitives.size()) {
        setError(errorMessage, "glTF mesh node references an invalid primitive");
        return false;
    }
    MeshPrimitive imported;
    const auto& primitive = gltf.meshes[static_cast<std::size_t>(meshIndex)].primitives[static_cast<std::size_t>(primitiveIndex)];
    if (!importPrimitive(gltf, primitive, materialCount, imported, errorMessage)) {
        return false;
    }
    applyGltfTransform(imported, gltfToEngineMatrix(identityGltfMatrix()));
    const auto signature = detail::meshPrimitiveSignature(imported);
    for (const auto& existing : signatures) {
        if (existing.signature == signature
            && detail::meshPrimitivesEqual(model.primitives[existing.outputIndex], imported)) {
            outputIndex = existing.outputIndex;
            primitiveMap.push_back({meshIndex, primitiveIndex, outputIndex});
            return true;
        }
    }
    outputIndex = static_cast<std::uint32_t>(model.primitives.size());
    model.primitives.push_back(std::move(imported));
    primitiveMap.push_back({meshIndex, primitiveIndex, outputIndex});
    signatures.push_back({signature, outputIndex});
    return true;
}
[[nodiscard]] bool importNodePrimitives(
    const tinygltf::Model& gltf,
    int nodeIndex,
    GltfMatrix4 parentTransform,
    ModelAsset& model,
    std::vector<ImportedPrimitiveKey>& primitiveMap,
    std::vector<ImportedPrimitiveSignature>& signatures,
    std::size_t materialCount,
    std::string* errorMessage,
    int depth = 0)
{
    if (nodeIndex < 0 || static_cast<std::size_t>(nodeIndex) >= gltf.nodes.size() || depth > 256) {
        setError(errorMessage, "glTF node hierarchy is invalid");
        return false;
    }
    const auto& node = gltf.nodes[static_cast<std::size_t>(nodeIndex)];
    const auto worldTransform = multiplyGltfMatrices(parentTransform, gltfNodeMatrix(node));
    if (node.mesh >= 0 && static_cast<std::size_t>(node.mesh) < gltf.meshes.size()) {
        const auto& mesh = gltf.meshes[static_cast<std::size_t>(node.mesh)];
        const auto instanceTransform = gltfToEngineInstanceMatrix(worldTransform);
        for (std::size_t primitiveIndex = 0; primitiveIndex < mesh.primitives.size(); ++primitiveIndex) {
            std::uint32_t outputIndex = 0;
            if (!ensureImportedPrimitive(
                    gltf,
                    node.mesh,
                    static_cast<int>(primitiveIndex),
                    materialCount,
                    model,
                    primitiveMap,
                    signatures,
                    errorMessage,
                    outputIndex)) {
                return false;
            }
            MeshPrimitiveInstance instance;
            instance.primitiveIndex = outputIndex;
            instance.transform = toFloatMatrix(instanceTransform);
            instance.bounds = transformBounds(model.primitives[outputIndex].bounds, instanceTransform);
            instance.flipsWinding = detail::determinantGltfLinear(instanceTransform) < 0.0;
            model.primitiveInstances.push_back(instance);
        }
    }
    for (const auto child : node.children) {
        if (!importNodePrimitives(gltf, child, worldTransform, model, primitiveMap, signatures, materialCount, errorMessage, depth + 1)) {
            return false;
        }
    }
    return true;
}
[[nodiscard]] ImportedModel importGltfModel(
    const std::filesystem::path& sourcePath,
    std::span<const std::uint8_t> sourceBytes,
    const AssetImportProgressCallback& progress,
    std::string* errorMessage)
{
    tinygltf::TinyGLTF loader;
    detail::configureGltfImageLoader(loader);
    tinygltf::Model gltf;
    std::string warning;
    std::string loaderError;
    const auto extension = lowerExtension(sourcePath);
    if (sourceBytes.size() > std::numeric_limits<unsigned int>::max()) {
        setError(errorMessage, "Asset source file is too large for TinyGLTF memory import");
        return {};
    }
    reportProgress(progress, 18, "Parsing glTF");
    const auto baseDirectory = sourcePath.parent_path().string();
    const bool loaded = extension == ".glb"
        ? loader.LoadBinaryFromMemory(
            &gltf,
            &loaderError,
            &warning,
            sourceBytes.data(),
            static_cast<unsigned int>(sourceBytes.size()),
            baseDirectory)
        : loader.LoadASCIIFromString(
            &gltf,
            &loaderError,
            &warning,
            reinterpret_cast<const char*>(sourceBytes.data()),
            static_cast<unsigned int>(sourceBytes.size()),
            baseDirectory);
    if (!warning.empty()) {
        core::logWarning(core::LogCategory::Assets, warning);
    }
    if (!loaded) {
        setError(errorMessage, loaderError.empty() ? "TinyGLTF failed to load the model" : loaderError);
        return {};
    }
    reportProgress(progress, 30, "Importing textures");
    auto model = std::make_shared<ModelAsset>();
    model->id = makeId(sourceBytes, AssetType::Model);
    model->name = sourcePath.stem().string().empty() ? "Imported Model" : sourcePath.stem().string();
    std::vector<int> textureMap(gltf.textures.size(), -1);
    for (std::size_t textureIndex = 0; textureIndex < gltf.textures.size(); ++textureIndex) {
        const auto sourceIndex = gltf.textures[textureIndex].source;
        if (sourceIndex < 0 || static_cast<std::size_t>(sourceIndex) >= gltf.images.size()) {
            continue;
        }
        TextureAsset texture;
        if (!detail::importGltfTexture(
                gltf,
                gltf.images[static_cast<std::size_t>(sourceIndex)],
                gltf.textures[textureIndex].name,
                sourcePath.parent_path(),
                texture,
                errorMessage)) {
            return {};
        }
        texture.sampler = textureSampler(gltf, gltf.textures[textureIndex]);
        textureMap[textureIndex] = static_cast<int>(model->textures.size());
        model->textures.push_back(std::move(texture));
        const auto textureProgress = 30 + static_cast<int>((textureIndex + 1U) * 20U / std::max<std::size_t>(gltf.textures.size(), 1U));
        reportProgress(progress, textureProgress, "Importing textures");
    }
    reportProgress(progress, 55, "Processing materials");
    if (gltf.materials.empty()) {
        model->materials.push_back(defaultMaterial());
    } else {
        model->materials.reserve(gltf.materials.size());
        for (const auto& sourceMaterial : gltf.materials) {
            MaterialAsset material;
            material.name = sourceMaterial.name.empty() ? "Material" : sourceMaterial.name;
            material.metallicFactor = static_cast<float>(sourceMaterial.pbrMetallicRoughness.metallicFactor);
            material.roughnessFactor = static_cast<float>(sourceMaterial.pbrMetallicRoughness.roughnessFactor);
            material.alphaMode = materialAlphaMode(sourceMaterial.alphaMode);
            material.alphaCutoff = std::clamp(static_cast<float>(sourceMaterial.alphaCutoff), 0.0F, 1.0F);
            material.doubleSided = sourceMaterial.doubleSided;
            const auto& factor = sourceMaterial.pbrMetallicRoughness.baseColorFactor;
            if (factor.size() == 4U) {
                material.baseColor = {
                    static_cast<float>(factor[0]),
                    static_cast<float>(factor[1]),
                    static_cast<float>(factor[2]),
                    static_cast<float>(factor[3]),
                };
            }
            if (sourceMaterial.emissiveFactor.size() == 3U) {
                material.emissiveColor = {
                    static_cast<float>(sourceMaterial.emissiveFactor[0]),
                    static_cast<float>(sourceMaterial.emissiveFactor[1]),
                    static_cast<float>(sourceMaterial.emissiveFactor[2]),
                };
            }
            const auto mapTexture = [&textureMap](int textureIndex, std::optional<std::size_t>& output) {
                if (textureIndex >= 0
                    && static_cast<std::size_t>(textureIndex) < textureMap.size()
                    && textureMap[static_cast<std::size_t>(textureIndex)] >= 0) {
                    output = static_cast<std::size_t>(textureMap[static_cast<std::size_t>(textureIndex)]);
                }
            };
            mapTexture(sourceMaterial.pbrMetallicRoughness.baseColorTexture.index, material.baseColorTexture);
            mapTexture(sourceMaterial.normalTexture.index, material.normalTexture);
            mapTexture(
                sourceMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index,
                material.metallicRoughnessTexture);
            mapTexture(sourceMaterial.occlusionTexture.index, material.occlusionTexture);
            mapTexture(sourceMaterial.emissiveTexture.index, material.emissiveTexture);
            material.normalScale = static_cast<float>(sourceMaterial.normalTexture.scale);
            material.occlusionStrength = std::clamp(static_cast<float>(sourceMaterial.occlusionTexture.strength), 0.0F, 1.0F);
            model->materials.push_back(std::move(material));
        }
    }
    if (!gltf.scenes.empty()) {
        reportProgress(progress, 62, "Processing scene nodes");
        const auto sceneIndex = gltf.defaultScene >= 0 ? gltf.defaultScene : 0;
        if (static_cast<std::size_t>(sceneIndex) >= gltf.scenes.size()) {
            setError(errorMessage, "glTF default scene index is invalid");
            return {};
        }
        std::vector<ImportedPrimitiveKey> primitiveMap;
        std::vector<ImportedPrimitiveSignature> primitiveSignatures;
        for (const auto node : gltf.scenes[static_cast<std::size_t>(sceneIndex)].nodes) {
            if (!importNodePrimitives(
                    gltf,
                    node,
                    identityGltfMatrix(),
                    *model,
                    primitiveMap,
                    primitiveSignatures,
                    model->materials.size(),
                    errorMessage)
                || !detail::importGltfSceneObjects(
                    gltf,
                    node,
                    identityGltfMatrix(),
                    model->lights,
                    model->cameras,
                    errorMessage)) {
                return {};
            }
        }
    } else {
        reportProgress(progress, 62, "Processing meshes");
        std::vector<ImportedPrimitiveKey> primitiveMap;
        std::vector<ImportedPrimitiveSignature> primitiveSignatures;
        for (std::size_t meshIndex = 0; meshIndex < gltf.meshes.size(); ++meshIndex) {
            const auto& mesh = gltf.meshes[meshIndex];
            for (std::size_t primitiveIndex = 0; primitiveIndex < mesh.primitives.size(); ++primitiveIndex) {
                std::uint32_t outputIndex = 0;
                if (!ensureImportedPrimitive(
                        gltf,
                        static_cast<int>(meshIndex),
                        static_cast<int>(primitiveIndex),
                        model->materials.size(),
                        *model,
                        primitiveMap,
                        primitiveSignatures,
                        errorMessage,
                        outputIndex)) {
                    return {};
                }
                MeshPrimitiveInstance instance;
                instance.primitiveIndex = outputIndex;
                instance.bounds = model->primitives[outputIndex].bounds;
                model->primitiveInstances.push_back(instance);
            }
        }
    }
    if (model->primitives.empty()) {
        setError(errorMessage, "glTF model contains no supported mesh primitives");
        return {};
    }
    reportProgress(progress, 82, "Batching and optimizing meshes");
    detail::batchModelPrimitives(*model);
    detail::splitLargePrimitivesIntoSpatialChunks(*model);
    AssetRecord record;
    record.id = model->id;
    record.type = AssetType::Model;
    record.displayName = model->name;
    record.sourceName = sourcePath.filename().string();
    record.cacheFile = std::to_string(record.id.value()) + ".asset.json";
    record.textureCount = model->textures.size();
    for (const auto& primitive : model->primitives) {
        record.vertexCount += primitive.vertices.size();
        record.indexCount += primitive.indices.size();
    }
    return {std::move(model), std::move(record)};
}
} // namespace
AssetImportResult AssetManager::importModel(
    const std::filesystem::path& sourcePath,
    const AssetImportProgressCallback& progress)
{
    std::string error;
    reportProgress(progress, 5, "Reading model source");
    const auto bytes = readBytes(sourcePath, &error);
    if (bytes.empty()) {
        core::logError(core::LogCategory::Assets, error);
        return {false, {}, std::move(error)};
    }
    reportProgress(progress, 12, "Source file loaded");
    auto imported = importGltfModel(sourcePath, bytes, progress, &error);
    if (imported.asset == nullptr) {
        core::logError(core::LogCategory::Assets, error);
        return {false, {}, std::move(error)};
    }
    reportProgress(progress, 92, "Writing asset cache");
    if (!writeCacheRecord(imported.record, &error)) {
        core::logError(core::LogCategory::Assets, error);
        return {false, {}, std::move(error)};
    }
    {
        std::scoped_lock lock(mutex_);
        auto existing = std::find_if(models_.begin(), models_.end(), [&imported](const auto& model) {
            return model != nullptr && model->id == imported.asset->id;
        });
        if (existing == models_.end()) {
            models_.push_back(imported.asset);
        } else {
            *existing = imported.asset;
        }
    }
    storeRecord(imported.record);
    reportProgress(progress, 100, "Model imported");
    core::logInfo(core::LogCategory::Assets, "Model asset imported and cached");
    return {true, imported.record, {}};
}
AssetImportResult AssetManager::importTexture(
    const std::filesystem::path& sourcePath,
    const AssetImportProgressCallback& progress)
{
    std::string error;
    reportProgress(progress, 5, "Reading texture source");
    const auto bytes = readBytes(sourcePath, &error);
    if (bytes.empty()) {
        core::logError(core::LogCategory::Assets, error);
        return {false, {}, std::move(error)};
    }
    reportProgress(progress, 30, "Decoding texture");
    auto textureAsset = std::make_shared<TextureAsset>(
        detail::isKtxTextureExtension(sourcePath)
            ? detail::importKtxTexture(sourcePath, bytes, &error)
            : detail::importStbTexture(sourcePath, bytes, &error));
    if (!textureAsset->id.isValid()) {
        core::logError(core::LogCategory::Assets, error);
        return {false, {}, std::move(error)};
    }
    AssetRecord record;
    record.id = textureAsset->id;
    record.type = AssetType::Texture2D;
    record.displayName = textureAsset->name;
    record.sourceName = sourcePath.filename().string();
    record.cacheFile = std::to_string(record.id.value()) + ".asset.json";
    reportProgress(progress, 88, "Writing asset cache");
    if (!writeCacheRecord(record, &error)) {
        core::logError(core::LogCategory::Assets, error);
        return {false, {}, std::move(error)};
    }
    {
        std::scoped_lock lock(mutex_);
        auto existing = std::find_if(textures_.begin(), textures_.end(), [&textureAsset](const auto& texture) {
            return texture != nullptr && texture->id == textureAsset->id;
        });
        if (existing == textures_.end()) {
            textures_.push_back(textureAsset);
        } else {
            *existing = textureAsset;
        }
    }
    storeRecord(record);
    reportProgress(progress, 100, "Texture imported");
    core::logInfo(core::LogCategory::Assets, "Texture asset imported and cached");
    return {true, std::move(record), {}};
}
} // namespace projectunity::assets
