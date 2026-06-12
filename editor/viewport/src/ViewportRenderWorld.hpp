#pragma once

#include <projectunity/assets/AssetManager.hpp>
#include <projectunity/math/Vec3.hpp>
#include <projectunity/renderer/RenderShadowSetup.hpp>
#include <projectunity/renderer/RendererTypes.hpp>
#include <projectunity/scene/Scene.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace projectunity::editor {

struct ViewportFrameBounds;
class ViewportOcclusionBuffer;
class ViewportSceneEntityLookup;

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
    std::uint64_t hlodMeshDrawCount {0};
    std::uint64_t hlodCandidateDrawCount {0};
    std::uint64_t hlodTriangleReductionCount {0};
    std::uint64_t occlusionTestedChunkCount {0};
    std::uint64_t occlusionRejectedChunkCount {0};
    std::uint64_t occlusionOccluderChunkCount {0};
    std::uint64_t occlusionRejectedInstanceCount {0};
    std::uint64_t occlusionRejectedTriangleCount {0};
    std::uint64_t shadowCandidateInstances {0};
    std::uint64_t shadowPolicyRejectedInstances {0};
    std::uint64_t largeRenderChunkCount {0};
    std::uint64_t largestRenderChunkTriangleCount {0};
    std::uint64_t largestRenderChunkInstanceCount {0};
    float maxRenderChunkExtent {0.0F};
};

struct ViewportRenderWorldChunkDebug {
    std::array<math::Vec3, 8> corners {};
    std::uint64_t triangleCount {0};
    std::uint64_t instanceCount {0};
    float maxExtent {0.0F};
    bool visible {false};
    bool large {false};
};

struct ViewportRenderWorldFrame {
    bool hasMeshSceneContent {false};
    bool visibleBoundsValid {false};
    math::Vec3 visibleBoundsCenter;
    float visibleBoundsRadius {0.0F};
    ViewportRenderWorldStats stats;
    std::vector<ViewportRenderWorldChunkDebug> debugChunks;
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

    void collectShadowCasters(
        const renderer::RenderShadowMapSelection& shadowSelection,
        const ViewportRenderWorldCamera& camera,
        int viewportHeight,
        scene::EntityId selectedEntityId,
        std::vector<renderer::RenderMeshDraw>& shadowMeshDraws,
        ViewportRenderWorldStats& stats) const;

private:
    struct EntityRecord;

    [[nodiscard]] std::shared_ptr<EntityRecord> buildEntityRecord(
        const ViewportSceneEntityLookup& entityLookup,
        const assets::IAssetManager& assetManager,
        const scene::Entity& entity,
        const std::unordered_map<std::uint64_t, const scene::Entity*>& primitiveProxyEntities,
        std::uint64_t signature) const;

    void finalizeEntityRecord(EntityRecord& record) const;
    void rebuildOverviewRecords();
    [[nodiscard]] bool tryEmitOverviewRecord(
        const EntityRecord& record,
        assets::AssetId selectedPrimitiveModel,
        std::uint32_t selectedPrimitiveIndex,
        const ViewportRenderWorldCamera& camera,
        const renderer::RenderMatrix4& viewProjection,
        int viewportHeight,
        const ViewportOcclusionBuffer* occlusionBuffer,
        const std::unordered_set<std::uint64_t>* occluderChunkIds,
        bool countVisibleChunks,
        std::vector<renderer::RenderMeshDraw>& meshDraws,
        ViewportRenderWorldStats& stats,
        ViewportFrameBounds& visibleBounds,
        std::uint64_t& visibleSourceTriangleCount) const;

    const scene::Scene* scene_ {nullptr};
    const assets::IAssetManager* assetManager_ {nullptr};
    bool dirty_ {true};
    std::uint64_t lastDebugSignature_ {0};
    std::unordered_map<std::uint64_t, std::shared_ptr<EntityRecord>> records_;
    std::vector<const EntityRecord*> orderedRecords_;
    std::vector<std::shared_ptr<EntityRecord>> overviewRecords_;
};

} // namespace projectunity::editor
