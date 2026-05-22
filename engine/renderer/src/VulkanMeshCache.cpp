#include "VulkanMeshCache.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace projectunity::renderer {
namespace {

[[nodiscard]] std::span<const std::byte> bytesOf(const std::vector<VulkanGpuVertex>& vertices)
{
    return {
        reinterpret_cast<const std::byte*>(vertices.data()),
        vertices.size() * sizeof(VulkanGpuVertex),
    };
}

[[nodiscard]] std::span<const std::byte> bytesOf(const std::vector<std::uint32_t>& indices)
{
    return {
        reinterpret_cast<const std::byte*>(indices.data()),
        indices.size() * sizeof(std::uint32_t),
    };
}

[[nodiscard]] std::vector<VulkanGpuVertex> packVertices(const assets::MeshPrimitive& primitive)
{
    std::vector<VulkanGpuVertex> packed;
    packed.reserve(primitive.vertices.size());
    for (const auto& vertex : primitive.vertices) {
        VulkanGpuVertex gpuVertex;
        gpuVertex.position[0] = vertex.position.x;
        gpuVertex.position[1] = vertex.position.y;
        gpuVertex.position[2] = vertex.position.z;
        gpuVertex.normal[0] = vertex.normal.x;
        gpuVertex.normal[1] = vertex.normal.y;
        gpuVertex.normal[2] = vertex.normal.z;
        gpuVertex.texCoord[0] = vertex.texCoord[0];
        gpuVertex.texCoord[1] = vertex.texCoord[1];
        packed.push_back(gpuVertex);
    }
    return packed;
}

} // namespace

std::size_t VulkanMeshKeyHash::operator()(const VulkanMeshKey& key) const noexcept
{
    return static_cast<std::size_t>(key.modelAssetId ^ (static_cast<std::uint64_t>(key.primitiveIndex) << 32U));
}

const VulkanMeshBuffers* VulkanMeshCache::ensureUploaded(
    VulkanResourceContext context,
    VulkanUploadContext& uploads,
    VulkanMeshKey key,
    const assets::MeshPrimitive& primitive,
    std::string* errorMessage)
{
    if (const auto existing = meshes_.find(key); existing != meshes_.end()) {
        return &existing->second;
    }
    if (primitive.vertices.empty() || primitive.indices.empty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Imported mesh primitive has no GPU uploadable vertices or indices";
        }
        return nullptr;
    }

    VulkanMeshBuffers next;
    auto vertices = packVertices(primitive);
    if (!next.vertices.upload(context, uploads, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, bytesOf(vertices), errorMessage)
        || !next.indices.upload(context, uploads, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, bytesOf(primitive.indices), errorMessage)) {
        return nullptr;
    }
    next.indexCount = static_cast<std::uint32_t>(primitive.indices.size());
    const auto [it, inserted] = meshes_.try_emplace(key, std::move(next));
    return inserted ? &it->second : nullptr;
}

void VulkanMeshCache::clear() noexcept
{
    meshes_.clear();
}

} // namespace projectunity::renderer
