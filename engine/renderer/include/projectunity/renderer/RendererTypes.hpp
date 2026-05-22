#pragma once

#include <projectunity/assets/AssetManager.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace projectunity::renderer {

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
    std::uint64_t lastFrameMeshDrawCount {0};
    std::uint64_t lastFrameColorMeshDrawCount {0};
    std::uint64_t meshDrawsPresented {0};
    std::uint64_t texturedMeshDrawsPresented {0};
    std::uint64_t colorMeshDrawsPresented {0};
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

struct RenderMeshDraw {
    assets::AssetId modelAssetId;
    std::uint32_t primitiveIndex {0};
    const assets::MeshPrimitive* primitive {nullptr};
    const assets::MaterialAsset* material {nullptr};
    const assets::TextureAsset* baseColorTexture {nullptr};
    const assets::TextureAsset* normalTexture {nullptr};
    const assets::TextureAsset* metallicRoughnessTexture {nullptr};
    const assets::TextureAsset* occlusionTexture {nullptr};
    float sortDepth {0.0F};
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
    std::span<const RenderMeshDraw> meshDraws;
    std::span<const RenderColorMeshDraw> colorMeshDraws;
};

} // namespace projectunity::renderer
