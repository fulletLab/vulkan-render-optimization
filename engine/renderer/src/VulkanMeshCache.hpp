#pragma once

#include "VulkanGpuBuffer.hpp"

#include <projectunity/assets/AssetManager.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>

namespace projectunity::renderer {

struct VulkanGpuVertex {
    float position[3] {};
    float normal[3] {};
    float texCoord[2] {};
    float color[4] {1.0F, 1.0F, 1.0F, 1.0F};
    float tangent[4] {1.0F, 0.0F, 0.0F, 1.0F};
};

struct VulkanMeshKey {
    std::uint64_t modelAssetId {0};
    std::uint32_t primitiveIndex {0};

    [[nodiscard]] bool operator==(const VulkanMeshKey&) const noexcept = default;
};

struct VulkanMeshKeyHash {
    [[nodiscard]] std::size_t operator()(const VulkanMeshKey& key) const noexcept;
};

struct VulkanMeshBuffers {
    VulkanGpuBuffer vertices;
    VulkanGpuBuffer indices;
    std::uint32_t indexCount {0};
};

class VulkanMeshCache final {
public:
    [[nodiscard]] const VulkanMeshBuffers* ensureUploaded(
        VulkanResourceContext context,
        VulkanUploadContext& uploads,
        VulkanMeshKey key,
        const assets::MeshPrimitive& primitive,
        std::string* errorMessage);
    void clear() noexcept;
    [[nodiscard]] std::uint64_t uploadCount() const noexcept;
    [[nodiscard]] std::uint64_t uploadedBytes() const noexcept;
    [[nodiscard]] std::uint64_t meshCount() const noexcept;

private:
    std::unordered_map<VulkanMeshKey, VulkanMeshBuffers, VulkanMeshKeyHash> meshes_;
    std::uint64_t uploadCount_ {0};
    std::uint64_t uploadedBytes_ {0};
};

} // namespace projectunity::renderer
