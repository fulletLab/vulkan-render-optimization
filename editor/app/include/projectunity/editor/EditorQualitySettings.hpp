#pragma once

#include <projectunity/editor/ViewportQualitySettings.hpp>
#include <projectunity/renderer/RendererTypes.hpp>

#include <cstdint>
#include <filesystem>
#include <string>

namespace projectunity::editor {

enum class QualityPreset : std::uint8_t {
    Low,
    Medium,
    High,
    Ultra,
    Custom,
};

enum class TextureFiltering : std::uint8_t {
    Bilinear,
    Trilinear,
    Anisotropic,
};

enum class ShadowQuality : std::uint8_t {
    Off,
    Low,
    Medium,
    High,
    Ultra,
};

enum class LodQuality : std::uint8_t {
    Performance,
    Balanced,
    Quality,
    Ultra,
};

enum class HlodAggressiveness : std::uint8_t {
    Low,
    Medium,
    High,
};

enum class TerrainQuality : std::uint8_t {
    Low,
    Medium,
    High,
    Ultra,
};

enum class PerformanceMode : std::uint8_t {
    Quality,
    Balanced,
    Performance,
};

struct GraphicsSettings {
    QualityPreset preset {QualityPreset::Custom};
    bool vsync {true};
    int fpsLimit {0};
    int resolutionScalePercent {100};
};

struct TextureSettings {
    renderer::RenderTextureQuality quality {renderer::RenderTextureQuality::High};
    int anisotropyLevel {8};
    TextureFiltering filtering {TextureFiltering::Anisotropic};
    bool mipmapsEnabled {true};
    float mipLodBias {0.0F};
};

struct ShadowSettings {
    ShadowQuality quality {ShadowQuality::Off};
    renderer::RenderShadowUpdateMode updateMode {renderer::RenderShadowUpdateMode::Off};
    int resolution {4096};
    int cascadeCount {4};
    float distance {120.0F};
    bool alphaMaskShadows {false};
    bool shadowHlodProxy {false};
};

struct LodSettings {
    bool automaticLod {true};
    LodQuality quality {LodQuality::Balanced};
    float lodDistance {18.0F};
    bool hlodEnabled {true};
    HlodAggressiveness aggressiveness {HlodAggressiveness::Medium};
    float screenError {1.15F};
    float hysteresis {0.15F};
    int chunkBudget {48};
    int drawPacketBudget {192};
    int shadowCasterBudget {192};
    float cullingBoundsPadding {0.25F};
    bool terrainNearHighQualityEnabled {true};
    float terrainNearHighQualityRadius {30.0F};
    bool occlusionCullingEnabled {true};
    bool spatialCellCullingEnabled {true};
    bool debugDisableTerrainHlod {false};
    bool debugDisableTerrainChunkLod {false};
    bool debugColors {false};
    ViewportHlodDebugOverride debugOverride {ViewportHlodDebugOverride::Automatic};
};

struct TerrainSettings {
    TerrainQuality quality {TerrainQuality::High};
    float terrainLodDistance {120.0F};
    bool generateNormals {true};
    bool generateTangents {true};
    bool terrainColliders {true};
};

struct PerformanceSettings {
    PerformanceMode mode {PerformanceMode::Balanced};
    bool fpsOverlay {false};
    bool profilerOverlay {false};
    bool advancedStats {false};
};

struct EditorUiSettings {
    std::string language {"es"};
    std::string theme {"dark"};
    std::string uiSize {"normal"};
    std::string iconSize {"normal"};
    bool showTooltips {true};
    bool showToolbarIcons {true};
    bool showToolbarText {true};
};

struct DebugSettings {
    bool wireframe {false};
    bool boundsXray {false};
    bool colliders {false};
    bool terrainBrush {false};
    bool lodColors {false};
    bool shadowCasters {false};
    bool sourceObjects {false};
    bool sunDirection {false};
    bool textureForceMaxLodZero {false};
    renderer::RenderTextureDebugAnisotropyOverride textureAnisotropyOverride {
        renderer::RenderTextureDebugAnisotropyOverride::Automatic};
    bool textureOverrideMipLodBias {false};
    float textureDebugMipLodBias {0.0F};
    bool normalsTangents {false};
    bool gpuProfiler {false};
};

struct EditorQualitySettings {
    GraphicsSettings graphics;
    TextureSettings texture;
    ShadowSettings shadow;
    LodSettings lod;
    TerrainSettings terrain;
    PerformanceSettings performance;
    EditorUiSettings editorUi;
    DebugSettings debug;
};

[[nodiscard]] std::filesystem::path projectGraphicsSettingsPath();
[[nodiscard]] EditorQualitySettings loadEditorQualitySettings(const std::filesystem::path& path);
[[nodiscard]] bool saveEditorQualitySettings(
    const EditorQualitySettings& settings,
    const std::filesystem::path& path,
    std::string* errorMessage = nullptr);

void applyQualityPreset(QualityPreset preset, EditorQualitySettings& settings);

[[nodiscard]] float requestedSamplerAnisotropy(const EditorQualitySettings& settings) noexcept;
[[nodiscard]] ViewportAssetLodSettings viewportAssetLodSettingsFromQuality(const EditorQualitySettings& settings);

[[nodiscard]] const char* qualityPresetName(QualityPreset preset) noexcept;
[[nodiscard]] const char* textureQualityName(renderer::RenderTextureQuality quality) noexcept;
[[nodiscard]] const char* shadowQualityName(ShadowQuality quality) noexcept;
[[nodiscard]] const char* lodQualityName(LodQuality quality) noexcept;
[[nodiscard]] const char* hlodAggressivenessName(HlodAggressiveness value) noexcept;
[[nodiscard]] const char* terrainQualityName(TerrainQuality quality) noexcept;
[[nodiscard]] const char* performanceModeName(PerformanceMode mode) noexcept;

} // namespace projectunity::editor
