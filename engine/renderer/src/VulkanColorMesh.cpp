#include "VulkanColorMesh.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <span>

namespace projectunity::renderer {
namespace {

template <typename T>
[[nodiscard]] std::span<const std::byte> bytesOf(std::span<const T> values)
{
    return {
        reinterpret_cast<const std::byte*>(values.data()),
        values.size_bytes(),
    };
}

} // namespace

bool VulkanColorMeshBuffers::upload(
    VulkanResourceContext context,
    VulkanUploadContext& uploads,
    const RenderColorMeshDraw& draw,
    std::string* errorMessage)
{
    if (draw.vertices.empty() || draw.indices.empty() || draw.indices.size() > std::numeric_limits<std::uint32_t>::max()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Editor color mesh has no GPU uploadable vertices or indices";
        }
        return false;
    }
    if (std::any_of(draw.indices.begin(), draw.indices.end(), [&draw](std::uint32_t index) {
            return index >= draw.vertices.size();
        })) {
        if (errorMessage != nullptr) {
            *errorMessage = "Editor color mesh index references a missing vertex";
        }
        return false;
    }
    if (!vertices.upload(context, uploads, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, bytesOf(draw.vertices), errorMessage)
        || !indices.upload(context, uploads, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, bytesOf(draw.indices), errorMessage)) {
        return false;
    }
    indexCount = static_cast<std::uint32_t>(draw.indices.size());
    return true;
}

} // namespace projectunity::renderer
