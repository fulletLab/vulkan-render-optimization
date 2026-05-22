#include "MeshBounds.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace projectunity::assets::detail {

void updateMeshBounds(MeshPrimitive& primitive) noexcept
{
    if (primitive.vertices.empty()) {
        primitive.bounds = {};
        return;
    }

    auto minimum = primitive.vertices.front().position;
    auto maximum = primitive.vertices.front().position;
    for (const auto& vertex : primitive.vertices) {
        minimum.x = std::min(minimum.x, vertex.position.x);
        minimum.y = std::min(minimum.y, vertex.position.y);
        minimum.z = std::min(minimum.z, vertex.position.z);
        maximum.x = std::max(maximum.x, vertex.position.x);
        maximum.y = std::max(maximum.y, vertex.position.y);
        maximum.z = std::max(maximum.z, vertex.position.z);
    }

    const auto center = (minimum + maximum) * 0.5F;
    float radiusSquared = 0.0F;
    for (const auto& vertex : primitive.vertices) {
        radiusSquared = std::max(radiusSquared, math::distanceSquared(center, vertex.position));
    }

    primitive.bounds.minimum = minimum;
    primitive.bounds.maximum = maximum;
    primitive.bounds.center = center;
    primitive.bounds.radius = std::sqrt(std::max(radiusSquared, 0.0F));
    if (!std::isfinite(primitive.bounds.radius)) {
        primitive.bounds = {};
    }
}

} // namespace projectunity::assets::detail
