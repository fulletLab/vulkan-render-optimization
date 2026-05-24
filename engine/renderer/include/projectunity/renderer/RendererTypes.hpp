#pragma once

#include <projectunity/assets/AssetManager.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace projectunity::renderer {

constexpr std::size_t kMaxFrameLights = 8;
constexpr std::size_t kMaxShadowCascades = 4;
constexpr std::size_t kMaxShadowViews = 6;

enum class RenderBackend : std::uint8_t {
    Vulkan,
};

struct RendererConfig {
    std::string applicationName {"ProjectUnity"};
    bool enableValidation {true};
    bool enableRenderDocMarkers {true};
};

struct RendererStats {
    RenderBackend backend {RenderBackend::Vulkan};
    std::string gpuName;
    std::uint32_t apiVersionMajor {0};
    std::uint32_t apiVersionMinor {0};
    std::uint32_t apiVersionPatch {0};
    bool validationEnabled {false};
    bool debugMarkersAvailable {false};
    bool vmaAllocatorReady {false};
    std::uint64_t viewportFramesPresented {0};
    std::uint64_t lastFrameCandidateMeshDrawCount {0};
    std::uint64_t lastFrameCulledMeshDrawCount {0};
    std::uint64_t lastFrameMeshDrawCount {0};
    std::uint64_t lastFrameMeshBatchCount {0};
    std::uint64_t lastFrameColorMeshDrawCount {0};
    std::uint64_t lastFrameCandidateTriangleCount {0};
    std::uint64_t lastFrameCulledTriangleCount {0};
    std::uint64_t lastFrameVisibleTriangleCount {0};
    std::uint64_t lastFrameLodMeshDrawCount {0};
    std::uint64_t lastFrameLodTriangleReductionCount {0};
    std::uint64_t lastFrameSceneNodeCount {0};
    std::uint64_t lastFrameRenderChunkCount {0};
    std::uint64_t lastFrameVisibleRenderChunkCount {0};
    std::uint64_t lastFrameRenderInstanceCount {0};
    std::uint64_t lastFrameVisibleRenderInstanceCount {0};
    std::uint64_t lastFrameLightCount {0};
    std::uint64_t lastFrameShadowCasterCount {0};
    std::uint64_t lastFrameShadowViewCount {0};
    std::uint64_t lastFrameShadowBatchCount {0};
    std::uint64_t lastFrameShadowCulledBatchCount {0};
    std::uint64_t lastFrameRenderCpuTimeUs {0};
    std::uint64_t averageRenderCpuTimeUs {0};
    std::uint64_t lastFrameResourcePrepareCpuTimeUs {0};
    std::uint64_t lastFrameCommandRecordCpuTimeUs {0};
    std::uint64_t lastFrameShadowRecordCpuTimeUs {0};
    std::uint64_t lastFrameMeshRecordCpuTimeUs {0};
    std::uint64_t lastFrameColorRecordCpuTimeUs {0};
    bool gpuTimestampsSupported {false};
    bool lastFrameGpuTimestampsValid {false};
    std::uint64_t lastFrameGpuTimeUs {0};
    std::uint64_t lastFrameShadowGpuTimeUs {0};
    std::uint64_t lastFrameMeshGpuTimeUs {0};
    std::uint64_t lastFrameColorGpuTimeUs {0};
    std::uint64_t lastFrameMeshUploadCount {0};
    std::uint64_t lastFrameTextureUploadCount {0};
    std::uint64_t lastFrameStaticUploadBytes {0};
    std::uint64_t lastFrameColorUploadBytes {0};
    std::uint64_t residentMeshCount {0};
    std::uint64_t residentTextureCount {0};
    std::uint64_t totalMeshUploadCount {0};
    std::uint64_t totalTextureUploadCount {0};
    std::uint64_t totalStaticUploadBytes {0};
    std::uint64_t totalColorUploadBytes {0};
    std::uint64_t meshDrawsPresented {0};
    std::uint64_t texturedMeshDrawsPresented {0};
    std::uint64_t colorMeshDrawsPresented {0};
    std::uint64_t shadowFramesPresented {0};
    std::uint64_t shadowCasterDrawsPresented {0};
};

struct RenderClearColor {
    float red {0.12F};
    float green {0.13F};
    float blue {0.15F};
    float alpha {1.0F};
};

struct RenderEnvironmentSettings {
    std::array<float, 3> skyColor {0.30F, 0.38F, 0.55F};
    std::array<float, 3> groundColor {0.08F, 0.07F, 0.055F};
    float intensity {1.0F};
    const assets::TextureAsset* sourceTexture {nullptr};
};

struct RenderMatrix4 {
    std::array<float, 16> values {
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    };
};

enum class RenderLightType : std::uint8_t {
    Directional,
    Point,
    Spot,
};

enum class RenderShadowMode : std::uint8_t {
    None,
    DirectionalCascades,
    Spot2D,
    PointCubemap,
    Point2DFallback,
};

struct RenderLight {
    RenderLightType type {RenderLightType::Directional};
    std::array<float, 3> position {0.0F, 0.0F, 0.0F};
    std::array<float, 3> direction {0.35F, -0.82F, 0.45F};
    std::array<float, 3> color {1.0F, 0.98F, 0.92F};
    float intensity {3.0F};
    float range {0.0F};
    float innerConeAngle {0.0F};
    float outerConeAngle {0.7853981634F};
};

struct RenderMeshDraw {
    assets::AssetId modelAssetId;
    std::uint32_t primitiveIndex {0};
    std::uint32_t lodIndex {0};
    const assets::MeshPrimitive* primitive {nullptr};
    const assets::MaterialAsset* material {nullptr};
    const assets::TextureAsset* baseColorTexture {nullptr};
    const assets::TextureAsset* normalTexture {nullptr};
    const assets::TextureAsset* metallicRoughnessTexture {nullptr};
    const assets::TextureAsset* occlusionTexture {nullptr};
    const assets::TextureAsset* emissiveTexture {nullptr};
    float sortDepth {0.0F};
    std::array<float, 3> worldBoundsCenter {0.0F, 0.0F, 0.0F};
    float worldBoundsRadius {0.0F};
    RenderMatrix4 modelMatrix;
    RenderMatrix4 modelViewProjection;
    bool flipsWinding {false};
    std::uint64_t renderInstanceId {0};
    std::uint64_t renderChunkId {0};
    std::uint64_t sceneNodeId {0};
};

struct RenderColorVertex {
    std::array<float, 3> position {};
    std::array<float, 4> color {1.0F, 1.0F, 1.0F, 1.0F};
};

struct RenderColorMeshDraw {
    std::span<const RenderColorVertex> vertices;
    std::span<const std::uint32_t> indices;
    RenderMatrix4 modelViewProjection;
};

struct RenderFrame {
    RenderClearColor clearColor;
    RenderMatrix4 viewProjection;
    RenderMatrix4 shadowViewProjection;
    std::array<RenderMatrix4, kMaxShadowViews> shadowViewProjections {};
    std::array<float, kMaxShadowCascades> shadowCascadeSplits {};
    std::array<float, 3> cameraPosition {0.0F, 0.0F, 0.0F};
    std::array<float, 3> visibleBoundsCenter {0.0F, 0.0F, 0.0F};
    float visibleBoundsRadius {0.0F};
    std::array<float, 3> ambientSkyColor {0.22F, 0.28F, 0.40F};
    std::array<float, 3> ambientGroundColor {0.07F, 0.06F, 0.05F};
    RenderEnvironmentSettings environment;
    std::span<const RenderLight> lights;
    bool shadowsEnabled {false};
    std::uint32_t shadowLightIndex {0};
    std::uint32_t shadowViewCount {0};
    std::uint32_t shadowCascadeCount {0};
    float shadowDepthFarPlane {0.0F};
    RenderShadowMode shadowMode {RenderShadowMode::None};
    std::uint64_t candidateMeshDrawCount {0};
    std::uint64_t culledMeshDrawCount {0};
    std::uint64_t candidateTriangleCount {0};
    std::uint64_t culledTriangleCount {0};
    std::uint64_t lodMeshDrawCount {0};
    std::uint64_t lodTriangleReductionCount {0};
    std::uint64_t sceneNodeCount {0};
    std::uint64_t renderChunkCount {0};
    std::uint64_t visibleRenderChunkCount {0};
    std::uint64_t renderInstanceCount {0};
    std::uint64_t visibleRenderInstanceCount {0};
    std::span<const RenderMeshDraw> meshDraws;
    std::span<const RenderColorMeshDraw> colorMeshDraws;
};

} // namespace projectunity::renderer
