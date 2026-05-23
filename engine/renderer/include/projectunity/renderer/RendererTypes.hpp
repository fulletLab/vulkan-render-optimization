#pragma once

#include <projectunity/assets/AssetManager.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace projectunity::renderer {

constexpr std::size_t kMaxFrameLights = 8;

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
    std::uint64_t lastFrameColorMeshDrawCount {0};
    std::uint64_t lastFrameLightCount {0};
    std::uint64_t lastFrameShadowCasterCount {0};
    std::uint64_t lastFrameRenderCpuTimeUs {0};
    std::uint64_t averageRenderCpuTimeUs {0};
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
    const assets::MeshPrimitive* primitive {nullptr};
    const assets::MaterialAsset* material {nullptr};
    const assets::TextureAsset* baseColorTexture {nullptr};
    const assets::TextureAsset* normalTexture {nullptr};
    const assets::TextureAsset* metallicRoughnessTexture {nullptr};
    const assets::TextureAsset* occlusionTexture {nullptr};
    const assets::TextureAsset* emissiveTexture {nullptr};
    float sortDepth {0.0F};
    RenderMatrix4 modelMatrix;
    RenderMatrix4 modelViewProjection;
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
    std::array<float, 3> cameraPosition {0.0F, 0.0F, 0.0F};
    std::array<float, 3> visibleBoundsCenter {0.0F, 0.0F, 0.0F};
    float visibleBoundsRadius {0.0F};
    std::array<float, 3> ambientSkyColor {0.22F, 0.28F, 0.40F};
    std::array<float, 3> ambientGroundColor {0.07F, 0.06F, 0.05F};
    std::span<const RenderLight> lights;
    bool shadowsEnabled {false};
    std::uint32_t shadowLightIndex {0};
    std::uint64_t candidateMeshDrawCount {0};
    std::uint64_t culledMeshDrawCount {0};
    std::span<const RenderMeshDraw> meshDraws;
    std::span<const RenderColorMeshDraw> colorMeshDraws;
};

} // namespace projectunity::renderer
