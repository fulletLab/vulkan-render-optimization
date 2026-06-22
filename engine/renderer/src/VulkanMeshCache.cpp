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
        gpuVertex.color[0] = vertex.color[0];
        gpuVertex.color[1] = vertex.color[1];
        gpuVertex.color[2] = vertex.color[2];
        gpuVertex.color[3] = vertex.color[3];
        gpuVertex.tangent[0] = vertex.tangent.x;
        gpuVertex.tangent[1] = vertex.tangent.y;
        gpuVertex.tangent[2] = vertex.tangent.z;
        gpuVertex.tangent[3] = vertex.tangentSign;
        gpuVertex.materialFactors[0] = vertex.materialFactors[0];
        gpuVertex.materialFactors[1] = vertex.materialFactors[1];
        gpuVertex.materialFactors[2] = vertex.materialFactors[2];
        gpuVertex.materialFactors[3] = vertex.materialFactors[3];
        packed.push_back(gpuVertex);
    }
    return packed;
}

[[nodiscard]] const std::vector<std::uint32_t>& indicesForLod(
    const assets::MeshPrimitive& primitive,
    std::uint32_t lodIndex) noexcept
{
    if (lodIndex == 0U || lodIndex - 1U >= primitive.lods.size() || primitive.lods[lodIndex - 1U].indices.empty()) {
        return primitive.indices;
    }
    return primitive.lods[lodIndex - 1U].indices;
}

} // namespace

std::size_t VulkanMeshKeyHash::operator()(const VulkanMeshKey& key) const noexcept
{
    const auto primitive = static_cast<std::uint64_t>(key.primitiveIndex) << 32U;
    const auto lod = static_cast<std::uint64_t>(key.lodIndex) * 0x9E3779B185EBCA87ULL;
    return static_cast<std::size_t>(key.modelAssetId ^ primitive ^ lod);
}

std::size_t VulkanPrimitiveKeyHash::operator()(const VulkanPrimitiveKey& key) const noexcept
{
    return static_cast<std::size_t>(key.modelAssetId ^ (static_cast<std::uint64_t>(key.primitiveIndex) << 32U));
}

bool VulkanMeshCache::ensureVertexUploaded(
    VulkanResourceContext context,
    VulkanUploadContext& uploads,
    VulkanPrimitiveKey key,
    const assets::MeshPrimitive& primitive,
    VkBuffer* vertexBuffer,
    std::string* errorMessage)
{
    if (vertexBuffer == nullptr) {
        return false;
    }
    if (const auto existing = vertexBuffers_.find(key); existing != vertexBuffers_.end()) {
        *vertexBuffer = existing->second.buffer();
        return *vertexBuffer != VK_NULL_HANDLE;
    }
    auto vertices = packVertices(primitive);
    const auto vertexBytes = bytesOf(vertices);
    VulkanGpuBuffer next;
    if (!next.upload(context, uploads, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertexBytes, errorMessage)) {
        return false;
    }
    const auto [it, inserted] = vertexBuffers_.try_emplace(key, std::move(next));
    if (inserted) {
        ++uploadCount_;
        uploadedBytes_ += static_cast<std::uint64_t>(vertexBytes.size());
    }
    *vertexBuffer = it->second.buffer();
    return *vertexBuffer != VK_NULL_HANDLE;
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
    const auto& indices = indicesForLod(primitive, key.lodIndex);
    if (primitive.vertices.empty() || indices.empty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Imported mesh primitive has no GPU uploadable vertices or indices";
        }
        return nullptr;
    }

    VulkanMeshBuffers next;
    if (!ensureVertexUploaded(
            context,
            uploads,
            {key.modelAssetId, key.primitiveIndex},
            primitive,
            &next.vertices,
            errorMessage)) {
        return nullptr;
    }
    const auto indexBytes = bytesOf(indices);
    if (!next.indices.upload(context, uploads, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indexBytes, errorMessage)) {
        return nullptr;
    }
    next.indexCount = static_cast<std::uint32_t>(indices.size());
    const auto [it, inserted] = meshes_.try_emplace(key, std::move(next));
    if (inserted) {
        ++uploadCount_;
        uploadedBytes_ += static_cast<std::uint64_t>(indexBytes.size());
    }
    return inserted ? &it->second : nullptr;
}

bool VulkanMeshCache::isUploaded(VulkanMeshKey key) const noexcept
{
    return meshes_.find(key) != meshes_.end();
}

const VulkanMeshBuffers* VulkanMeshCache::uploaded(VulkanMeshKey key) const noexcept
{
    const auto existing = meshes_.find(key);
    return existing == meshes_.end() ? nullptr : &existing->second;
}

std::uint64_t VulkanMeshCache::estimatedUploadBytes(
    VulkanMeshKey key,
    const assets::MeshPrimitive& primitive) const noexcept
{
    if (isUploaded(key)) {
        return 0;
    }
    std::uint64_t bytes = 0;
    const VulkanPrimitiveKey primitiveKey {key.modelAssetId, key.primitiveIndex};
    if (vertexBuffers_.find(primitiveKey) == vertexBuffers_.end()) {
        bytes += static_cast<std::uint64_t>(primitive.vertices.size()) * sizeof(VulkanGpuVertex);
    }
    bytes += static_cast<std::uint64_t>(indicesForLod(primitive, key.lodIndex).size()) * sizeof(std::uint32_t);
    return bytes;
}

void VulkanMeshCache::clear() noexcept
{
    vertexBuffers_.clear();
    meshes_.clear();
    uploadCount_ = 0;
    uploadedBytes_ = 0;
}

std::uint64_t VulkanMeshCache::uploadCount() const noexcept
{
    return uploadCount_;
}

std::uint64_t VulkanMeshCache::uploadedBytes() const noexcept
{
    return uploadedBytes_;
}

std::uint64_t VulkanMeshCache::meshCount() const noexcept
{
    return static_cast<std::uint64_t>(vertexBuffers_.size() + meshes_.size());
}

} // namespace projectunity::renderer
