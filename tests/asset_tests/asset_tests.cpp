#include <projectunity/assets/AssetManager.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

int fail(const char* message)
{
    std::cerr << message << '\n';
    return EXIT_FAILURE;
}

void appendU16(std::vector<std::uint8_t>& bytes, std::uint16_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
}

void appendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
}

void appendFloat(std::vector<std::uint8_t>& bytes, float value)
{
    const auto* valueBytes = reinterpret_cast<const std::uint8_t*>(&value);
    bytes.insert(bytes.end(), valueBytes, valueBytes + sizeof(value));
}

[[nodiscard]] std::vector<std::uint8_t> makeTriangleBuffer()
{
    std::vector<std::uint8_t> bytes;
    const std::array<float, 27> vec3Values {{
        -1.0F, -1.0F, 0.0F,
        1.0F, -1.0F, 0.0F,
        0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 1.0F,
        0.0F, 0.0F, 1.0F,
        0.0F, 0.0F, 1.0F,
        0.0F, 1.0F, 0.0F,
        1.0F, 1.0F, 0.0F,
        0.5F, 0.0F, 0.0F,
    }};
    for (std::size_t index = 0; index < 18U; ++index) {
        appendFloat(bytes, vec3Values[index]);
    }

    const std::array<float, 6> texCoords {{0.0F, 1.0F, 1.0F, 1.0F, 0.5F, 0.0F}};
    for (const auto value : texCoords) {
        appendFloat(bytes, value);
    }

    const std::array<float, 12> colors {{
        1.0F, 0.25F, 0.0F, 1.0F,
        0.0F, 1.0F, 0.25F, 1.0F,
        0.25F, 0.0F, 1.0F, 1.0F,
    }};
    for (const auto value : colors) {
        appendFloat(bytes, value);
    }

    appendU16(bytes, 0);
    appendU16(bytes, 1);
    appendU16(bytes, 2);
    return bytes;
}

[[nodiscard]] std::vector<std::uint8_t> makeKtx1Astc4x4Texture()
{
    std::vector<std::uint8_t> bytes {
        0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A,
    };
    appendU32(bytes, 0x04030201U);
    appendU32(bytes, 0U);
    appendU32(bytes, 1U);
    appendU32(bytes, 0U);
    appendU32(bytes, 0x93B0U);
    appendU32(bytes, 0x1908U);
    appendU32(bytes, 4U);
    appendU32(bytes, 4U);
    appendU32(bytes, 0U);
    appendU32(bytes, 0U);
    appendU32(bytes, 1U);
    appendU32(bytes, 1U);
    appendU32(bytes, 0U);
    appendU32(bytes, 16U);
    for (std::uint8_t value = 0; value < 16U; ++value) {
        bytes.push_back(value);
    }
    return bytes;
}

[[nodiscard]] bool writeBinary(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return file.good();
}

[[nodiscard]] bool writeTriangleGlb(const std::filesystem::path& path)
{
    auto binary = makeTriangleBuffer();
    const std::string json = R"({
        "asset":{"version":"2.0"},
        "buffers":[{"byteLength":150}],
        "bufferViews":[
            {"buffer":0,"byteOffset":0,"byteLength":36},
            {"buffer":0,"byteOffset":36,"byteLength":36},
            {"buffer":0,"byteOffset":72,"byteLength":24},
            {"buffer":0,"byteOffset":96,"byteLength":48},
            {"buffer":0,"byteOffset":144,"byteLength":6}
        ],
        "accessors":[
            {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[-1,-1,0],"max":[1,1,0]},
            {"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
            {"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},
            {"bufferView":3,"componentType":5126,"count":3,"type":"VEC4"},
            {"bufferView":4,"componentType":5123,"count":3,"type":"SCALAR"}
        ],
        "images":[{
            "uri":"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg=="
        }],
        "samplers":[{"magFilter":9728,"minFilter":9986,"wrapS":33071,"wrapT":33648}],
        "textures":[{"sampler":0,"source":0}],
        "materials":[{
            "pbrMetallicRoughness":{
                "baseColorTexture":{"index":0},
                "metallicRoughnessTexture":{"index":0},
                "baseColorFactor":[0.25,0.5,0.75,1.0],
                "metallicFactor":0.35,
                "roughnessFactor":0.65
            },
            "normalTexture":{"index":0,"scale":0.75},
            "occlusionTexture":{"index":0,"strength":0.4},
            "emissiveTexture":{"index":0},
            "alphaMode":"MASK",
            "alphaCutoff":0.33,
            "doubleSided":true,
            "emissiveFactor":[0.05,0.1,0.15]
        }],
        "cameras":[{
            "name":"Fixture Camera",
            "type":"perspective",
            "perspective":{"yfov":0.9,"znear":0.1,"zfar":250.0,"aspectRatio":1.7777778}
        }],
        "extensionsUsed":["KHR_lights_punctual"],
        "extensions":{"KHR_lights_punctual":{"lights":[{
            "name":"Fixture Key",
            "type":"spot",
            "color":[0.7,0.8,1.0],
            "intensity":8.0,
            "range":14.0,
            "spot":{"innerConeAngle":0.2,"outerConeAngle":0.6}
        }]}},
        "meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2,"COLOR_0":3},"indices":4,"material":0}]}],
        "nodes":[
            {"translation":[6,0,0],"mesh":0},
            {"translation":[2,4,5],"extensions":{"KHR_lights_punctual":{"light":0}}},
            {"translation":[0,1,9],"camera":0}
        ],
        "scenes":[{"nodes":[0,1,2]}],
        "scene":0
    })";

    std::vector<std::uint8_t> jsonBytes(json.begin(), json.end());
    while (jsonBytes.size() % 4U != 0) {
        jsonBytes.push_back(static_cast<std::uint8_t>(' '));
    }
    while (binary.size() % 4U != 0) {
        binary.push_back(0);
    }

    std::vector<std::uint8_t> glb;
    appendU32(glb, 0x46546c67U);
    appendU32(glb, 2U);
    appendU32(glb, static_cast<std::uint32_t>(12U + 8U + jsonBytes.size() + 8U + binary.size()));
    appendU32(glb, static_cast<std::uint32_t>(jsonBytes.size()));
    appendU32(glb, 0x4e4f534aU);
    glb.insert(glb.end(), jsonBytes.begin(), jsonBytes.end());
    appendU32(glb, static_cast<std::uint32_t>(binary.size()));
    appendU32(glb, 0x004e4942U);
    glb.insert(glb.end(), binary.begin(), binary.end());
    return writeBinary(path, glb);
}

[[nodiscard]] std::vector<std::uint8_t> makeSpatialBuffer()
{
    std::vector<std::uint8_t> bytes;
    const std::array<float, 18> vec3Values {{
        -0.25F, 0.0F, 0.0F,
        0.75F, 0.0F, 0.0F,
        -0.25F, 0.5F, 0.0F,
        0.0F, 0.0F, 1.0F,
        0.0F, 0.0F, 1.0F,
        0.0F, 0.0F, 1.0F,
    }};
    for (const auto value : vec3Values) {
        appendFloat(bytes, value);
    }
    const std::array<float, 6> texCoords {{0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F}};
    for (const auto value : texCoords) {
        appendFloat(bytes, value);
    }
    appendU16(bytes, 0);
    appendU16(bytes, 1);
    appendU16(bytes, 2);
    return bytes;
}

[[nodiscard]] bool writeSpatialGlb(const std::filesystem::path& path)
{
    auto binary = makeSpatialBuffer();
    const std::string json = R"({
        "asset":{"version":"2.0"},
        "buffers":[{"byteLength":102}],
        "bufferViews":[
            {"buffer":0,"byteOffset":0,"byteLength":36},
            {"buffer":0,"byteOffset":36,"byteLength":36},
            {"buffer":0,"byteOffset":72,"byteLength":24},
            {"buffer":0,"byteOffset":96,"byteLength":6}
        ],
        "accessors":[
            {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[-0.25,0,0],"max":[0.75,0.5,0]},
            {"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
            {"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},
            {"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}
        ],
        "meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3}]}],
        "nodes":[
            {"name":"left","translation":[-3,0,0],"mesh":0},
            {"name":"right","translation":[5,0,0],"mesh":0},
            {"name":"front","translation":[0,0,-7],"mesh":0},
            {"name":"back","translation":[0,0,9],"mesh":0},
            {"name":"matrix","matrix":[1,0,0,0,0,1,0,0,0,0,1,0,11,2,13,1],"mesh":0},
            {"name":"rotated","translation":[15,0,0],"rotation":[0,0.70710678,0,0.70710678],"mesh":0},
            {"name":"mirrored","translation":[0,0,3],"scale":[-1,1,1],"mesh":0}
        ],
        "scenes":[{"nodes":[0,1,2,3,4,5,6]}],
        "scene":0
    })";

    std::vector<std::uint8_t> jsonBytes(json.begin(), json.end());
    while (jsonBytes.size() % 4U != 0) {
        jsonBytes.push_back(static_cast<std::uint8_t>(' '));
    }
    while (binary.size() % 4U != 0) {
        binary.push_back(0);
    }

    std::vector<std::uint8_t> glb;
    appendU32(glb, 0x46546c67U);
    appendU32(glb, 2U);
    appendU32(glb, static_cast<std::uint32_t>(12U + 8U + jsonBytes.size() + 8U + binary.size()));
    appendU32(glb, static_cast<std::uint32_t>(jsonBytes.size()));
    appendU32(glb, 0x4e4f534aU);
    glb.insert(glb.end(), jsonBytes.begin(), jsonBytes.end());
    appendU32(glb, static_cast<std::uint32_t>(binary.size()));
    appendU32(glb, 0x004e4942U);
    glb.insert(glb.end(), binary.begin(), binary.end());
    return writeBinary(path, glb);
}

[[nodiscard]] bool near(float actual, float expected, float epsilon = 0.03F)
{
    return std::fabs(actual - expected) <= epsilon;
}

[[nodiscard]] bool hasTexCoord(const projectunity::assets::MeshPrimitive& primitive, float u, float v)
{
    return std::any_of(primitive.vertices.begin(), primitive.vertices.end(), [u, v](const auto& vertex) {
        return near(vertex.texCoord[0], u, 0.001F) && near(vertex.texCoord[1], v, 0.001F);
    });
}

[[nodiscard]] float firstFaceNormalZ(const projectunity::assets::MeshPrimitive& primitive)
{
    const auto& a = primitive.vertices[primitive.indices[0]].position;
    const auto& b = primitive.vertices[primitive.indices[1]].position;
    const auto& c = primitive.vertices[primitive.indices[2]].position;
    return projectunity::math::cross(b - a, c - a).z;
}

[[nodiscard]] bool writeTinyPng(const std::filesystem::path& path)
{
    const std::vector<std::uint8_t> bytes {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
        0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53, 0xDE, 0x00, 0x00, 0x00,
        0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0xDA, 0x63, 0xFC, 0xCF, 0xC0, 0x50,
        0x0F, 0x00, 0x04, 0x85, 0x01, 0x80, 0x84, 0xA9, 0x8C, 0x21, 0x00, 0x00,
        0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
    };
    return writeBinary(path, bytes);
}

} // namespace

int main()
{
    using namespace projectunity::assets;

    const auto root = std::filesystem::temp_directory_path() / "projectunity_asset_tests";
    std::error_code errorCode;
    std::filesystem::remove_all(root, errorCode);
    std::filesystem::create_directories(root, errorCode);
    if (errorCode) {
        return fail("unable to create asset test directory");
    }

    const auto glbPath = root / "triangle.glb";
    const auto spatialGlbPath = root / "spatial.glb";
    const auto pngPath = root / "tiny.png";
    const auto ktxPath = root / "astc.ktx";
    if (!writeTriangleGlb(glbPath)
        || !writeSpatialGlb(spatialGlbPath)
        || !writeTinyPng(pngPath)
        || !writeBinary(ktxPath, makeKtx1Astc4x4Texture())) {
        return fail("unable to write generated asset fixtures");
    }

    AssetManager manager(root / "Cache" / "Assets");
    const auto modelResult = manager.importModel(glbPath);
    if (!modelResult.success) {
        std::cerr << modelResult.error << '\n';
        return fail("generated GLB import failed");
    }
    const auto model = manager.model(modelResult.record.id);
    if (model == nullptr
        || model->primitives.size() != 1
        || model->primitiveInstances.size() != 1
        || model->primitives.front().vertices.size() != 3) {
        return fail("imported GLB model data is incomplete");
    }
    const auto firstModelInstance = model.get();
    const auto reimportResult = manager.importModel(glbPath);
    const auto reimportedModel = manager.model(modelResult.record.id);
    if (!reimportResult.success || reimportedModel == nullptr || reimportedModel.get() == firstModelInstance) {
        return fail("reimporting a GLB did not refresh the active in-memory model instance");
    }
    if (model->primitives.front().vertices.front().tangent.lengthSquared() <= 0.1F) {
        return fail("imported GLB tangent generation produced invalid data");
    }
    const auto colorPreserved = std::any_of(
        model->primitives.front().vertices.begin(),
        model->primitives.front().vertices.end(),
        [](const MeshVertex& vertex) {
            return vertex.color[0] > 0.99F
                && vertex.color[1] > 0.24F
                && vertex.color[2] < 0.01F
                && vertex.color[3] > 0.99F;
        });
    if (!colorPreserved) {
        return fail("imported GLB vertex colors were not preserved");
    }
    const auto& instance = model->primitiveInstances.front();
    if (instance.primitiveIndex != 0 || instance.bounds.minimum.x < 5.0F || instance.bounds.maximum.x > 7.0F) {
        return fail("imported GLB node transform was not preserved as a mesh instance");
    }
    const auto& bounds = model->primitives.front().bounds;
    if (bounds.radius <= 0.1F || bounds.minimum.x < -1.1F || bounds.maximum.x > 1.1F) {
        return fail("imported GLB source primitive bounds were not preserved for instancing");
    }
    const auto& material = model->materials.front();
    if (material.metallicFactor < 0.34F
        || material.metallicFactor > 0.36F
        || material.roughnessFactor < 0.64F
        || material.roughnessFactor > 0.66F
        || material.emissiveColor[2] < 0.14F
        || material.baseColor[2] < 0.74F
        || material.normalScale < 0.74F
        || material.alphaMode != MaterialAlphaMode::Mask
        || material.alphaCutoff < 0.32F
        || material.alphaCutoff > 0.34F
        || !material.doubleSided
        || !material.baseColorTexture.has_value()
        || !material.normalTexture.has_value()
        || !material.metallicRoughnessTexture.has_value()
        || !material.occlusionTexture.has_value()
        || !material.emissiveTexture.has_value()
        || material.occlusionStrength < 0.39F
        || material.occlusionStrength > 0.41F) {
        return fail("imported GLB PBR material factors were not preserved");
    }
    const auto& sampler = model->textures.front().sampler;
    if (sampler.magnificationFilter != TextureFilterMode::Nearest
        || sampler.minificationFilter != TextureFilterMode::Nearest
        || sampler.mipmapFilter != TextureFilterMode::Linear
        || sampler.wrapU != TextureWrapMode::ClampToEdge
        || sampler.wrapV != TextureWrapMode::MirroredRepeat
        || !sampler.useMipmaps) {
        return fail("imported GLB texture sampler state was not preserved");
    }
    if (model->lights.size() != 1
        || model->lights.front().type != ImportedLightType::Spot
        || model->lights.front().position.y < 3.99F
        || model->lights.front().range < 13.99F
        || model->lights.front().outerConeAngle < 0.59F
        || model->lights.front().color[2] < 0.99F) {
        return fail("imported glTF punctual light state was not preserved");
    }
    if (model->cameras.size() != 1
        || model->cameras.front().projection != ImportedCameraProjection::Perspective
        || !near(model->cameras.front().position.z, -9.0F)
        || model->cameras.front().direction.z < 0.99F
        || model->cameras.front().right.x < 0.99F
        || model->cameras.front().verticalFovRadians < 0.89F
        || model->cameras.front().nearPlane < 0.09F
        || model->cameras.front().farPlane < 249.0F) {
        return fail("imported glTF camera state was not preserved");
    }

    const auto textureResult = manager.importTexture(pngPath);
    if (!textureResult.success) {
        std::cerr << textureResult.error << '\n';
        return fail("PNG texture import failed");
    }
    const auto texture = manager.texture(textureResult.record.id);
    if (texture == nullptr || texture->width != 1 || texture->height != 1 || texture->rgba8.size() != 4) {
        return fail("imported PNG texture data is incomplete");
    }

    const auto cachePath = manager.cacheRoot() / modelResult.record.cacheFile;
    if (!std::filesystem::exists(cachePath) || manager.records().size() != 2) {
        return fail("asset cache records were not written");
    }

    std::vector<int> progressValues;
    const auto ktxResult = manager.importTexture(ktxPath, [&progressValues](const AssetImportProgress& progress) {
        progressValues.push_back(progress.percent);
    });
    const auto ktxTexture = manager.texture(ktxResult.record.id);
    if (!ktxResult.success
        || ktxTexture == nullptr
        || ktxTexture->gpuFormat != TextureGpuFormat::Astc4x4Unorm
        || ktxTexture->gpuMipLevels.size() != 1
        || !ktxTexture->rgba8.empty()
        || ktxTexture->gpuMipLevels.front().bytes.size() != 16U) {
        std::cerr << ktxResult.error << '\n';
        return fail("KTX1 ASTC texture import did not preserve GPU mip data");
    }
    if (progressValues.empty()
        || progressValues.front() < 1
        || progressValues.back() != 100
        || !std::is_sorted(progressValues.begin(), progressValues.end())) {
        return fail("asset import progress did not report monotonic 1-100 updates");
    }

    const auto spatialResult = manager.importModel(spatialGlbPath);
    const auto spatial = manager.model(spatialResult.record.id);
    if (!spatialResult.success
        || spatial == nullptr
        || spatial->primitives.size() != 1
        || spatial->primitiveInstances.size() != 7) {
        return fail("spatial glTF fixture import failed");
    }
    const auto& left = spatial->primitiveInstances[0].bounds.center;
    const auto& right = spatial->primitiveInstances[1].bounds.center;
    const auto& front = spatial->primitiveInstances[2].bounds.center;
    const auto& back = spatial->primitiveInstances[3].bounds.center;
    const auto& matrixNode = spatial->primitiveInstances[4].bounds.center;
    const auto& rotated = spatial->primitiveInstances[5].bounds;
    const auto& mirrored = spatial->primitiveInstances[6];
    if (!(left.x < 0.0F && right.x > 0.0F && right.x > left.x)) {
        return fail("glTF left/right node positions were mirrored");
    }
    if (!(front.z > 0.0F && back.z < 0.0F && front.z > back.z)) {
        return fail("glTF front/back node positions were not converted to engine +Z forward");
    }
    if (!near(matrixNode.x, 11.25F) || !near(matrixNode.y, 2.25F) || !near(matrixNode.z, -13.0F)) {
        return fail("glTF node.matrix column-major translation was not preserved");
    }
    if (!near(rotated.center.x, 15.0F) || !near(rotated.center.z, 0.25F)) {
        return fail("glTF quaternion rotation was not preserved");
    }
    if (!near(mirrored.bounds.center.x, -0.25F) || !near(mirrored.bounds.center.z, -3.0F)) {
        return fail("glTF negative-scale node position/bounds were not preserved");
    }
    if (!mirrored.flipsWinding
        || firstFaceNormalZ(spatial->primitives.front()) >= 0.0F
        || spatial->primitives.front().vertices.front().normal.z > -0.99F) {
        return fail("glTF negative determinant instance or base conversion state was not preserved");
    }
    if (!hasTexCoord(spatial->primitives[0], 0.0F, 0.0F)
        || !hasTexCoord(spatial->primitives[0], 1.0F, 0.0F)
        || !hasTexCoord(spatial->primitives[0], 0.0F, 1.0F)) {
        return fail("glTF texture coordinates were flipped or lost");
    }

    const auto examplePath = std::filesystem::path(PROJECTUNITY_SOURCE_DIR)
        / "examples" / "basic_assets" / "TexturedTriangle.gltf";
    const auto exampleResult = manager.importModel(examplePath);
    const auto example = manager.model(exampleResult.record.id);
    if (!exampleResult.success || example == nullptr || example->textures.empty()) {
        std::cerr << exampleResult.error << '\n';
        return fail("textured glTF example import failed");
    }
    const auto nodePerformancePath = std::filesystem::path(PROJECTUNITY_SOURCE_DIR)
        / "Project" / "Assets" / "VisualVerification" / "NodePerformanceTest.glb";
    if (std::filesystem::exists(nodePerformancePath)) {
        const auto nodePerfResult = manager.importModel(nodePerformancePath);
        const auto nodePerf = manager.model(nodePerfResult.record.id);
        if (!nodePerfResult.success
            || nodePerf == nullptr
            || nodePerf->primitives.size() > 256U
            || nodePerf->primitiveInstances.size() > 256U) {
            std::cerr << nodePerfResult.error << '\n';
            if (nodePerf != nullptr) {
                std::cerr << "primitives=" << nodePerf->primitives.size()
                          << " instances=" << nodePerf->primitiveInstances.size()
                          << " materials=" << nodePerf->materials.size()
                          << " textures=" << nodePerf->textures.size()
                          << " vertices=" << nodePerfResult.record.vertexCount << '\n';
                if (nodePerf->materials.size() >= 2U) {
                    const auto& a = nodePerf->materials[0];
                    const auto& b = nodePerf->materials[1];
                    std::cerr << "m0=" << a.baseColor[0] << ',' << a.baseColor[1] << ',' << a.baseColor[2]
                              << " mr=" << a.metallicFactor << ',' << a.roughnessFactor
                              << " tex=" << a.baseColorTexture.has_value() << '\n';
                    std::cerr << "m1=" << b.baseColor[0] << ',' << b.baseColor[1] << ',' << b.baseColor[2]
                              << " mr=" << b.metallicFactor << ',' << b.roughnessFactor
                              << " tex=" << b.baseColorTexture.has_value() << '\n';
                }
            }
            return fail("NodePerformanceTest did not collapse into renderer-friendly batches");
        }
        if (nodePerfResult.record.vertexCount > 1'000'000U) {
            return fail("NodePerformanceTest import produced an unexpected vertex count");
        }
    }

    const auto missingResult = manager.importAsset(root / "missing.glb");
    if (missingResult.success || missingResult.error.empty()) {
        return fail("missing asset import was not rejected");
    }

    std::filesystem::remove_all(root, errorCode);
    return EXIT_SUCCESS;
}
