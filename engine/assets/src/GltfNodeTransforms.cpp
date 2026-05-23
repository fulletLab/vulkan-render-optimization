#include "GltfNodeTransforms.hpp"

#include "MeshBounds.hpp"

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

GltfMatrix4 gltfToEngineMatrix(GltfMatrix4 gltfWorldMatrix)
{
    auto conversion = identityGltfMatrix();
    conversion[10] = -1.0;
    return multiplyGltfMatrices(conversion, gltfWorldMatrix);
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

math::Vec3 transformGltfPoint(GltfMatrix4 matrix, math::Vec3 value)
{
    return {
        static_cast<float>(matrix[0] * value.x + matrix[4] * value.y + matrix[8] * value.z + matrix[12]),
        static_cast<float>(matrix[1] * value.x + matrix[5] * value.y + matrix[9] * value.z + matrix[13]),
        static_cast<float>(matrix[2] * value.x + matrix[6] * value.y + matrix[10] * value.z + matrix[14]),
    };
}

math::Vec3 transformGltfVector(GltfMatrix4 matrix, math::Vec3 value)
{
    return {
        static_cast<float>(matrix[0] * value.x + matrix[4] * value.y + matrix[8] * value.z),
        static_cast<float>(matrix[1] * value.x + matrix[5] * value.y + matrix[9] * value.z),
        static_cast<float>(matrix[2] * value.x + matrix[6] * value.y + matrix[10] * value.z),
    };
}

double determinantGltfLinear(GltfMatrix4 matrix)
{
    return matrix[0] * (matrix[5] * matrix[10] - matrix[9] * matrix[6])
        - matrix[4] * (matrix[1] * matrix[10] - matrix[9] * matrix[2])
        + matrix[8] * (matrix[1] * matrix[6] - matrix[5] * matrix[2]);
}

math::Vec3 transformGltfNormal(GltfMatrix4 matrix, math::Vec3 value)
{
    const auto determinant = determinantGltfLinear(matrix);
    if (std::fabs(determinant) <= 0.000000001) {
        return transformGltfVector(matrix, value);
    }

    const auto invDet = 1.0 / determinant;
    const auto inverse00 = (matrix[5] * matrix[10] - matrix[9] * matrix[6]) * invDet;
    const auto inverse01 = (matrix[8] * matrix[6] - matrix[4] * matrix[10]) * invDet;
    const auto inverse02 = (matrix[4] * matrix[9] - matrix[8] * matrix[5]) * invDet;
    const auto inverse10 = (matrix[9] * matrix[2] - matrix[1] * matrix[10]) * invDet;
    const auto inverse11 = (matrix[0] * matrix[10] - matrix[8] * matrix[2]) * invDet;
    const auto inverse12 = (matrix[8] * matrix[1] - matrix[0] * matrix[9]) * invDet;
    const auto inverse20 = (matrix[1] * matrix[6] - matrix[5] * matrix[2]) * invDet;
    const auto inverse21 = (matrix[4] * matrix[2] - matrix[0] * matrix[6]) * invDet;
    const auto inverse22 = (matrix[0] * matrix[5] - matrix[4] * matrix[1]) * invDet;
    return {
        static_cast<float>(inverse00 * value.x + inverse10 * value.y + inverse20 * value.z),
        static_cast<float>(inverse01 * value.x + inverse11 * value.y + inverse21 * value.z),
        static_cast<float>(inverse02 * value.x + inverse12 * value.y + inverse22 * value.z),
    };
}

void applyGltfTransform(MeshPrimitive& primitive, GltfMatrix4 transform)
{
    const bool flipsHandedness = determinantGltfLinear(transform) < 0.0;
    for (auto& vertex : primitive.vertices) {
        vertex.position = transformGltfPoint(transform, vertex.position);
        vertex.normal = transformGltfNormal(transform, vertex.normal);
        if (!normalize(vertex.normal)) {
            vertex.normal = {0.0F, 1.0F, 0.0F};
        }
        vertex.tangent = transformGltfVector(transform, vertex.tangent);
        if (!normalize(vertex.tangent)) {
            vertex.tangent = {1.0F, 0.0F, 0.0F};
        }
        if (flipsHandedness) {
            vertex.tangentSign = -vertex.tangentSign;
        }
    }
    if (flipsHandedness) {
        for (std::size_t index = 2; index < primitive.indices.size(); index += 3) {
            std::swap(primitive.indices[index - 1], primitive.indices[index]);
        }
    }
    updateMeshBounds(primitive);
}

} // namespace projectunity::assets::detail
