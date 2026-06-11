#include "ViewportMeshLod.hpp"

#include <algorithm>
#include <cmath>

namespace projectunity::editor {

std::size_t indexCountForViewportLod(const assets::MeshPrimitive& primitive, std::uint32_t lodIndex)
{
    if (lodIndex == 0U || lodIndex - 1U >= primitive.lods.size()) {
        return primitive.indices.size();
    }
    const auto& indices = primitive.lods[lodIndex - 1U].indices;
    return indices.empty() ? primitive.indices.size() : indices.size();
}

std::uint32_t selectViewportMeshLod(
    const assets::MeshPrimitive& primitive,
    float boundsRadius,
    float depth,
    float verticalFovRadians,
    float viewportHeight,
    bool forceFullResolution) noexcept
{
    (void)primitive;
    (void)boundsRadius;
    (void)depth;
    (void)verticalFovRadians;
    (void)viewportHeight;
    (void)forceFullResolution;
    return 0U;
}

} // namespace projectunity::editor
