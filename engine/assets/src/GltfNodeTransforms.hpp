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
[[nodiscard]] GltfMatrix4 gltfToEngineMatrix(GltfMatrix4 gltfWorldMatrix);
[[nodiscard]] GltfMatrix4 gltfNodeMatrix(const tinygltf::Node& node);
[[nodiscard]] math::Vec3 transformGltfPoint(GltfMatrix4 matrix, math::Vec3 value);
[[nodiscard]] math::Vec3 transformGltfVector(GltfMatrix4 matrix, math::Vec3 value);
[[nodiscard]] double determinantGltfLinear(GltfMatrix4 matrix);
void applyGltfTransform(MeshPrimitive& primitive, GltfMatrix4 transform);

} // namespace projectunity::assets::detail
