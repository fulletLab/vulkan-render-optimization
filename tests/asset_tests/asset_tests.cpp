#include <projectunity/assets/AssetManager.hpp>

#include <algorithm>
#include <array>
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

    appendU16(bytes, 0);
    appendU16(bytes, 1);
    appendU16(bytes, 2);
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
        "buffers":[{"byteLength":102}],
        "bufferViews":[
            {"buffer":0,"byteOffset":0,"byteLength":36},
            {"buffer":0,"byteOffset":36,"byteLength":36},
            {"buffer":0,"byteOffset":72,"byteLength":24},
            {"buffer":0,"byteOffset":96,"byteLength":6}
        ],
        "accessors":[
            {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[-1,-1,0],"max":[1,1,0]},
            {"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
            {"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},
            {"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}
        ],
        "materials":[{
            "pbrMetallicRoughness":{
                "baseColorFactor":[0.25,0.5,0.75,1.0],
                "metallicFactor":0.35,
                "roughnessFactor":0.65
            },
            "emissiveFactor":[0.05,0.1,0.15]
        }],
        "meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3,"material":0}]}],
        "nodes":[{"translation":[6,0,0],"mesh":0}],
        "scenes":[{"nodes":[0]}],
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
    const auto pngPath = root / "tiny.png";
    if (!writeTriangleGlb(glbPath) || !writeTinyPng(pngPath)) {
        return fail("unable to write generated asset fixtures");
    }

    AssetManager manager(root / "Cache" / "Assets");
    const auto modelResult = manager.importModel(glbPath);
    if (!modelResult.success) {
        std::cerr << modelResult.error << '\n';
        return fail("generated GLB import failed");
    }
    const auto model = manager.model(modelResult.record.id);
    if (model == nullptr || model->primitives.size() != 1 || model->primitives.front().vertices.size() != 3) {
        return fail("imported GLB model data is incomplete");
    }
    if (model->primitives.front().vertices.front().tangent.lengthSquared() <= 0.1F) {
        return fail("imported GLB tangent generation produced invalid data");
    }
    const auto nodeTransformApplied = std::all_of(
        model->primitives.front().vertices.begin(),
        model->primitives.front().vertices.end(),
        [](const MeshVertex& vertex) {
            return vertex.position.x >= 5.0F && vertex.position.x <= 7.0F;
        });
    if (!nodeTransformApplied) {
        return fail("imported GLB node transform was not applied to mesh geometry");
    }
    const auto& bounds = model->primitives.front().bounds;
    if (bounds.radius <= 0.1F || bounds.minimum.x < 5.0F || bounds.maximum.x > 7.0F) {
        return fail("imported GLB bounds were not updated after node transform");
    }
    const auto& material = model->materials.front();
    if (material.metallicFactor < 0.34F
        || material.metallicFactor > 0.36F
        || material.roughnessFactor < 0.64F
        || material.roughnessFactor > 0.66F
        || material.emissiveColor[2] < 0.14F
        || material.baseColor[2] < 0.74F) {
        return fail("imported GLB PBR material factors were not preserved");
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

    const auto examplePath = std::filesystem::path(PROJECTUNITY_SOURCE_DIR)
        / "examples" / "basic_assets" / "TexturedTriangle.gltf";
    const auto exampleResult = manager.importModel(examplePath);
    const auto example = manager.model(exampleResult.record.id);
    if (!exampleResult.success || example == nullptr || example->textures.empty()) {
        std::cerr << exampleResult.error << '\n';
        return fail("textured glTF example import failed");
    }

    const auto missingResult = manager.importAsset(root / "missing.glb");
    if (missingResult.success || missingResult.error.empty()) {
        return fail("missing asset import was not rejected");
    }

    std::filesystem::remove_all(root, errorCode);
    return EXIT_SUCCESS;
}
