#pragma once

#include <projectunity/assets/AssetManager.hpp>
#include <projectunity/math/Vec3.hpp>
#include <projectunity/renderer/RendererTypes.hpp>
#include <projectunity/scene/Scene.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace projectunity::editor {

struct ViewportRenderWorldCamera {
    math::Vec3 eye;
    math::Vec3 right;
    math::Vec3 up;
    math::Vec3 forward;
    float verticalFovRadians {1.04719755F};
    float aspectRatio {1.0F};
    float nearPlane {0.05F};
    float farPlane {4000.0F};
};

struct ViewportRenderWorldStats {
    std::uint64_t sceneNodeCount {0};
    std::uint64_t renderChunkCount {0};
    std::uint64_t visibleRenderChunkCount {0};
    std::uint64_t renderInstanceCount {0};
    std::uint64_t visibleRenderInstanceCount {0};
    std::uint64_t rebuiltRecordCount {0};
    std::uint64_t reusedRecordCount {0};
    std::uint64_t candidateMeshDrawCount {0};
    std::uint64_t culledMeshDrawCount {0};
    std::uint64_t candidateTriangleCount {0};
    std::uint64_t culledTriangleCount {0};
    std::uint64_t lodMeshDrawCount {0};
    std::uint64_t lodTriangleReductionCount {0};
};

struct ViewportRenderWorldFrame {
    bool hasMeshSceneContent {false};
    bool visibleBoundsValid {false};
    math::Vec3 visibleBoundsCenter;
    float visibleBoundsRadius {0.0F};
    ViewportRenderWorldStats stats;
};

class ViewportRenderWorld final {
public:
    ViewportRenderWorld() = default;
    ~ViewportRenderWorld();

    void markDirty() noexcept;

    [[nodiscard]] ViewportRenderWorldFrame buildFrame(
        const scene::Scene* scene,
        const assets::IAssetManager* assetManager,
        scene::EntityId selectedEntityId,
        const ViewportRenderWorldCamera& camera,
        const renderer::RenderMatrix4& viewProjection,
        int viewportHeight,
        std::vector<renderer::RenderMeshDraw>& meshDraws,
        std::vector<renderer::RenderLight>& lights);

private:
    struct EntityRecord;

    [[nodiscard]] std::shared_ptr<EntityRecord> buildEntityRecord(
        const scene::Scene& scene,
        const assets::IAssetManager& assetManager,
        const scene::Entity& entity,
        const std::unordered_map<std::uint64_t, const scene::Entity*>& primitiveProxyEntities,
        std::uint64_t signature) const;

    const scene::Scene* scene_ {nullptr};
    const assets::IAssetManager* assetManager_ {nullptr};
    bool dirty_ {true};
    std::unordered_map<std::uint64_t, std::shared_ptr<EntityRecord>> records_;
    std::vector<const EntityRecord*> orderedRecords_;
};

} // namespace projectunity::editor
