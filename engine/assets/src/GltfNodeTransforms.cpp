#include "GltfNodeTransforms.hpp"

#include <tiny_gltf.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace projectunity::assets::detail {
namespace {

constexpr float kNormalizeEpsilon = 0.000001F;

[[nodiscard]] bool normalize(math::Vec3& value)
{
    const auto length = value.length();
    if (length <= kNormalizeEpsilon || !std::isfinite(length)) {
        return false;
    }
    value = value / length;
    return true;
}

[[nodiscard]] math::Vec3 transformPoint(GltfMatrix4 matrix, math::Vec3 value)
{
    return {
        static_cast<float>(matrix[0] * value.x + matrix[4] * value.y + matrix[8] * value.z + matrix[12]),
        static_cast<float>(matrix[1] * value.x + matrix[5] * value.y + matrix[9] * value.z + matrix[13]),
        static_cast<float>(matrix[2] * value.x + matrix[6] * value.y + matrix[10] * value.z + matrix[14]),
    };
}

[[nodiscard]] math::Vec3 transformVector(GltfMatrix4 matrix, math::Vec3 value)
{
    return {
        static_cast<float>(matrix[0] * value.x + matrix[4] * value.y + matrix[8] * value.z),
        static_cast<float>(matrix[1] * value.x + matrix[5] * value.y + matrix[9] * value.z),
        static_cast<float>(matrix[2] * value.x + matrix[6] * value.y + matrix[10] * value.z),
    };
}

} // namespace

GltfMatrix4 identityGltfMatrix()
{
    GltfMatrix4 result {};
    result[0] = result[5] = result[10] = result[15] = 1.0;
    return result;
}

GltfMatrix4 multiplyGltfMatrices(GltfMatrix4 lhs, GltfMatrix4 rhs)
{
    GltfMatrix4 result {};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            for (int index = 0; index < 4; ++index) {
                result[static_cast<std::size_t>(column * 4 + row)]
                    += lhs[static_cast<std::size_t>(index * 4 + row)]
                    * rhs[static_cast<std::size_t>(column * 4 + index)];
            }
        }
    }
    return result;
}

GltfMatrix4 gltfNodeMatrix(const tinygltf::Node& node)
{
    if (node.matrix.size() == 16U) {
        GltfMatrix4 result {};
        std::copy(node.matrix.begin(), node.matrix.end(), result.begin());
        return result;
    }

    const auto translation = node.translation.size() == 3U ? node.translation : std::vector<double> {0.0, 0.0, 0.0};
    const auto scale = node.scale.size() == 3U ? node.scale : std::vector<double> {1.0, 1.0, 1.0};
    const auto rotation = node.rotation.size() == 4U ? node.rotation : std::vector<double> {0.0, 0.0, 0.0, 1.0};
    const auto x = rotation[0];
    const auto y = rotation[1];
    const auto z = rotation[2];
    const auto w = rotation[3];
    auto result = identityGltfMatrix();
    result[0] = (1.0 - 2.0 * (y * y + z * z)) * scale[0];
    result[1] = (2.0 * (x * y + z * w)) * scale[0];
    result[2] = (2.0 * (x * z - y * w)) * scale[0];
    result[4] = (2.0 * (x * y - z * w)) * scale[1];
    result[5] = (1.0 - 2.0 * (x * x + z * z)) * scale[1];
    result[6] = (2.0 * (y * z + x * w)) * scale[1];
    result[8] = (2.0 * (x * z + y * w)) * scale[2];
    result[9] = (2.0 * (y * z - x * w)) * scale[2];
    result[10] = (1.0 - 2.0 * (x * x + y * y)) * scale[2];
    result[12] = translation[0];
    result[13] = translation[1];
    result[14] = translation[2];
    return result;
}

void applyGltfTransform(MeshPrimitive& primitive, GltfMatrix4 transform)
{
    for (auto& vertex : primitive.vertices) {
        vertex.position = transformPoint(transform, vertex.position);
        vertex.normal = transformVector(transform, vertex.normal);
        if (!normalize(vertex.normal)) {
            vertex.normal = {0.0F, 1.0F, 0.0F};
        }
        vertex.tangent = transformVector(transform, vertex.tangent);
        if (!normalize(vertex.tangent)) {
            vertex.tangent = {1.0F, 0.0F, 0.0F};
        }
    }
}

} // namespace projectunity::assets::detail
