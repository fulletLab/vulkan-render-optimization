#include <projectunity/assets/AssetManager.hpp>

#include "AssetImportUtils.hpp"
#include "GltfAttributeReader.hpp"
#include "GltfNodeTransforms.hpp"
#include "MeshBounds.hpp"
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

namespace projectunity::assets {
namespace {

constexpr float kTangentEpsilon = 0.000001F;

using detail::makeId;
using detail::makeTextureId;
using detail::readBytes;
using detail::readColorAttribute;
using detail::readFloatAttribute;
using detail::readIndices;
using detail::setError;
using detail::GltfMatrix4;
using detail::applyGltfTransform;
using detail::gltfNodeMatrix;
using detail::identityGltfMatrix;
using detail::multiplyGltfMatrices;

struct ImportedModel {
    std::shared_ptr<ModelAsset> asset;
    AssetRecord record;
};

[[nodiscard]] std::string lowerExtension(std::filesystem::path path)
{
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension;
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

    const auto appendLod = [&primitive](std::size_t targetTriangleCount) {
        const auto targetCount = targetTriangleCount * 3U;
        if (targetCount < 3U || targetCount >= primitive.indices.size()) {
            return;
        }
        const auto duplicate = std::any_of(primitive.lods.begin(), primitive.lods.end(), [targetCount](const MeshLod& lod) {
            return lod.indices.size() == targetCount;
        });
        if (duplicate) {
            return;
        }

        MeshLod lod;
        lod.indices.resize(primitive.indices.size());
        const auto result = meshopt_simplify(
            lod.indices.data(),
            primitive.indices.data(),
            primitive.indices.size(),
            &primitive.vertices.front().position.x,
            primitive.vertices.size(),
            sizeof(MeshVertex),
            targetCount,
            0.01F);
        lod.indices.resize(result);
        if (lod.indices.size() >= 3U && lod.indices.size() < primitive.indices.size()) {
            primitive.lods.push_back(std::move(lod));
        }
    };

    const auto sourceTriangles = primitive.indices.size() / 3U;
    appendLod(sourceTriangles / 2U);
    appendLod(sourceTriangles / 4U);
    appendLod(sourceTriangles / 8U);
}

[[nodiscard]] bool convertTexture(
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

[[nodiscard]] MaterialAsset defaultMaterial()
{
    return {};
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

[[nodiscard]] bool importNodePrimitives(
    const tinygltf::Model& gltf,
    int nodeIndex,
    GltfMatrix4 parentTransform,
    std::vector<MeshPrimitive>& output,
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
        for (const auto& primitive : gltf.meshes[static_cast<std::size_t>(node.mesh)].primitives) {
            MeshPrimitive imported;
            if (!importPrimitive(gltf, primitive, materialCount, imported, errorMessage)) {
                return false;
            }
            applyGltfTransform(imported, worldTransform);
            output.push_back(std::move(imported));
        }
    }
    for (const auto child : node.children) {
        if (!importNodePrimitives(gltf, child, worldTransform, output, materialCount, errorMessage, depth + 1)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] ImportedModel importGltfModel(
    const std::filesystem::path& sourcePath,
    std::span<const std::uint8_t> sourceBytes,
    std::string* errorMessage)
{
    tinygltf::TinyGLTF loader;
    tinygltf::Model gltf;
    std::string warning;
    std::string loaderError;
    const auto extension = lowerExtension(sourcePath);
    if (sourceBytes.size() > std::numeric_limits<unsigned int>::max()) {
        setError(errorMessage, "Asset source file is too large for TinyGLTF memory import");
        return {};
    }

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
        if (!convertTexture(gltf.images[static_cast<std::size_t>(sourceIndex)], gltf.textures[textureIndex].name, texture, errorMessage)) {
            return {};
        }
        textureMap[textureIndex] = static_cast<int>(model->textures.size());
        model->textures.push_back(std::move(texture));
    }

    if (gltf.materials.empty()) {
        model->materials.push_back(defaultMaterial());
    } else {
        model->materials.reserve(gltf.materials.size());
        for (const auto& sourceMaterial : gltf.materials) {
            MaterialAsset material;
            material.name = sourceMaterial.name.empty() ? "Material" : sourceMaterial.name;
            material.metallicFactor = static_cast<float>(sourceMaterial.pbrMetallicRoughness.metallicFactor);
            material.roughnessFactor = static_cast<float>(sourceMaterial.pbrMetallicRoughness.roughnessFactor);
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

            const auto textureIndex = sourceMaterial.pbrMetallicRoughness.baseColorTexture.index;
            if (textureIndex >= 0
                && static_cast<std::size_t>(textureIndex) < textureMap.size()
                && textureMap[static_cast<std::size_t>(textureIndex)] >= 0) {
                material.baseColorTexture = static_cast<std::size_t>(textureMap[static_cast<std::size_t>(textureIndex)]);
            }
            model->materials.push_back(std::move(material));
        }
    }

    if (!gltf.scenes.empty()) {
        const auto sceneIndex = gltf.defaultScene >= 0 ? gltf.defaultScene : 0;
        if (static_cast<std::size_t>(sceneIndex) >= gltf.scenes.size()) {
            setError(errorMessage, "glTF default scene index is invalid");
            return {};
        }
        for (const auto node : gltf.scenes[static_cast<std::size_t>(sceneIndex)].nodes) {
            if (!importNodePrimitives(gltf, node, identityGltfMatrix(), model->primitives, model->materials.size(), errorMessage)) {
                return {};
            }
        }
    } else {
        for (const auto& mesh : gltf.meshes) {
            for (const auto& primitive : mesh.primitives) {
                MeshPrimitive imported;
                if (!importPrimitive(gltf, primitive, model->materials.size(), imported, errorMessage)) {
                    return {};
                }
                model->primitives.push_back(std::move(imported));
            }
        }
    }

    if (model->primitives.empty()) {
        setError(errorMessage, "glTF model contains no supported mesh primitives");
        return {};
    }

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

AssetImportResult AssetManager::importModel(const std::filesystem::path& sourcePath)
{
    std::string error;
    const auto bytes = readBytes(sourcePath, &error);
    if (bytes.empty()) {
        core::logError(core::LogCategory::Assets, error);
        return {false, {}, std::move(error)};
    }

    auto imported = importGltfModel(sourcePath, bytes, &error);
    if (imported.asset == nullptr) {
        core::logError(core::LogCategory::Assets, error);
        return {false, {}, std::move(error)};
    }

    if (!writeCacheRecord(imported.record, &error)) {
        core::logError(core::LogCategory::Assets, error);
        return {false, {}, std::move(error)};
    }

    {
        std::scoped_lock lock(mutex_);
        models_.push_back(imported.asset);
    }
    storeRecord(imported.record);
    core::logInfo(core::LogCategory::Assets, "Model asset imported and cached");
    return {true, imported.record, {}};
}

AssetImportResult AssetManager::importTexture(const std::filesystem::path& sourcePath)
{
    std::string error;
    const auto bytes = readBytes(sourcePath, &error);
    if (bytes.empty()) {
        core::logError(core::LogCategory::Assets, error);
        return {false, {}, std::move(error)};
    }

    auto textureAsset = std::make_shared<TextureAsset>(detail::importStbTexture(sourcePath, bytes, &error));
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
    if (!writeCacheRecord(record, &error)) {
        core::logError(core::LogCategory::Assets, error);
        return {false, {}, std::move(error)};
    }

    {
        std::scoped_lock lock(mutex_);
        textures_.push_back(textureAsset);
    }
    storeRecord(record);
    core::logInfo(core::LogCategory::Assets, "Texture asset imported and cached");
    return {true, std::move(record), {}};
}

} // namespace projectunity::assets
