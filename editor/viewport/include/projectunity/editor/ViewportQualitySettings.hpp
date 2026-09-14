#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>

namespace projectunity::editor {

enum class ViewportHlodDebugOverride : std::uint8_t {
    Automatic,
    ForceDetailed,
    ForceHlod,
};

struct ViewportAssetLodSettings {
    float hlodScreenSizeThreshold {1.15F};
    float hlodDistanceThreshold {18.0F};
    float chunkCollapseDistance {24.0F};
    std::size_t maxVisibleChunksFromFar {48U};
    std::size_t maxDrawPackets {192U};
    std::size_t maxShadowCasters {192U};
    float shadowFocusRadius {220.0F};
    std::uint64_t maxDetailedTriangles {4'000'000ULL};
    std::uint64_t staticUploadBudgetBytes {24ULL * 1024ULL * 1024ULL};
    std::uint32_t staticUploadBatchBudget {12U};
    float lodBias {1.0F};
    float lodHysteresisRatio {0.15F};
    float hlodHysteresisRatio {0.15F};
    float cullingBoundsPadding {0.25F};
    float terrainNearHighQualityRadius {30.0F};
    bool terrainNearHighQualityEnabled {true};
    bool forceLod0 {false};
    bool frustumCullingEnabled {true};
    bool instanceCullingEnabled {true};
    bool occlusionCullingEnabled {true};
    bool spatialCellCullingEnabled {true};
    bool triangleBudgetEnabled {true};
    bool unlimitedStaticUploads {false};
    bool debugDisableTerrainHlod {false};
    bool debugDisableTerrainChunkLod {false};
    ViewportHlodDebugOverride debugOverride {ViewportHlodDebugOverride::Automatic};
};

namespace detail {

[[nodiscard]] inline std::optional<std::string> viewportEnvironmentValue(const char* name)
{
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t length = 0;
    if (::_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
        return std::nullopt;
    }
    std::string result(value);
    std::free(value);
    return result;
#else
    const auto* value = std::getenv(name);
    return value == nullptr ? std::nullopt : std::optional<std::string>(value);
#endif
}

[[nodiscard]] inline float viewportEnvironmentFloat(const char* name, float fallback)
{
    const auto value = viewportEnvironmentValue(name);
    if (!value.has_value()) {
        return fallback;
    }
    char* end = nullptr;
    const auto parsed = std::strtof(value->c_str(), &end);
    return end != value->c_str() && std::isfinite(parsed) ? parsed : fallback;
}

[[nodiscard]] inline std::uint64_t viewportEnvironmentUnsigned(const char* name, std::uint64_t fallback)
{
    const auto value = viewportEnvironmentValue(name);
    if (!value.has_value()) {
        return fallback;
    }
    char* end = nullptr;
    const auto parsed = std::strtoull(value->c_str(), &end, 10);
    return end != value->c_str() ? static_cast<std::uint64_t>(parsed) : fallback;
}

[[nodiscard]] inline std::optional<bool> viewportEnvironmentFlag(const char* name)
{
    const auto value = viewportEnvironmentValue(name);
    if (!value.has_value()) {
        return std::nullopt;
    }
    auto normalized = *value;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (normalized.empty() || normalized == "0" || normalized == "false" || normalized == "off" || normalized == "no") {
        return false;
    }
    return true;
}

} // namespace detail

[[nodiscard]] inline ViewportAssetLodSettings sanitizeViewportAssetLodSettings(ViewportAssetLodSettings settings) noexcept
{
    settings.hlodScreenSizeThreshold = std::clamp(settings.hlodScreenSizeThreshold, 0.05F, 4.0F);
    settings.hlodDistanceThreshold = std::clamp(settings.hlodDistanceThreshold, 0.0F, 1'000'000.0F);
    settings.chunkCollapseDistance = std::clamp(settings.chunkCollapseDistance, 0.0F, 1'000'000.0F);
    settings.maxVisibleChunksFromFar = std::clamp<std::size_t>(settings.maxVisibleChunksFromFar, 1U, 1'000'000U);
    settings.maxDrawPackets = std::clamp<std::size_t>(settings.maxDrawPackets, 1U, 1'000'000U);
    settings.maxShadowCasters = std::clamp<std::size_t>(settings.maxShadowCasters, 1U, 1'000'000U);
    settings.shadowFocusRadius = std::clamp(settings.shadowFocusRadius, 48.0F, 5000.0F);
    settings.maxDetailedTriangles = std::clamp<std::uint64_t>(settings.maxDetailedTriangles, 1'000ULL, 1'000'000'000ULL);
    settings.staticUploadBudgetBytes = std::clamp<std::uint64_t>(
        settings.staticUploadBudgetBytes,
        1ULL * 1024ULL * 1024ULL,
        16ULL * 1024ULL * 1024ULL * 1024ULL);
    settings.staticUploadBatchBudget = std::clamp<std::uint32_t>(settings.staticUploadBatchBudget, 1U, 100'000U);
    settings.lodBias = std::clamp(settings.lodBias, 0.25F, 8.0F);
    settings.lodHysteresisRatio = std::clamp(settings.lodHysteresisRatio, 0.0F, 0.45F);
    settings.hlodHysteresisRatio = std::clamp(settings.hlodHysteresisRatio, 0.0F, 0.45F);
    settings.cullingBoundsPadding = std::clamp(settings.cullingBoundsPadding, 0.0F, 1000.0F);
    settings.terrainNearHighQualityRadius = std::clamp(settings.terrainNearHighQualityRadius, 0.0F, 1'000'000.0F);
    return settings;
}

[[nodiscard]] inline ViewportAssetLodSettings viewportAssetLodSettingsFromEnvironment()
{
    ViewportAssetLodSettings settings;
    settings.hlodScreenSizeThreshold = detail::viewportEnvironmentFloat(
        "PROJECTUNITY_HLOD_SCREEN_SIZE_THRESHOLD",
        settings.hlodScreenSizeThreshold);
    settings.hlodDistanceThreshold = detail::viewportEnvironmentFloat(
        "PROJECTUNITY_HLOD_DISTANCE_THRESHOLD",
        settings.hlodDistanceThreshold);
    settings.chunkCollapseDistance = detail::viewportEnvironmentFloat(
        "PROJECTUNITY_HLOD_CHUNK_COLLAPSE_DISTANCE",
        settings.chunkCollapseDistance);
    settings.maxVisibleChunksFromFar = static_cast<std::size_t>(detail::viewportEnvironmentUnsigned(
        "PROJECTUNITY_HLOD_MAX_VISIBLE_CHUNKS",
        settings.maxVisibleChunksFromFar));
    settings.maxDrawPackets = static_cast<std::size_t>(detail::viewportEnvironmentUnsigned(
        "PROJECTUNITY_HLOD_MAX_DRAW_PACKETS",
        settings.maxDrawPackets));
    settings.maxShadowCasters = static_cast<std::size_t>(detail::viewportEnvironmentUnsigned(
        "PROJECTUNITY_HLOD_MAX_SHADOW_CASTERS",
        settings.maxShadowCasters));
    settings.shadowFocusRadius = detail::viewportEnvironmentFloat(
        "PROJECTUNITY_SHADOW_FOCUS_RADIUS",
        settings.shadowFocusRadius);
    settings.maxDetailedTriangles = detail::viewportEnvironmentUnsigned(
        "PROJECTUNITY_HLOD_MAX_DETAILED_TRIANGLES",
        settings.maxDetailedTriangles);
    settings.lodBias = detail::viewportEnvironmentFloat("PROJECTUNITY_HLOD_LOD_BIAS", settings.lodBias);
    settings.lodHysteresisRatio = detail::viewportEnvironmentFloat(
        "PROJECTUNITY_LOD_HYSTERESIS_RATIO",
        settings.lodHysteresisRatio);
    settings.hlodHysteresisRatio = detail::viewportEnvironmentFloat(
        "PROJECTUNITY_HLOD_HYSTERESIS_RATIO",
        settings.hlodHysteresisRatio);
    settings.cullingBoundsPadding = detail::viewportEnvironmentFloat(
        "PROJECTUNITY_CULLING_BOUNDS_PADDING",
        settings.cullingBoundsPadding);
    settings.terrainNearHighQualityRadius = detail::viewportEnvironmentFloat(
        "PROJECTUNITY_TERRAIN_NEAR_HIGH_QUALITY_RADIUS",
        settings.terrainNearHighQualityRadius);
    if (const auto forceNearLod0 = detail::viewportEnvironmentFlag("PROJECTUNITY_DEBUG_FORCE_TERRAIN_LOD0_NEAR")) {
        settings.terrainNearHighQualityEnabled = *forceNearLod0;
    }
    if (const auto disableOcclusionCulling = detail::viewportEnvironmentFlag("PROJECTUNITY_DEBUG_DISABLE_OCCLUSION_CULLING")) {
        settings.occlusionCullingEnabled = !*disableOcclusionCulling;
    }
    if (const auto disableSpatialCulling = detail::viewportEnvironmentFlag("PROJECTUNITY_DEBUG_DISABLE_SPATIAL_CELL_CULLING")) {
        settings.spatialCellCullingEnabled = !*disableSpatialCulling;
    }
    if (const auto disableTerrainHlod = detail::viewportEnvironmentFlag("PROJECTUNITY_DEBUG_DISABLE_TERRAIN_HLOD")) {
        settings.debugDisableTerrainHlod = *disableTerrainHlod;
    }
    if (const auto disableTerrainChunkLod = detail::viewportEnvironmentFlag("PROJECTUNITY_DEBUG_DISABLE_TERRAIN_CHUNK_LOD")) {
        settings.debugDisableTerrainChunkLod = *disableTerrainChunkLod;
    }
    if (const auto overrideValue = detail::viewportEnvironmentValue("PROJECTUNITY_HLOD_DEBUG_OVERRIDE")) {
        if (*overrideValue == "detailed" || *overrideValue == "DETAIL" || *overrideValue == "0") {
            settings.debugOverride = ViewportHlodDebugOverride::ForceDetailed;
        } else if (*overrideValue == "hlod" || *overrideValue == "HLOD" || *overrideValue == "1") {
            settings.debugOverride = ViewportHlodDebugOverride::ForceHlod;
        }
    }
    return sanitizeViewportAssetLodSettings(settings);
}

} // namespace projectunity::editor
