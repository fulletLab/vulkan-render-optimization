#pragma once

#include <projectunity/assets/AssetManager.hpp>

#include <array>

namespace tinygltf {
struct Node;
} // namespace tinygltf

namespace projectunity::assets::detail {

using GltfMatrix4 = std::array<double, 16>;

[[nodiscard]] GltfMatrix4 identityGltfMatrix();
[[nodiscard]] GltfMatrix4 multiplyGltfMatrices(GltfMatrix4 lhs, GltfMatrix4 rhs);
[[nodiscard]] GltfMatrix4 gltfNodeMatrix(const tinygltf::Node& node);
void applyGltfTransform(MeshPrimitive& primitive, GltfMatrix4 transform);

} // namespace projectunity::assets::detail
