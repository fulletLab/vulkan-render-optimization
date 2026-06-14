#include <projectunity/assets/AssetManager.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kPrimitiveCount = 640;

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

bool writeBinary(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return file.good();
}

std::uint32_t align4(std::vector<std::uint8_t>& bytes)
{
    while (bytes.size() % 4U != 0U) {
        bytes.push_back(0);
    }
    return static_cast<std::uint32_t>(bytes.size());
}

bool writeDenseSpatialGlb(const std::filesystem::path& path)
{
    std::vector<std::uint8_t> binary;
    std::ostringstream bufferViews;
    std::ostringstream accessors;
    std::ostringstream meshes;
    const auto comma = [](std::uint32_t index) { return index == 0U ? "" : ","; };
    for (std::uint32_t index = 0; index < kPrimitiveCount; ++index) {
        const auto column = index % 40U;
        const auto row = index / 40U;
        const auto x = static_cast<float>(column) * 2.0F;
        const auto z = static_cast<float>(row) * 2.0F;
        const auto posOffset = align4(binary);
        const std::array<float, 9> positions {{x, 0.0F, z, x + 0.8F, 0.0F, z, x, 0.6F, z + 0.8F}};
        for (const auto value : positions) {
            appendFloat(binary, value);
        }
        const auto normalOffset = align4(binary);
        for (std::uint32_t vertex = 0; vertex < 3U; ++vertex) {
            appendFloat(binary, 0.0F); appendFloat(binary, 1.0F); appendFloat(binary, 0.0F);
        }
        const auto uvOffset = align4(binary);
        for (const auto value : std::array<float, 6> {{0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F}}) {
            appendFloat(binary, value);
        }
        const auto indexOffset = align4(binary);
        appendU16(binary, 0); appendU16(binary, 1); appendU16(binary, 2);
        const auto viewBase = index * 4U;
        bufferViews << comma(index) << "{\"buffer\":0,\"byteOffset\":" << posOffset << ",\"byteLength\":36}"
                    << ",{\"buffer\":0,\"byteOffset\":" << normalOffset << ",\"byteLength\":36}"
                    << ",{\"buffer\":0,\"byteOffset\":" << uvOffset << ",\"byteLength\":24}"
                    << ",{\"buffer\":0,\"byteOffset\":" << indexOffset << ",\"byteLength\":6}";
        accessors << comma(index) << "{\"bufferView\":" << viewBase << ",\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"min\":["
                  << x << ",0," << z << "],\"max\":[" << x + 0.8F << ",0.6," << z + 0.8F << "]}"
                  << ",{\"bufferView\":" << viewBase + 1U << ",\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"}"
                  << ",{\"bufferView\":" << viewBase + 2U << ",\"componentType\":5126,\"count\":3,\"type\":\"VEC2\"}"
                  << ",{\"bufferView\":" << viewBase + 3U << ",\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}";
        const auto accessorBase = index * 4U;
        meshes << comma(index) << "{\"primitives\":[{\"attributes\":{\"POSITION\":" << accessorBase
               << ",\"NORMAL\":" << accessorBase + 1U << ",\"TEXCOORD_0\":" << accessorBase + 2U
               << "},\"indices\":" << accessorBase + 3U << ",\"material\":0}]}";
    }
    std::ostringstream nodes;
    for (std::uint32_t index = 0; index < kPrimitiveCount; ++index) {
        nodes << comma(index) << "{\"mesh\":" << index << "}";
    }
    std::ostringstream sceneNodes;
    for (std::uint32_t index = 0; index < kPrimitiveCount; ++index) {
        sceneNodes << comma(index) << index;
    }
    const auto json = std::string("{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":")
        + std::to_string(binary.size()) + "}],\"bufferViews\":[" + bufferViews.str()
        + "],\"accessors\":[" + accessors.str()
        + "],\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.4,0.4,0.4,1]}}],\"meshes\":["
        + meshes.str() + "],\"nodes\":[" + nodes.str() + "],\"scenes\":[{\"nodes\":["
        + sceneNodes.str() + "]}],\"scene\":0}";
    std::vector<std::uint8_t> jsonBytes(json.begin(), json.end());
    while (jsonBytes.size() % 4U != 0U) {
        jsonBytes.push_back(static_cast<std::uint8_t>(' '));
    }
    align4(binary);
    std::vector<std::uint8_t> glb;
    appendU32(glb, 0x46546c67U); appendU32(glb, 2U);
    appendU32(glb, static_cast<std::uint32_t>(12U + 8U + jsonBytes.size() + 8U + binary.size()));
    appendU32(glb, static_cast<std::uint32_t>(jsonBytes.size())); appendU32(glb, 0x4e4f534aU);
    glb.insert(glb.end(), jsonBytes.begin(), jsonBytes.end());
    appendU32(glb, static_cast<std::uint32_t>(binary.size())); appendU32(glb, 0x004e4942U);
    glb.insert(glb.end(), binary.begin(), binary.end());
    return writeBinary(path, glb);
}

bool writeRepeatedPrimitiveGlb(const std::filesystem::path& path)
{
    std::vector<std::uint8_t> binary;

    const auto posOffset = align4(binary);
    const std::array<float, 9> positions {{0.0F, 0.0F, 0.0F, 0.8F, 0.0F, 0.0F, 0.0F, 0.6F, 0.8F}};
    for (const auto value : positions) {
        appendFloat(binary, value);
    }
    const auto normalOffset = align4(binary);
    for (std::uint32_t vertex = 0; vertex < 3U; ++vertex) {
        appendFloat(binary, 0.0F); appendFloat(binary, 1.0F); appendFloat(binary, 0.0F);
    }
    const auto uvOffset = align4(binary);
    for (const auto value : std::array<float, 6> {{0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F}}) {
        appendFloat(binary, value);
    }
    const auto indexOffset = align4(binary);
    appendU16(binary, 0); appendU16(binary, 1); appendU16(binary, 2);

    std::ostringstream nodes;
    std::ostringstream sceneNodes;
    const auto comma = [](std::uint32_t index) { return index == 0U ? "" : ","; };
    for (std::uint32_t index = 0; index < kPrimitiveCount; ++index) {
        const auto column = index % 40U;
        const auto row = index / 40U;
        const auto x = static_cast<float>(column) * 2.0F;
        const auto z = static_cast<float>(row) * 2.0F;
        nodes << comma(index) << "{\"mesh\":0,\"translation\":[" << x << ",0," << z << "]}";
        sceneNodes << comma(index) << index;
    }

    const auto json = std::string("{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":")
        + std::to_string(binary.size()) + "}],\"bufferViews\":["
        + "{\"buffer\":0,\"byteOffset\":" + std::to_string(posOffset) + ",\"byteLength\":36},"
        + "{\"buffer\":0,\"byteOffset\":" + std::to_string(normalOffset) + ",\"byteLength\":36},"
        + "{\"buffer\":0,\"byteOffset\":" + std::to_string(uvOffset) + ",\"byteLength\":24},"
        + "{\"buffer\":0,\"byteOffset\":" + std::to_string(indexOffset) + ",\"byteLength\":6}],"
        + "\"accessors\":["
        + "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[0.8,0.6,0.8]},"
        + "{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        + "{\"bufferView\":2,\"componentType\":5126,\"count\":3,\"type\":\"VEC2\"},"
        + "{\"bufferView\":3,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}],"
        + "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.4,0.4,0.4,1]}}],"
        + "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2},\"indices\":3,\"material\":0}]}],"
        + "\"nodes\":[" + nodes.str() + "],\"scenes\":[{\"nodes\":[" + sceneNodes.str() + "]}],\"scene\":0}";

    std::vector<std::uint8_t> jsonBytes(json.begin(), json.end());
    while (jsonBytes.size() % 4U != 0U) {
        jsonBytes.push_back(static_cast<std::uint8_t>(' '));
    }
    align4(binary);
    std::vector<std::uint8_t> glb;
    appendU32(glb, 0x46546c67U); appendU32(glb, 2U);
    appendU32(glb, static_cast<std::uint32_t>(12U + 8U + jsonBytes.size() + 8U + binary.size()));
    appendU32(glb, static_cast<std::uint32_t>(jsonBytes.size())); appendU32(glb, 0x4e4f534aU);
    glb.insert(glb.end(), jsonBytes.begin(), jsonBytes.end());
    appendU32(glb, static_cast<std::uint32_t>(binary.size())); appendU32(glb, 0x004e4942U);
    glb.insert(glb.end(), binary.begin(), binary.end());
    return writeBinary(path, glb);
}

bool writeWideSinglePrimitiveGlb(const std::filesystem::path& path)
{
    constexpr std::uint32_t columns = 40U;
    constexpr std::uint32_t rows = 16U;
    constexpr std::uint32_t triangleCount = columns * rows;
    constexpr std::uint32_t vertexCount = triangleCount * 3U;
    std::vector<std::uint8_t> binary;

    const auto posOffset = align4(binary);
    for (std::uint32_t index = 0; index < triangleCount; ++index) {
        const auto column = index % columns;
        const auto row = index / columns;
        const auto x = static_cast<float>(column) * 2.0F;
        const auto z = static_cast<float>(row) * 2.0F;
        const std::array<float, 9> positions {{x, 0.0F, z, x + 0.8F, 0.0F, z, x, 0.6F, z + 0.8F}};
        for (const auto value : positions) {
            appendFloat(binary, value);
        }
    }
    const auto posLength = static_cast<std::uint32_t>(binary.size() - posOffset);

    const auto normalOffset = align4(binary);
    for (std::uint32_t vertex = 0; vertex < vertexCount; ++vertex) {
        appendFloat(binary, 0.0F); appendFloat(binary, 1.0F); appendFloat(binary, 0.0F);
    }
    const auto normalLength = static_cast<std::uint32_t>(binary.size() - normalOffset);

    const auto uvOffset = align4(binary);
    for (std::uint32_t vertex = 0; vertex < vertexCount; ++vertex) {
        appendFloat(binary, 0.0F); appendFloat(binary, 0.0F);
    }
    const auto uvLength = static_cast<std::uint32_t>(binary.size() - uvOffset);

    const auto indexOffset = align4(binary);
    for (std::uint32_t vertex = 0; vertex < vertexCount; ++vertex) {
        appendU32(binary, vertex);
    }
    const auto indexLength = static_cast<std::uint32_t>(binary.size() - indexOffset);

    const auto maxX = static_cast<float>(columns - 1U) * 2.0F + 0.8F;
    const auto maxZ = static_cast<float>(rows - 1U) * 2.0F + 0.8F;
    const auto json = std::string("{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":")
        + std::to_string(binary.size()) + "}],\"bufferViews\":["
        + "{\"buffer\":0,\"byteOffset\":" + std::to_string(posOffset) + ",\"byteLength\":" + std::to_string(posLength) + "},"
        + "{\"buffer\":0,\"byteOffset\":" + std::to_string(normalOffset) + ",\"byteLength\":" + std::to_string(normalLength) + "},"
        + "{\"buffer\":0,\"byteOffset\":" + std::to_string(uvOffset) + ",\"byteLength\":" + std::to_string(uvLength) + "},"
        + "{\"buffer\":0,\"byteOffset\":" + std::to_string(indexOffset) + ",\"byteLength\":" + std::to_string(indexLength) + "}],"
        + "\"accessors\":["
        + "{\"bufferView\":0,\"componentType\":5126,\"count\":" + std::to_string(vertexCount)
        + ",\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[" + std::to_string(maxX) + ",0.6," + std::to_string(maxZ) + "]},"
        + "{\"bufferView\":1,\"componentType\":5126,\"count\":" + std::to_string(vertexCount) + ",\"type\":\"VEC3\"},"
        + "{\"bufferView\":2,\"componentType\":5126,\"count\":" + std::to_string(vertexCount) + ",\"type\":\"VEC2\"},"
        + "{\"bufferView\":3,\"componentType\":5125,\"count\":" + std::to_string(vertexCount) + ",\"type\":\"SCALAR\"}],"
        + "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.4,0.4,0.4,1]}}],"
        + "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2},\"indices\":3,\"material\":0}]}],"
        + "\"nodes\":[{\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}],\"scene\":0}";

    std::vector<std::uint8_t> jsonBytes(json.begin(), json.end());
    while (jsonBytes.size() % 4U != 0U) {
        jsonBytes.push_back(static_cast<std::uint8_t>(' '));
    }
    align4(binary);
    std::vector<std::uint8_t> glb;
    appendU32(glb, 0x46546c67U); appendU32(glb, 2U);
    appendU32(glb, static_cast<std::uint32_t>(12U + 8U + jsonBytes.size() + 8U + binary.size()));
    appendU32(glb, static_cast<std::uint32_t>(jsonBytes.size())); appendU32(glb, 0x4e4f534aU);
    glb.insert(glb.end(), jsonBytes.begin(), jsonBytes.end());
    appendU32(glb, static_cast<std::uint32_t>(binary.size())); appendU32(glb, 0x004e4942U);
    glb.insert(glb.end(), binary.begin(), binary.end());
    return writeBinary(path, glb);
}

} // namespace

int main()
{
    using namespace projectunity::assets;
    const auto root = std::filesystem::temp_directory_path() / "projectunity_spatial_batch_tests";
    std::error_code errorCode;
    std::filesystem::remove_all(root, errorCode);
    std::filesystem::create_directories(root, errorCode);
    const auto glbPath = root / "dense_spatial.glb";
    if (!writeDenseSpatialGlb(glbPath)) {
        return fail("unable to write dense spatial GLB fixture");
    }
    AssetManager manager(root / "Cache" / "Assets");
    const auto result = manager.importModel(glbPath);
    const auto model = manager.model(result.record.id);
    if (!result.success || model == nullptr) {
        std::cerr << result.error << '\n';
        return fail("dense spatial GLB import failed");
    }
    if (model->editorInstances.size() != kPrimitiveCount) {
        return fail("dense spatial GLB lost editable source instances");
    }
    if (model->primitiveInstances.size() != 16U) {
        std::cerr << "primitiveInstances=" << model->primitiveInstances.size() << '\n';
        return fail("dense spatial batching did not preserve the stable 2x2-era runtime batch count");
    }
    if (model->primitiveInstances.size() >= 128U
        && (model->primitiveClusters.empty() || model->primitiveClusters.size() >= model->primitiveInstances.size())) {
        return fail("dense spatial batching did not build a useful cluster hierarchy");
    }

    const auto repeatedPath = root / "repeated_primitive.glb";
    if (!writeRepeatedPrimitiveGlb(repeatedPath)) {
        return fail("unable to write repeated primitive GLB fixture");
    }
    AssetManager repeatedManager(root / "RepeatedCache" / "Assets");
    const auto repeatedResult = repeatedManager.importModel(repeatedPath);
    const auto repeatedModel = repeatedManager.model(repeatedResult.record.id);
    if (!repeatedResult.success || repeatedModel == nullptr) {
        std::cerr << repeatedResult.error << '\n';
        return fail("repeated primitive GLB import failed");
    }
    if (repeatedModel->editorInstances.size() != kPrimitiveCount) {
        return fail("repeated primitive GLB lost editable source instances");
    }
    if (repeatedModel->primitiveInstances.size() != kPrimitiveCount) {
        std::cerr << "repeatedPrimitiveInstances=" << repeatedModel->primitiveInstances.size() << '\n';
        return fail("repeated primitive GLB should keep the stable import-safe path instead of geometry-fusing source instances");
    }

    const auto widePath = root / "wide_single_primitive.glb";
    if (!writeWideSinglePrimitiveGlb(widePath)) {
        return fail("unable to write wide single primitive GLB fixture");
    }
    AssetManager wideManager(root / "WideCache" / "Assets");
    const auto wideResult = wideManager.importModel(widePath);
    const auto wideModel = wideManager.model(wideResult.record.id);
    if (!wideResult.success || wideModel == nullptr) {
        std::cerr << wideResult.error << '\n';
        return fail("wide single primitive GLB import failed");
    }
    if (wideModel->editorInstances.size() != 1U) {
        return fail("wide single primitive GLB lost its editable source instance");
    }
    if (wideModel->primitiveInstances.size() != 1U) {
        std::cerr << "widePrimitiveInstances=" << wideModel->primitiveInstances.size() << '\n';
        return fail("wide medium primitive should not be split by physical extent in the stable cooker path");
    }
    std::filesystem::remove_all(root, errorCode);
    return EXIT_SUCCESS;
}
