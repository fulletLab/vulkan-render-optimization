#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace projectunity::asset_tests {

[[nodiscard]] inline bool writeBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return file.good();
}

[[nodiscard]] inline bool writeSidecarTextureFixture(const std::filesystem::path& root)
{
    const auto texturePath = root / "tile_0000.png";
    const std::vector<std::uint8_t> pngBytes {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
        0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53, 0xDE, 0x00, 0x00, 0x00,
        0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0xDA, 0x63, 0xFC, 0xCF, 0xC0, 0x50,
        0x0F, 0x00, 0x04, 0x85, 0x01, 0x80, 0x84, 0xA9, 0x8C, 0x21, 0x00, 0x00,
        0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
    };
    if (!writeBytes(texturePath, pngBytes)) {
        return false;
    }
    const std::string gltf = R"({
        "asset":{"version":"2.0"},
        "buffers":[{"uri":"data:application/octet-stream;base64,AACAvwAAgL8AAAAAAACAPwAAgL8AAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAD8AAIA/AAABAAIAAAA=","byteLength":68}],
        "bufferViews":[
            {"buffer":0,"byteOffset":0,"byteLength":36},
            {"buffer":0,"byteOffset":36,"byteLength":24},
            {"buffer":0,"byteOffset":60,"byteLength":6}
        ],
        "accessors":[
            {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[-1,-1,0],"max":[1,1,0]},
            {"bufferView":1,"componentType":5126,"count":3,"type":"VEC2"},
            {"bufferView":2,"componentType":5123,"count":3,"type":"SCALAR"}
        ],
        "materials":[{"name":"plain","pbrMetallicRoughness":{"baseColorFactor":[0.25,0.25,0.25,1.0]}}],
        "meshes":[{"name":"tile_0000","primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":1},"indices":2,"material":0}]}],
        "nodes":[{"mesh":0}],
        "scenes":[{"nodes":[0]}],
        "scene":0
    })";
    std::ofstream file(root / "sidecar.gltf", std::ios::binary | std::ios::trunc);
    file << gltf;
    return file.good();
}

} // namespace projectunity::asset_tests
