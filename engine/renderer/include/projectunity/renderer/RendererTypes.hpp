#pragma once

#include <projectunity/assets/AssetManager.hpp>

#include <algorithm>
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

enum class RenderShadowUpdateMode : std::uint8_t {
    Live,
    Frozen,
    Off,
};

enum class RenderTextureQuality : std::uint8_t {
    Low,
    Medium,
    High,
    Ultra,
};

enum class RenderFramePath : std::uint8_t {
    SceneView,
    GameView,
    GameRuntime,
};

[[nodiscard]] inline const char* renderFramePathName(RenderFramePath path) noexcept
{
    switch (path) {
    case RenderFramePath::SceneView: return "SceneView";
    case RenderFramePath::GameView: return "GameView";
    case RenderFramePath::GameRuntime: return "GameRuntime";
    }
    return "Unknown";
}

enum class RenderTextureDebugAnisotropyOverride : std::uint8_t {
    Automatic,
    ForceOff,
    ForceOn,
};

[[nodiscard]] inline const char* renderTextureDebugAnisotropyOverrideName(
    RenderTextureDebugAnisotropyOverride value) noexcept
{
    switch (value) {
    case RenderTextureDebugAnisotropyOverride::Automatic: return "automatic";
    case RenderTextureDebugAnisotropyOverride::ForceOff: return "force-off";
    case RenderTextureDebugAnisotropyOverride::ForceOn: return "force-on";
    }
    return "automatic";
}

struct RenderTextureDebugSettings {
    bool forceMaxLodZero {false};
    RenderTextureDebugAnisotropyOverride anisotropyOverride {RenderTextureDebugAnisotropyOverride::Automatic};
    bool overrideMipLodBias {false};
    float mipLodBias {0.0F};
    std::uint64_t revision {0};

    [[nodiscard]] bool operator==(const RenderTextureDebugSettings&) const noexcept = default;
};

[[nodiscard]] inline const char* renderTextureQualityName(RenderTextureQuality quality) noexcept
{
    switch (quality) {
    case RenderTextureQuality::Low: return "Low";
    case RenderTextureQuality::Medium: return "Medium";
    case RenderTextureQuality::High: return "High";
    case RenderTextureQuality::Ultra: return "Ultra";
    }
    return "Unknown";
}

[[nodiscard]] inline float renderTextureQualityMaxAnisotropy(RenderTextureQuality quality) noexcept
{
    switch (quality) {
    case RenderTextureQuality::Low: return 1.0F;
    case RenderTextureQuality::Medium: return 4.0F;
    case RenderTextureQuality::High: return 16.0F;
    case RenderTextureQuality::Ultra: return 16.0F;
    }
    return 1.0F;
}

struct RendererConfig {
    std::string applicationName {"ProjectUnity"};
    bool enableValidation {true};
    bool enableRenderDocMarkers {true};
    RenderTextureQuality textureQuality {RenderTextureQuality::High};
    float requestedMaxSamplerAnisotropy {0.0F};
    float textureMipLodBias {0.0F};
};

constexpr std::uint32_t kRenderOverviewPrimitiveIndexBase = 0x80000000U;
constexpr std::size_t kRenderLodCounterCount = 4U;

enum class RenderLodSelectionReason : std::uint8_t {
    Unspecified,
    FullResolution,
    ScreenError,
    HysteresisHold,
    HlodScreenSize,
    HlodChunkBudget,
    HlodDrawBudget,
    HlodDebugOverride,
    HlodHysteresisHold,
};

struct RenderLodBreakdown {
    std::array<std::uint64_t, kRenderLodCounterCount> draws {};
    std::array<std::uint64_t, kRenderLodCounterCount> triangles {};
    std::array<std::uint64_t, kRenderLodCounterCount> maxDrawTriangles {};
};

struct RenderMaterialBreakdown {
    std::uint64_t opaqueDraws {0};
    std::uint64_t opaqueTriangles {0};
    std::uint64_t alphaMaskDraws {0};
    std::uint64_t alphaMaskTriangles {0};
    std::uint64_t blendDraws {0};
    std::uint64_t blendTriangles {0};
    std::uint64_t doubleSidedDraws {0};
    std::uint64_t doubleSidedTriangles {0};
};

[[nodiscard]] inline bool isOverviewRenderMeshDraw(const struct RenderMeshDraw& draw) noexcept;
[[nodiscard]] inline std::uint64_t renderMeshDrawIndexCount(const struct RenderMeshDraw& draw) noexcept;
[[nodiscard]] inline std::uint64_t renderMeshDrawTriangleCount(const struct RenderMeshDraw& draw) noexcept;
[[nodiscard]] inline std::size_t renderMeshDrawLodCounterSlot(const struct RenderMeshDraw& draw) noexcept;
inline void accumulateRenderLodBreakdown(
    RenderLodBreakdown& breakdown,
    const struct RenderMeshDraw& draw,
    std::uint32_t instanceCount,
    std::uint64_t indexCount) noexcept;
inline void accumulateRenderMaterialBreakdown(
    RenderMaterialBreakdown& breakdown,
    const struct RenderMeshDraw& draw,
    std::uint32_t instanceCount,
    std::uint64_t indexCount) noexcept;

struct RendererStats {
    RenderBackend backend {RenderBackend::Vulkan};
    std::string gpuName;
    std::uint32_t apiVersionMajor {0};
    std::uint32_t apiVersionMinor {0};
    std::uint32_t apiVersionPatch {0};
    bool validationEnabled {false};
    bool debugMarkersAvailable {false};
    bool vmaAllocatorReady {false};
    RenderTextureQuality textureQuality {RenderTextureQuality::High};
    bool samplerAnisotropySupported {false};
    bool samplerAnisotropyEnabled {false};
    float deviceMaxSamplerAnisotropy {1.0F};
    float activeMaxSamplerAnisotropy {1.0F};
    std::uint64_t viewportFramesPresented {0};
    std::uint64_t viewportSurfacePrepareCount {0};
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
    std::uint64_t lastFrameHlodMeshDrawCount {0};
    std::uint64_t lastFrameHlodCandidateDrawCount {0};
    std::uint64_t lastFrameHlodTriangleReductionCount {0};
    std::uint64_t lastFrameHlodRejectedNoOverviewCount {0};
    std::uint64_t lastFrameHlodRejectedVisibleWorkCount {0};
    std::uint64_t lastFrameHlodRejectedCoverageCount {0};
    std::uint64_t lastFrameHlodRejectedScreenCount {0};
    std::uint64_t lastFrameHlodRejectedClusterScreenCount {0};
    std::uint64_t lastFrameHlodCollapsedChunkCount {0};
    float lastFrameHlodScreenCoverage {0.0F};
    float lastFrameHlodCameraDistance {0.0F};
    std::string lastFrameHlodReason;
    std::uint64_t lastFrameOcclusionTestedChunkCount {0};
    std::uint64_t lastFrameOcclusionRejectedChunkCount {0};
    std::uint64_t lastFrameOcclusionOccluderChunkCount {0};
    std::uint64_t lastFrameOcclusionRejectedInstanceCount {0};
    std::uint64_t lastFrameOcclusionRejectedTriangleCount {0};
    std::uint64_t lastFrameSpatialCellCount {0};
    std::uint64_t lastFrameSpatialCellTestCount {0};
    std::uint64_t lastFrameSpatialCellRejectedCount {0};
    std::uint64_t lastFrameSpatialCellCandidateChunkCount {0};
    std::uint64_t lastFrameSceneNodeCount {0};
    std::uint64_t lastFrameRenderChunkCount {0};
    std::uint64_t lastFrameVisibleRenderChunkCount {0};
    std::uint64_t lastFrameRenderInstanceCount {0};
    std::uint64_t lastFrameVisibleRenderInstanceCount {0};
    std::uint64_t objectsConsidered {0};
    std::uint64_t passedFrustum {0};
    std::uint64_t visibleBatches {0};
    std::uint64_t resourcePrepared {0};
    std::uint64_t resourceDeferred {0};
    std::uint64_t resourceFallback {0};
    std::uint64_t resourceDeferredBytes {0};
    std::uint64_t shadowCastersSubmitted {0};
    std::uint64_t shadowCandidateInstances {0};
    std::uint64_t shadowPolicyRejectedInstances {0};
    std::uint64_t shadowVisibleInstances {0};
    std::uint64_t shadowOnlyCandidateInstances {0};
    std::uint64_t shadowOnlyRejectedInstances {0};
    std::uint64_t shadowHlodProxyDrawCount {0};
    std::uint64_t shadowCandidates {0};
    std::uint64_t shadowSubmitted {0};
    std::uint64_t shadowTriangles {0};
    std::uint64_t shadowRejectedByPolicy {0};
    std::uint64_t shadowRejectedByCasterCull {0};
    double shadowCpuMs {0.0};
    double shadowGpuMs {0.0};
    std::uint64_t shadowBatchesSubmitted {0};
    std::uint64_t shadowInstancesSubmitted {0};
    std::uint64_t shadowTrianglesSubmitted {0};
    RenderLodBreakdown renderWorldSelectedLod;
    RenderLodBreakdown resourcePreparedLod;
    RenderLodBreakdown vulkanBatchLod;
    RenderLodBreakdown vkDrawIndexedLod;
    RenderMaterialBreakdown mainMaterialDraws;
    RenderMaterialBreakdown shadowMaterialDraws;
    std::uint64_t vkBindVertex {0};
    std::uint64_t vkBindIndex {0};
    std::uint64_t vkBindDescriptors {0};
    std::uint64_t vkDrawIndexed {0};
    std::uint64_t trianglesSubmitted {0};
    std::uint64_t pipelineSwitches {0};
    std::uint64_t materialSwitches {0};
    std::uint64_t shadowPipelineSwitches {0};
    std::uint64_t shadowMaterialSwitches {0};
    double commandRecordingMs {0.0};
    double resourcePrepareMs {0.0};
    double FPS {0.0};
    std::uint64_t recentMaxFrameCpuTimeUs {0};
    std::uint64_t recentMaxFrameGpuTimeUs {0};
    std::uint64_t hitchFrameCount {0};
    std::uint64_t lastHitchFrameIndex {0};
    std::uint64_t lastHitchCpuTimeUs {0};
    std::uint64_t lastHitchGpuTimeUs {0};
    std::uint64_t lastHitchEditorBuildCpuTimeUs {0};
    std::uint64_t lastHitchRenderWorldBuildCpuTimeUs {0};
    std::uint64_t lastHitchResourcePrepareCpuTimeUs {0};
    std::uint64_t lastHitchCommandRecordCpuTimeUs {0};
    std::uint64_t lastHitchMeshDrawCount {0};
    std::uint64_t lastHitchVkDrawIndexed {0};
    std::uint64_t lastHitchMeshUploadBytes {0};
    std::uint64_t lastHitchTextureUploadBytes {0};
    std::uint64_t lastHitchStaticUploadBytes {0};
    std::uint64_t lastFrameLargeRenderChunkCount {0};
    std::uint64_t lastFrameLargestRenderChunkTriangleCount {0};
    std::uint64_t lastFrameLargestRenderChunkInstanceCount {0};
    float lastFrameMaxRenderChunkExtent {0.0F};
    std::uint64_t lastFrameRenderWorldDrawPacketCount {0};
    std::uint64_t lastFrameRenderWorldTriangleCount {0};
    std::uint64_t lastFrameFinalVisibleChunkCount {0};
    std::uint64_t lastFrameBudgetDegradedDrawCount {0};
    std::uint64_t lastFrameLightCount {0};
    std::uint64_t lastFrameShadowCasterCount {0};
    std::uint64_t lastFrameShadowViewCount {0};
    std::uint64_t lastFrameShadowBatchCount {0};
    std::uint64_t lastFrameShadowCulledBatchCount {0};
    RenderShadowUpdateMode lastFrameShadowUpdateMode {RenderShadowUpdateMode::Off};
    bool lastFrameShadowMapUpdated {false};
    std::uint64_t lastFrameRenderCpuTimeUs {0};
    std::uint64_t averageRenderCpuTimeUs {0};
    std::uint64_t lastFrameEditorBuildCpuTimeUs {0};
    std::uint64_t lastFrameRenderWorldBuildCpuTimeUs {0};
    std::uint64_t lastFrameRenderWorldRebuiltRecordCount {0};
    std::uint64_t lastFrameRenderWorldReusedRecordCount {0};
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
    std::uint64_t lastFrameMeshUploadBytes {0};
    std::uint64_t lastFrameTextureUploadBytes {0};
    std::uint64_t lastFrameStaticUploadBytes {0};
    std::uint64_t lastFrameColorUploadBytes {0};
    std::uint64_t residentMeshCount {0};
    std::uint64_t residentTextureCount {0};
    std::uint64_t textureSamplerCreateCount {0};
    std::uint64_t anisotropicTextureSamplerCount {0};
    std::uint64_t trilinearTextureSamplerCount {0};
    std::string lastFrameRenderPath;
    std::string lastFrameSamplerDebugLine;
    std::string lastFrameRuntimeSamplerDebugLine;
    std::string lastFrameTextureDebugLine;
    std::string lastTextureSamplerName;
    std::string lastTextureSamplerRole;
    std::uint32_t lastTextureSamplerWidth {0};
    std::uint32_t lastTextureSamplerHeight {0};
    std::uint32_t lastTextureSamplerMipLevels {0};
    bool lastTextureSamplerAnisotropyEnabled {false};
    float lastTextureSamplerMaxAnisotropy {1.0F};
    float lastTextureSamplerMipLodBias {0.0F};
    float lastTextureSamplerMinLod {0.0F};
    float lastTextureSamplerMaxLod {0.0F};
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
    bool castsShadow {true};
    std::uint64_t renderInstanceId {0};
    std::uint64_t renderChunkId {0};
    std::uint64_t sceneNodeId {0};
    std::array<float, 3> worldBoundsHalfExtent {0.0F, 0.0F, 0.0F};
    float distanceToCameraCenter {0.0F};
    float distanceToCameraBounds {0.0F};
    float projectedRadiusPixels {0.0F};
    std::array<float, 3> rootBoundsHalfExtent {0.0F, 0.0F, 0.0F};
    bool cameraInsideRootBounds {false};
    bool cameraInsideChunkBounds {false};
    std::uint32_t previousLodIndex {0};
    float projectedLodErrorPixels {0.0F};
    RenderLodSelectionReason lodSelectionReason {RenderLodSelectionReason::Unspecified};
    bool lodHysteresisActive {false};
    std::uint32_t materialIndex {UINT32_MAX};
    bool generatedTerrainModel {false};
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
    RenderFramePath renderPath {RenderFramePath::SceneView};
    RenderTextureDebugSettings textureDebug;
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
    std::uint64_t staticUploadBudgetBytes {24ULL * 1024ULL * 1024ULL};
    std::uint32_t staticUploadBatchBudget {12U};
    bool unlimitedStaticUploads {false};
    std::span<const RenderLight> lights;
    bool shadowsEnabled {false};
    RenderShadowUpdateMode shadowUpdateMode {RenderShadowUpdateMode::Off};
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
    std::uint64_t hlodMeshDrawCount {0};
    std::uint64_t hlodCandidateDrawCount {0};
    std::uint64_t hlodTriangleReductionCount {0};
    std::uint64_t hlodRejectedNoOverviewCount {0};
    std::uint64_t hlodRejectedVisibleWorkCount {0};
    std::uint64_t hlodRejectedCoverageCount {0};
    std::uint64_t hlodRejectedScreenCount {0};
    std::uint64_t hlodRejectedClusterScreenCount {0};
    std::uint64_t hlodCollapsedChunkCount {0};
    float hlodScreenCoverage {0.0F};
    float hlodCameraDistance {0.0F};
    std::uint64_t occlusionTestedChunkCount {0};
    std::uint64_t occlusionRejectedChunkCount {0};
    std::uint64_t occlusionOccluderChunkCount {0};
    std::uint64_t occlusionRejectedInstanceCount {0};
    std::uint64_t occlusionRejectedTriangleCount {0};
    std::uint64_t spatialCellCount {0};
    std::uint64_t spatialCellTestCount {0};
    std::uint64_t spatialCellRejectedCount {0};
    std::uint64_t spatialCellCandidateChunkCount {0};
    std::uint64_t sceneNodeCount {0};
    std::uint64_t renderChunkCount {0};
    std::uint64_t visibleRenderChunkCount {0};
    std::uint64_t renderInstanceCount {0};
    std::uint64_t visibleRenderInstanceCount {0};
    std::uint64_t largeRenderChunkCount {0};
    std::uint64_t largestRenderChunkTriangleCount {0};
    std::uint64_t largestRenderChunkInstanceCount {0};
    float maxRenderChunkExtent {0.0F};
    std::uint64_t editorBuildCpuTimeUs {0};
    std::uint64_t renderWorldBuildCpuTimeUs {0};
    std::uint64_t renderWorldRebuiltRecordCount {0};
    std::uint64_t renderWorldReusedRecordCount {0};
    std::uint64_t shadowCandidateInstances {0};
    std::uint64_t shadowPolicyRejectedInstances {0};
    std::uint64_t shadowVisibleInstances {0};
    std::uint64_t shadowOnlyCandidateInstances {0};
    std::uint64_t shadowOnlyRejectedInstances {0};
    std::uint64_t shadowHlodProxyDrawCount {0};
    std::uint64_t renderWorldDrawPacketCount {0};
    std::uint64_t renderWorldTriangleCount {0};
    std::uint64_t renderWorldFinalVisibleChunkCount {0};
    std::uint64_t renderWorldBudgetDegradedDrawCount {0};
    std::span<const RenderMeshDraw> shadowMeshDraws;
    std::span<const RenderMeshDraw> meshDraws;
    float meshDebugOpacity {1.0F};
    bool meshWireOverlayEnabled {false};
    bool selectedMeshWireOverlayEnabled {false};
    std::uint64_t selectedMeshWireOverlaySceneNodeId {0};
    std::span<const RenderColorMeshDraw> colorMeshDraws;
};

[[nodiscard]] inline bool isOverviewRenderMeshDraw(const RenderMeshDraw& draw) noexcept
{
    return draw.primitiveIndex >= kRenderOverviewPrimitiveIndexBase;
}

[[nodiscard]] inline std::uint64_t renderMeshDrawIndexCount(const RenderMeshDraw& draw) noexcept
{
    if (draw.primitive == nullptr) {
        return 0U;
    }
    if (draw.lodIndex > 0U && draw.lodIndex - 1U < draw.primitive->lods.size()) {
        const auto& lodIndices = draw.primitive->lods[draw.lodIndex - 1U].indices;
        if (!lodIndices.empty()) {
            return static_cast<std::uint64_t>(lodIndices.size());
        }
    }
    return static_cast<std::uint64_t>(draw.primitive->indices.size());
}

[[nodiscard]] inline std::uint64_t renderMeshDrawTriangleCount(const RenderMeshDraw& draw) noexcept
{
    return renderMeshDrawIndexCount(draw) / 3U;
}

[[nodiscard]] inline std::size_t renderMeshDrawLodCounterSlot(const RenderMeshDraw& draw) noexcept
{
    if (isOverviewRenderMeshDraw(draw)) {
        return 3U;
    }
    if (draw.lodIndex == 0U) {
        return 0U;
    }
    if (draw.lodIndex == 1U) {
        return 1U;
    }
    return 2U;
}

inline void accumulateRenderLodBreakdown(
    RenderLodBreakdown& breakdown,
    const RenderMeshDraw& draw,
    std::uint32_t instanceCount,
    std::uint64_t indexCount) noexcept
{
    const auto slot = renderMeshDrawLodCounterSlot(draw);
    if (slot >= kRenderLodCounterCount) {
        return;
    }
    const auto instances = static_cast<std::uint64_t>(std::max<std::uint32_t>(instanceCount, 1U));
    const auto triangles = (indexCount / 3U) * instances;
    ++breakdown.draws[slot];
    breakdown.triangles[slot] += triangles;
    breakdown.maxDrawTriangles[slot] = std::max(breakdown.maxDrawTriangles[slot], triangles);
}

inline void accumulateRenderMaterialBreakdown(
    RenderMaterialBreakdown& breakdown,
    const RenderMeshDraw& draw,
    std::uint32_t instanceCount,
    std::uint64_t indexCount) noexcept
{
    const auto triangles = (indexCount / 3U) * static_cast<std::uint64_t>(std::max<std::uint32_t>(instanceCount, 1U));
    switch (draw.material == nullptr ? assets::MaterialAlphaMode::Opaque : draw.material->alphaMode) {
    case assets::MaterialAlphaMode::Mask:
        ++breakdown.alphaMaskDraws;
        breakdown.alphaMaskTriangles += triangles;
        break;
    case assets::MaterialAlphaMode::Blend:
        ++breakdown.blendDraws;
        breakdown.blendTriangles += triangles;
        break;
    case assets::MaterialAlphaMode::Opaque:
        ++breakdown.opaqueDraws;
        breakdown.opaqueTriangles += triangles;
        break;
    }
    if (draw.material != nullptr && draw.material->doubleSided) {
        ++breakdown.doubleSidedDraws;
        breakdown.doubleSidedTriangles += triangles;
    }
}

} // namespace projectunity::renderer
