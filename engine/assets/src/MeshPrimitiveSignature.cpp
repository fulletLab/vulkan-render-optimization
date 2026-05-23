#include "MeshPrimitiveSignature.hpp"

#include <bit>
#include <cstddef>

namespace projectunity::assets::detail {
namespace {

constexpr std::uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

void hashU64(std::uint64_t& hash, std::uint64_t value) noexcept
{
    for (int byte = 0; byte < 8; ++byte) {
        hash ^= (value >> (byte * 8)) & 0xffU;
        hash *= kFnvPrime;
    }
}

void hashFloat(std::uint64_t& hash, float value) noexcept
{
    hashU64(hash, std::bit_cast<std::uint32_t>(value));
}

void hashVertex(std::uint64_t& hash, const MeshVertex& vertex) noexcept
{
    hashFloat(hash, vertex.position.x);
    hashFloat(hash, vertex.position.y);
    hashFloat(hash, vertex.position.z);
    hashFloat(hash, vertex.normal.x);
    hashFloat(hash, vertex.normal.y);
    hashFloat(hash, vertex.normal.z);
    hashFloat(hash, vertex.tangent.x);
    hashFloat(hash, vertex.tangent.y);
    hashFloat(hash, vertex.tangent.z);
    hashFloat(hash, vertex.tangentSign);
    for (const auto value : vertex.texCoord) {
        hashFloat(hash, value);
    }
    for (const auto value : vertex.color) {
        hashFloat(hash, value);
    }
    for (const auto value : vertex.materialFactors) {
        hashFloat(hash, value);
    }
}

[[nodiscard]] bool sameVertex(const MeshVertex& lhs, const MeshVertex& rhs) noexcept
{
    return lhs.position.x == rhs.position.x
        && lhs.position.y == rhs.position.y
        && lhs.position.z == rhs.position.z
        && lhs.normal.x == rhs.normal.x
        && lhs.normal.y == rhs.normal.y
        && lhs.normal.z == rhs.normal.z
        && lhs.tangent.x == rhs.tangent.x
        && lhs.tangent.y == rhs.tangent.y
        && lhs.tangent.z == rhs.tangent.z
        && lhs.tangentSign == rhs.tangentSign
        && lhs.texCoord == rhs.texCoord
        && lhs.color == rhs.color
        && lhs.materialFactors == rhs.materialFactors;
}

} // namespace

std::uint64_t meshPrimitiveSignature(const MeshPrimitive& primitive) noexcept
{
    auto hash = kFnvOffset;
    hashU64(hash, static_cast<std::uint64_t>(primitive.materialIndex));
    hashU64(hash, static_cast<std::uint64_t>(primitive.vertices.size()));
    hashU64(hash, static_cast<std::uint64_t>(primitive.indices.size()));
    for (const auto& vertex : primitive.vertices) {
        hashVertex(hash, vertex);
    }
    for (const auto index : primitive.indices) {
        hashU64(hash, index);
    }
    return hash;
}

bool meshPrimitivesEqual(const MeshPrimitive& lhs, const MeshPrimitive& rhs) noexcept
{
    if (lhs.materialIndex != rhs.materialIndex
        || lhs.indices != rhs.indices
        || lhs.vertices.size() != rhs.vertices.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.vertices.size(); ++index) {
        if (!sameVertex(lhs.vertices[index], rhs.vertices[index])) {
            return false;
        }
    }
    return true;
}

} // namespace projectunity::assets::detail
