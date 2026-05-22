#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tinygltf {
class Model;
} // namespace tinygltf

namespace projectunity::assets::detail {

[[nodiscard]] bool readFloatAttribute(
    const tinygltf::Model& model,
    int accessorIndex,
    std::size_t expectedComponents,
    std::vector<float>& output,
    std::string* errorMessage);

[[nodiscard]] bool readColorAttribute(
    const tinygltf::Model& model,
    int accessorIndex,
    std::vector<float>& output,
    std::string* errorMessage);

[[nodiscard]] bool readIndices(
    const tinygltf::Model& model,
    int accessorIndex,
    std::size_t vertexCount,
    std::vector<std::uint32_t>& output,
    std::string* errorMessage);

} // namespace projectunity::assets::detail
