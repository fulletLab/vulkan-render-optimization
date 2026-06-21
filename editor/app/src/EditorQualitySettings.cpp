#include <projectunity/editor/EditorQualitySettings.hpp>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QString>

#include <algorithm>
#include <cmath>
#include <string>

namespace projectunity::editor {
namespace {

[[nodiscard]] QString pathToQString(const std::filesystem::path& path)
{
    return QString::fromStdWString(path.wstring());
}

[[nodiscard]] int clampInt(int value, int minimum, int maximum) noexcept
{
    return std::clamp(value, minimum, maximum);
}

[[nodiscard]] float clampFloat(float value, float minimum, float maximum) noexcept
{
    return std::clamp(std::isfinite(value) ? value : minimum, minimum, maximum);
}

template <typename Enum>
[[nodiscard]] Enum enumFromString(const QJsonObject& object, const char* key, Enum fallback, const std::initializer_list<std::pair<const char*, Enum>>& values)
{
    const auto text = object.value(QString::fromLatin1(key)).toString();
    for (const auto& [name, value] : values) {
        if (text == QString::fromLatin1(name)) {
            return value;
        }
    }
    return fallback;
}

[[nodiscard]] int intFromJson(const QJsonObject& object, const char* key, int fallback, int minimum, int maximum)
{
    return clampInt(object.value(QString::fromLatin1(key)).toInt(fallback), minimum, maximum);
}

[[nodiscard]] float floatFromJson(const QJsonObject& object, const char* key, float fallback, float minimum, float maximum)
{
    return clampFloat(static_cast<float>(object.value(QString::fromLatin1(key)).toDouble(fallback)), minimum, maximum);
}

[[nodiscard]] bool boolFromJson(const QJsonObject& object, const char* key, bool fallback)
{
    const auto value = object.value(QString::fromLatin1(key));
    return value.isBool() ? value.toBool() : fallback;
}

[[nodiscard]] QString stringFromJson(const QJsonObject& object, const char* key, QString fallback)
{
    const auto value = object.value(QString::fromLatin1(key));
    return value.isString() ? value.toString() : fallback;
}

[[nodiscard]] QJsonObject jsonObject(const QJsonObject& object, const char* key)
{
    const auto value = object.value(QString::fromLatin1(key));
    return value.isObject() ? value.toObject() : QJsonObject {};
}

[[nodiscard]] renderer::RenderShadowUpdateMode shadowUpdateModeFromString(const QString& text, renderer::RenderShadowUpdateMode fallback)
{
    if (text == QStringLiteral("live")) {
        return renderer::RenderShadowUpdateMode::Live;
    }
    if (text == QStringLiteral("frozen")) {
        return renderer::RenderShadowUpdateMode::Frozen;
    }
    if (text == QStringLiteral("off")) {
        return renderer::RenderShadowUpdateMode::Off;
    }
    return fallback;
}

[[nodiscard]] const char* shadowUpdateModeName(renderer::RenderShadowUpdateMode mode) noexcept
{
    switch (mode) {
    case renderer::RenderShadowUpdateMode::Live: return "live";
    case renderer::RenderShadowUpdateMode::Frozen: return "frozen";
    case renderer::RenderShadowUpdateMode::Off: return "off";
    }
    return "off";
}

[[nodiscard]] ViewportHlodDebugOverride hlodDebugOverrideFromString(const QString& text, ViewportHlodDebugOverride fallback)
{
    if (text == QStringLiteral("detailed")) {
        return ViewportHlodDebugOverride::ForceDetailed;
    }
    if (text == QStringLiteral("hlod")) {
        return ViewportHlodDebugOverride::ForceHlod;
    }
    if (text == QStringLiteral("automatic")) {
        return ViewportHlodDebugOverride::Automatic;
    }
    return fallback;
}

[[nodiscard]] const char* hlodDebugOverrideName(ViewportHlodDebugOverride value) noexcept
{
    switch (value) {
    case ViewportHlodDebugOverride::Automatic: return "automatic";
    case ViewportHlodDebugOverride::ForceDetailed: return "detailed";
    case ViewportHlodDebugOverride::ForceHlod: return "hlod";
    }
    return "automatic";
}

[[nodiscard]] renderer::RenderTextureDebugAnisotropyOverride textureAnisotropyOverrideFromString(
    const QString& text,
    renderer::RenderTextureDebugAnisotropyOverride fallback)
{
    if (text == QStringLiteral("force-off")) {
        return renderer::RenderTextureDebugAnisotropyOverride::ForceOff;
    }
    if (text == QStringLiteral("force-on")) {
        return renderer::RenderTextureDebugAnisotropyOverride::ForceOn;
    }
    if (text == QStringLiteral("automatic")) {
        return renderer::RenderTextureDebugAnisotropyOverride::Automatic;
    }
    return fallback;
}

[[nodiscard]] const char* textureAnisotropyOverrideName(
    renderer::RenderTextureDebugAnisotropyOverride value) noexcept
{
    switch (value) {
    case renderer::RenderTextureDebugAnisotropyOverride::Automatic: return "automatic";
    case renderer::RenderTextureDebugAnisotropyOverride::ForceOff: return "force-off";
    case renderer::RenderTextureDebugAnisotropyOverride::ForceOn: return "force-on";
    }
    return "automatic";
}

[[nodiscard]] QJsonObject graphicsToJson(const GraphicsSettings& settings)
{
    return {
        {"preset", QString::fromLatin1(qualityPresetName(settings.preset))},
        {"vsync", settings.vsync},
        {"fpsLimit", settings.fpsLimit},
        {"resolutionScalePercent", settings.resolutionScalePercent},
    };
}

[[nodiscard]] QJsonObject textureToJson(const TextureSettings& settings)
{
    return {
        {"quality", QString::fromLatin1(textureQualityName(settings.quality))},
        {"anisotropyLevel", settings.anisotropyLevel},
        {"filtering", settings.filtering == TextureFiltering::Bilinear ? QStringLiteral("bilinear")
            : settings.filtering == TextureFiltering::Trilinear ? QStringLiteral("trilinear") : QStringLiteral("anisotropic")},
        {"mipmapsEnabled", settings.mipmapsEnabled},
        {"mipLodBias", settings.mipLodBias},
    };
}

[[nodiscard]] QJsonObject shadowToJson(const ShadowSettings& settings)
{
    return {
        {"quality", QString::fromLatin1(shadowQualityName(settings.quality))},
        {"updateMode", QString::fromLatin1(shadowUpdateModeName(settings.updateMode))},
        {"resolution", settings.resolution},
        {"cascadeCount", settings.cascadeCount},
        {"distance", settings.distance},
        {"alphaMaskShadows", settings.alphaMaskShadows},
        {"shadowHlodProxy", settings.shadowHlodProxy},
    };
}

[[nodiscard]] QJsonObject lodToJson(const LodSettings& settings)
{
    return {
        {"automaticLod", settings.automaticLod},
        {"quality", QString::fromLatin1(lodQualityName(settings.quality))},
        {"lodDistance", settings.lodDistance},
        {"hlodEnabled", settings.hlodEnabled},
        {"aggressiveness", QString::fromLatin1(hlodAggressivenessName(settings.aggressiveness))},
        {"screenError", settings.screenError},
        {"hysteresis", settings.hysteresis},
        {"chunkBudget", settings.chunkBudget},
        {"drawPacketBudget", settings.drawPacketBudget},
        {"shadowCasterBudget", settings.shadowCasterBudget},
        {"cullingBoundsPadding", settings.cullingBoundsPadding},
        {"terrainNearHighQualityEnabled", settings.terrainNearHighQualityEnabled},
        {"terrainNearHighQualityRadius", settings.terrainNearHighQualityRadius},
        {"occlusionCullingEnabled", settings.occlusionCullingEnabled},
        {"spatialCellCullingEnabled", settings.spatialCellCullingEnabled},
        {"debugDisableTerrainHlod", settings.debugDisableTerrainHlod},
        {"debugDisableTerrainChunkLod", settings.debugDisableTerrainChunkLod},
        {"debugColors", settings.debugColors},
        {"debugOverride", QString::fromLatin1(hlodDebugOverrideName(settings.debugOverride))},
    };
}

[[nodiscard]] QJsonObject terrainToJson(const TerrainSettings& settings)
{
    return {
        {"quality", QString::fromLatin1(terrainQualityName(settings.quality))},
        {"terrainLodDistance", settings.terrainLodDistance},
        {"generateNormals", settings.generateNormals},
        {"generateTangents", settings.generateTangents},
        {"terrainColliders", settings.terrainColliders},
    };
}

[[nodiscard]] QJsonObject performanceToJson(const PerformanceSettings& settings)
{
    return {
        {"mode", QString::fromLatin1(performanceModeName(settings.mode))},
        {"fpsOverlay", settings.fpsOverlay},
        {"profilerOverlay", settings.profilerOverlay},
        {"advancedStats", settings.advancedStats},
    };
}

[[nodiscard]] QJsonObject debugToJson(const DebugSettings& settings)
{
    return {
        {"wireframe", settings.wireframe},
        {"boundsXray", settings.boundsXray},
        {"colliders", settings.colliders},
        {"terrainBrush", settings.terrainBrush},
        {"lodColors", settings.lodColors},
        {"shadowCasters", settings.shadowCasters},
        {"sourceObjects", settings.sourceObjects},
        {"sunDirection", settings.sunDirection},
        {"textureForceMaxLodZero", settings.textureForceMaxLodZero},
        {"textureAnisotropyOverride", QString::fromLatin1(textureAnisotropyOverrideName(settings.textureAnisotropyOverride))},
        {"textureOverrideMipLodBias", settings.textureOverrideMipLodBias},
        {"textureDebugMipLodBias", settings.textureDebugMipLodBias},
        {"normalsTangents", settings.normalsTangents},
        {"gpuProfiler", settings.gpuProfiler},
    };
}

void loadProjectJson(EditorQualitySettings& settings, const QJsonObject& root)
{
    const auto graphics = jsonObject(root, "graphics");
    settings.graphics.preset = enumFromString(graphics, "preset", settings.graphics.preset, {
        {"low", QualityPreset::Low},
        {"medium", QualityPreset::Medium},
        {"high", QualityPreset::High},
        {"ultra", QualityPreset::Ultra},
        {"custom", QualityPreset::Custom},
    });
    settings.graphics.vsync = boolFromJson(graphics, "vsync", settings.graphics.vsync);
    settings.graphics.fpsLimit = intFromJson(graphics, "fpsLimit", settings.graphics.fpsLimit, 0, 1000);
    settings.graphics.resolutionScalePercent = intFromJson(
        graphics,
        "resolutionScalePercent",
        settings.graphics.resolutionScalePercent,
        50,
        200);

    const auto texture = jsonObject(root, "texture");
    settings.texture.quality = enumFromString(texture, "quality", settings.texture.quality, {
        {"low", renderer::RenderTextureQuality::Low},
        {"medium", renderer::RenderTextureQuality::Medium},
        {"high", renderer::RenderTextureQuality::High},
        {"ultra", renderer::RenderTextureQuality::Ultra},
    });
    settings.texture.anisotropyLevel = intFromJson(texture, "anisotropyLevel", settings.texture.anisotropyLevel, -1, 16);
    settings.texture.filtering = enumFromString(texture, "filtering", settings.texture.filtering, {
        {"bilinear", TextureFiltering::Bilinear},
        {"trilinear", TextureFiltering::Trilinear},
        {"anisotropic", TextureFiltering::Anisotropic},
    });
    settings.texture.mipmapsEnabled = boolFromJson(texture, "mipmapsEnabled", settings.texture.mipmapsEnabled);
    settings.texture.mipLodBias = floatFromJson(texture, "mipLodBias", settings.texture.mipLodBias, -1.0F, 1.0F);

    const auto shadow = jsonObject(root, "shadow");
    settings.shadow.quality = enumFromString(shadow, "quality", settings.shadow.quality, {
        {"off", ShadowQuality::Off},
        {"low", ShadowQuality::Low},
        {"medium", ShadowQuality::Medium},
        {"high", ShadowQuality::High},
        {"ultra", ShadowQuality::Ultra},
    });
    settings.shadow.updateMode = shadowUpdateModeFromString(
        stringFromJson(shadow, "updateMode", QString::fromLatin1(shadowUpdateModeName(settings.shadow.updateMode))),
        settings.shadow.updateMode);
    settings.shadow.resolution = intFromJson(shadow, "resolution", settings.shadow.resolution, 512, 8192);
    settings.shadow.cascadeCount = intFromJson(shadow, "cascadeCount", settings.shadow.cascadeCount, 1, 4);
    settings.shadow.distance = floatFromJson(shadow, "distance", settings.shadow.distance, 0.0F, 100000.0F);
    settings.shadow.alphaMaskShadows = boolFromJson(shadow, "alphaMaskShadows", settings.shadow.alphaMaskShadows);
    settings.shadow.shadowHlodProxy = boolFromJson(shadow, "shadowHlodProxy", settings.shadow.shadowHlodProxy);

    const auto lod = jsonObject(root, "lod");
    settings.lod.automaticLod = boolFromJson(lod, "automaticLod", settings.lod.automaticLod);
    settings.lod.quality = enumFromString(lod, "quality", settings.lod.quality, {
        {"performance", LodQuality::Performance},
        {"balanced", LodQuality::Balanced},
        {"quality", LodQuality::Quality},
        {"ultra", LodQuality::Ultra},
    });
    settings.lod.lodDistance = floatFromJson(lod, "lodDistance", settings.lod.lodDistance, 0.0F, 100000.0F);
    settings.lod.hlodEnabled = boolFromJson(lod, "hlodEnabled", settings.lod.hlodEnabled);
    settings.lod.aggressiveness = enumFromString(lod, "aggressiveness", settings.lod.aggressiveness, {
        {"low", HlodAggressiveness::Low},
        {"medium", HlodAggressiveness::Medium},
        {"high", HlodAggressiveness::High},
    });
    settings.lod.screenError = floatFromJson(lod, "screenError", settings.lod.screenError, 0.05F, 4.0F);
    settings.lod.hysteresis = floatFromJson(lod, "hysteresis", settings.lod.hysteresis, 0.0F, 0.45F);
    settings.lod.chunkBudget = intFromJson(lod, "chunkBudget", settings.lod.chunkBudget, 1, 1000000);
    settings.lod.drawPacketBudget = intFromJson(lod, "drawPacketBudget", settings.lod.drawPacketBudget, 1, 1000000);
    settings.lod.shadowCasterBudget = intFromJson(lod, "shadowCasterBudget", settings.lod.shadowCasterBudget, 1, 1000000);
    settings.lod.cullingBoundsPadding = floatFromJson(
        lod,
        "cullingBoundsPadding",
        settings.lod.cullingBoundsPadding,
        0.0F,
        1000.0F);
    settings.lod.terrainNearHighQualityEnabled = boolFromJson(
        lod,
        "terrainNearHighQualityEnabled",
        settings.lod.terrainNearHighQualityEnabled);
    settings.lod.terrainNearHighQualityRadius = floatFromJson(
        lod,
        "terrainNearHighQualityRadius",
        settings.lod.terrainNearHighQualityRadius,
        0.0F,
        1000000.0F);
    settings.lod.occlusionCullingEnabled = boolFromJson(
        lod,
        "occlusionCullingEnabled",
        settings.lod.occlusionCullingEnabled);
    settings.lod.spatialCellCullingEnabled = boolFromJson(
        lod,
        "spatialCellCullingEnabled",
        settings.lod.spatialCellCullingEnabled);
    settings.lod.debugDisableTerrainHlod = boolFromJson(lod, "debugDisableTerrainHlod", settings.lod.debugDisableTerrainHlod);
    settings.lod.debugDisableTerrainChunkLod = boolFromJson(lod, "debugDisableTerrainChunkLod", settings.lod.debugDisableTerrainChunkLod);
    settings.lod.debugColors = boolFromJson(lod, "debugColors", settings.lod.debugColors);
    settings.lod.debugOverride = hlodDebugOverrideFromString(
        stringFromJson(lod, "debugOverride", QString::fromLatin1(hlodDebugOverrideName(settings.lod.debugOverride))),
        settings.lod.debugOverride);

    const auto terrain = jsonObject(root, "terrain");
    settings.terrain.quality = enumFromString(terrain, "quality", settings.terrain.quality, {
        {"low", TerrainQuality::Low},
        {"medium", TerrainQuality::Medium},
        {"high", TerrainQuality::High},
        {"ultra", TerrainQuality::Ultra},
    });
    settings.terrain.terrainLodDistance = floatFromJson(terrain, "terrainLodDistance", settings.terrain.terrainLodDistance, 0.0F, 100000.0F);
    settings.terrain.generateNormals = boolFromJson(terrain, "generateNormals", settings.terrain.generateNormals);
    settings.terrain.generateTangents = boolFromJson(terrain, "generateTangents", settings.terrain.generateTangents);
    settings.terrain.terrainColliders = boolFromJson(terrain, "terrainColliders", settings.terrain.terrainColliders);

    const auto performance = jsonObject(root, "performance");
    settings.performance.mode = enumFromString(performance, "mode", settings.performance.mode, {
        {"quality", PerformanceMode::Quality},
        {"balanced", PerformanceMode::Balanced},
        {"performance", PerformanceMode::Performance},
    });
    settings.performance.fpsOverlay = boolFromJson(performance, "fpsOverlay", settings.performance.fpsOverlay);
    settings.performance.profilerOverlay = boolFromJson(performance, "profilerOverlay", settings.performance.profilerOverlay);
    settings.performance.advancedStats = boolFromJson(performance, "advancedStats", settings.performance.advancedStats);

    const auto debug = jsonObject(root, "debug");
    settings.debug.wireframe = boolFromJson(debug, "wireframe", settings.debug.wireframe);
    settings.debug.boundsXray = boolFromJson(debug, "boundsXray", settings.debug.boundsXray);
    settings.debug.colliders = boolFromJson(debug, "colliders", settings.debug.colliders);
    settings.debug.terrainBrush = boolFromJson(debug, "terrainBrush", settings.debug.terrainBrush);
    settings.debug.lodColors = boolFromJson(debug, "lodColors", settings.debug.lodColors);
    settings.debug.shadowCasters = boolFromJson(debug, "shadowCasters", settings.debug.shadowCasters);
    settings.debug.sourceObjects = boolFromJson(debug, "sourceObjects", settings.debug.sourceObjects);
    settings.debug.sunDirection = boolFromJson(debug, "sunDirection", settings.debug.sunDirection);
    settings.debug.textureForceMaxLodZero = boolFromJson(debug, "textureForceMaxLodZero", settings.debug.textureForceMaxLodZero);
    settings.debug.textureAnisotropyOverride = textureAnisotropyOverrideFromString(
        stringFromJson(
            debug,
            "textureAnisotropyOverride",
            QString::fromLatin1(textureAnisotropyOverrideName(settings.debug.textureAnisotropyOverride))),
        settings.debug.textureAnisotropyOverride);
    settings.debug.textureOverrideMipLodBias = boolFromJson(
        debug,
        "textureOverrideMipLodBias",
        settings.debug.textureOverrideMipLodBias);
    settings.debug.textureDebugMipLodBias = floatFromJson(
        debug,
        "textureDebugMipLodBias",
        settings.debug.textureDebugMipLodBias,
        -1.0F,
        1.0F);
    settings.debug.normalsTangents = boolFromJson(debug, "normalsTangents", settings.debug.normalsTangents);
    settings.debug.gpuProfiler = boolFromJson(debug, "gpuProfiler", settings.debug.gpuProfiler);
}

void loadEditorUiSettings(EditorQualitySettings& settings)
{
    QSettings qsettings;
    qsettings.beginGroup(QStringLiteral("editor/settings"));
    settings.editorUi.language = qsettings.value(QStringLiteral("language"), QString::fromStdString(settings.editorUi.language)).toString().toStdString();
    settings.editorUi.theme = qsettings.value(QStringLiteral("theme"), QString::fromStdString(settings.editorUi.theme)).toString().toStdString();
    settings.editorUi.uiSize = qsettings.value(QStringLiteral("uiSize"), QString::fromStdString(settings.editorUi.uiSize)).toString().toStdString();
    settings.editorUi.iconSize = qsettings.value(QStringLiteral("iconSize"), QString::fromStdString(settings.editorUi.iconSize)).toString().toStdString();
    settings.editorUi.showTooltips = qsettings.value(QStringLiteral("showTooltips"), settings.editorUi.showTooltips).toBool();
    settings.editorUi.showToolbarIcons = qsettings.value(QStringLiteral("showToolbarIcons"), settings.editorUi.showToolbarIcons).toBool();
    settings.editorUi.showToolbarText = qsettings.value(QStringLiteral("showToolbarText"), settings.editorUi.showToolbarText).toBool();
    qsettings.endGroup();
}

void saveEditorUiSettings(const EditorUiSettings& settings)
{
    QSettings qsettings;
    qsettings.beginGroup(QStringLiteral("editor/settings"));
    qsettings.setValue(QStringLiteral("language"), QString::fromStdString(settings.language));
    qsettings.setValue(QStringLiteral("theme"), QString::fromStdString(settings.theme));
    qsettings.setValue(QStringLiteral("uiSize"), QString::fromStdString(settings.uiSize));
    qsettings.setValue(QStringLiteral("iconSize"), QString::fromStdString(settings.iconSize));
    qsettings.setValue(QStringLiteral("showTooltips"), settings.showTooltips);
    qsettings.setValue(QStringLiteral("showToolbarIcons"), settings.showToolbarIcons);
    qsettings.setValue(QStringLiteral("showToolbarText"), settings.showToolbarText);
    qsettings.endGroup();
}

} // namespace

std::filesystem::path projectGraphicsSettingsPath()
{
    return std::filesystem::path(PROJECTUNITY_SOURCE_DIR)
        / "Project"
        / "ProjectSettings"
        / "graphics_settings.json";
}

EditorQualitySettings loadEditorQualitySettings(const std::filesystem::path& path)
{
    EditorQualitySettings settings;
    const auto envLod = viewportAssetLodSettingsFromEnvironment();
    settings.lod.lodDistance = envLod.hlodDistanceThreshold;
    settings.lod.screenError = envLod.hlodScreenSizeThreshold;
    settings.lod.hysteresis = envLod.lodHysteresisRatio;
    settings.lod.chunkBudget = static_cast<int>(std::min<std::size_t>(envLod.maxVisibleChunksFromFar, 1000000U));
    settings.lod.drawPacketBudget = static_cast<int>(std::min<std::size_t>(envLod.maxDrawPackets, 1000000U));
    settings.lod.shadowCasterBudget = static_cast<int>(std::min<std::size_t>(envLod.maxShadowCasters, 1000000U));
    settings.lod.cullingBoundsPadding = envLod.cullingBoundsPadding;
    settings.lod.terrainNearHighQualityEnabled = envLod.terrainNearHighQualityEnabled;
    settings.lod.terrainNearHighQualityRadius = envLod.terrainNearHighQualityRadius;
    settings.lod.occlusionCullingEnabled = envLod.occlusionCullingEnabled;
    settings.lod.spatialCellCullingEnabled = envLod.spatialCellCullingEnabled;
    settings.lod.debugDisableTerrainHlod = envLod.debugDisableTerrainHlod;
    settings.lod.debugDisableTerrainChunkLod = envLod.debugDisableTerrainChunkLod;
    settings.lod.debugOverride = envLod.debugOverride;

    QFile file(pathToQString(path));
    if (file.open(QIODevice::ReadOnly)) {
        const auto document = QJsonDocument::fromJson(file.readAll());
        if (document.isObject()) {
            loadProjectJson(settings, document.object());
        }
    }
    loadEditorUiSettings(settings);
    return settings;
}

bool saveEditorQualitySettings(
    const EditorQualitySettings& settings,
    const std::filesystem::path& path,
    std::string* errorMessage)
{
    QDir directory(pathToQString(path.parent_path()));
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        if (errorMessage != nullptr) {
            *errorMessage = "Could not create ProjectSettings directory";
        }
        return false;
    }

    QJsonObject root;
    root["graphics"] = graphicsToJson(settings.graphics);
    root["texture"] = textureToJson(settings.texture);
    root["shadow"] = shadowToJson(settings.shadow);
    root["lod"] = lodToJson(settings.lod);
    root["terrain"] = terrainToJson(settings.terrain);
    root["performance"] = performanceToJson(settings.performance);
    root["debug"] = debugToJson(settings.debug);

    QFile file(pathToQString(path));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage != nullptr) {
            *errorMessage = "Could not write graphics_settings.json";
        }
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    saveEditorUiSettings(settings.editorUi);
    return true;
}

void applyQualityPreset(QualityPreset preset, EditorQualitySettings& settings)
{
    settings.graphics.preset = preset;
    if (preset == QualityPreset::Custom) {
        return;
    }

    settings.shadow.updateMode = renderer::RenderShadowUpdateMode::Live;
    settings.lod.automaticLod = true;
    settings.lod.hlodEnabled = true;
    settings.lod.debugOverride = ViewportHlodDebugOverride::Automatic;
    settings.lod.cullingBoundsPadding = 0.25F;
    settings.lod.occlusionCullingEnabled = true;
    settings.lod.spatialCellCullingEnabled = true;
    settings.lod.terrainNearHighQualityEnabled = true;
    settings.lod.debugDisableTerrainHlod = false;
    settings.lod.debugDisableTerrainChunkLod = false;

    switch (preset) {
    case QualityPreset::Low:
        settings.texture.quality = renderer::RenderTextureQuality::Low;
        settings.texture.anisotropyLevel = 2;
        settings.shadow.quality = ShadowQuality::Low;
        settings.lod.quality = LodQuality::Performance;
        settings.lod.aggressiveness = HlodAggressiveness::High;
        settings.lod.lodDistance = 12.0F;
        settings.lod.screenError = 1.65F;
        settings.lod.chunkBudget = 32;
        settings.lod.drawPacketBudget = 96;
        settings.terrain.quality = TerrainQuality::Low;
        settings.performance.mode = PerformanceMode::Performance;
        break;
    case QualityPreset::Medium:
        settings.texture.quality = renderer::RenderTextureQuality::Medium;
        settings.texture.anisotropyLevel = 4;
        settings.shadow.quality = ShadowQuality::Medium;
        settings.lod.quality = LodQuality::Balanced;
        settings.lod.aggressiveness = HlodAggressiveness::Medium;
        settings.lod.lodDistance = 18.0F;
        settings.lod.screenError = 1.15F;
        settings.lod.chunkBudget = 48;
        settings.lod.drawPacketBudget = 192;
        settings.terrain.quality = TerrainQuality::Medium;
        settings.performance.mode = PerformanceMode::Balanced;
        break;
    case QualityPreset::High:
        settings.texture.quality = renderer::RenderTextureQuality::High;
        settings.texture.anisotropyLevel = 8;
        settings.shadow.quality = ShadowQuality::High;
        settings.lod.quality = LodQuality::Quality;
        settings.lod.aggressiveness = HlodAggressiveness::Medium;
        settings.lod.lodDistance = 24.0F;
        settings.lod.screenError = 0.9F;
        settings.lod.chunkBudget = 96;
        settings.lod.drawPacketBudget = 384;
        settings.terrain.quality = TerrainQuality::High;
        settings.performance.mode = PerformanceMode::Quality;
        break;
    case QualityPreset::Ultra:
        settings.texture.quality = renderer::RenderTextureQuality::Ultra;
        settings.texture.anisotropyLevel = 16;
        settings.shadow.quality = ShadowQuality::Ultra;
        settings.lod.quality = LodQuality::Ultra;
        settings.lod.aggressiveness = HlodAggressiveness::Low;
        settings.lod.lodDistance = 36.0F;
        settings.lod.screenError = 0.65F;
        settings.lod.chunkBudget = 160;
        settings.lod.drawPacketBudget = 640;
        settings.terrain.quality = TerrainQuality::Ultra;
        settings.performance.mode = PerformanceMode::Quality;
        break;
    case QualityPreset::Custom:
        break;
    }
}

float requestedSamplerAnisotropy(const EditorQualitySettings& settings) noexcept
{
    if (settings.texture.anisotropyLevel < 0) {
        return 64.0F;
    }
    return settings.texture.anisotropyLevel <= 1 ? 1.0F : static_cast<float>(settings.texture.anisotropyLevel);
}

ViewportAssetLodSettings viewportAssetLodSettingsFromQuality(const EditorQualitySettings& settings)
{
    ViewportAssetLodSettings result;
    result.hlodScreenSizeThreshold = settings.lod.screenError;
    result.hlodDistanceThreshold = settings.lod.lodDistance;
    result.chunkCollapseDistance = std::max(settings.lod.lodDistance * 1.35F, settings.lod.lodDistance + 1.0F);
    result.maxVisibleChunksFromFar = static_cast<std::size_t>(std::max(settings.lod.chunkBudget, 1));
    result.maxDrawPackets = static_cast<std::size_t>(std::max(settings.lod.drawPacketBudget, 1));
    result.maxShadowCasters = static_cast<std::size_t>(std::max(settings.lod.shadowCasterBudget, 1));
    result.lodHysteresisRatio = settings.lod.hysteresis;
    result.hlodHysteresisRatio = settings.lod.hysteresis;
    result.cullingBoundsPadding = settings.lod.cullingBoundsPadding;
    result.terrainNearHighQualityEnabled = settings.lod.terrainNearHighQualityEnabled;
    result.terrainNearHighQualityRadius = settings.lod.terrainNearHighQualityRadius;
    result.occlusionCullingEnabled = settings.lod.occlusionCullingEnabled;
    result.spatialCellCullingEnabled = settings.lod.spatialCellCullingEnabled;
    result.debugDisableTerrainHlod = settings.lod.debugDisableTerrainHlod;
    result.debugDisableTerrainChunkLod = settings.lod.debugDisableTerrainChunkLod;

    switch (settings.lod.quality) {
    case LodQuality::Performance: result.lodBias = 1.8F; break;
    case LodQuality::Balanced: result.lodBias = 1.0F; break;
    case LodQuality::Quality: result.lodBias = 0.75F; break;
    case LodQuality::Ultra: result.lodBias = 0.55F; break;
    }

    switch (settings.lod.aggressiveness) {
    case HlodAggressiveness::Low:
        result.hlodScreenSizeThreshold *= 0.75F;
        result.hlodDistanceThreshold *= 1.35F;
        break;
    case HlodAggressiveness::Medium:
        break;
    case HlodAggressiveness::High:
        result.hlodScreenSizeThreshold *= 1.35F;
        result.hlodDistanceThreshold *= 0.75F;
        break;
    }

    result.debugOverride = settings.lod.hlodEnabled
        ? settings.lod.debugOverride
        : ViewportHlodDebugOverride::ForceDetailed;
    return sanitizeViewportAssetLodSettings(result);
}

const char* qualityPresetName(QualityPreset preset) noexcept
{
    switch (preset) {
    case QualityPreset::Low: return "low";
    case QualityPreset::Medium: return "medium";
    case QualityPreset::High: return "high";
    case QualityPreset::Ultra: return "ultra";
    case QualityPreset::Custom: return "custom";
    }
    return "custom";
}

const char* textureQualityName(renderer::RenderTextureQuality quality) noexcept
{
    switch (quality) {
    case renderer::RenderTextureQuality::Low: return "low";
    case renderer::RenderTextureQuality::Medium: return "medium";
    case renderer::RenderTextureQuality::High: return "high";
    case renderer::RenderTextureQuality::Ultra: return "ultra";
    }
    return "high";
}

const char* shadowQualityName(ShadowQuality quality) noexcept
{
    switch (quality) {
    case ShadowQuality::Off: return "off";
    case ShadowQuality::Low: return "low";
    case ShadowQuality::Medium: return "medium";
    case ShadowQuality::High: return "high";
    case ShadowQuality::Ultra: return "ultra";
    }
    return "off";
}

const char* lodQualityName(LodQuality quality) noexcept
{
    switch (quality) {
    case LodQuality::Performance: return "performance";
    case LodQuality::Balanced: return "balanced";
    case LodQuality::Quality: return "quality";
    case LodQuality::Ultra: return "ultra";
    }
    return "balanced";
}

const char* hlodAggressivenessName(HlodAggressiveness value) noexcept
{
    switch (value) {
    case HlodAggressiveness::Low: return "low";
    case HlodAggressiveness::Medium: return "medium";
    case HlodAggressiveness::High: return "high";
    }
    return "medium";
}

const char* terrainQualityName(TerrainQuality quality) noexcept
{
    switch (quality) {
    case TerrainQuality::Low: return "low";
    case TerrainQuality::Medium: return "medium";
    case TerrainQuality::High: return "high";
    case TerrainQuality::Ultra: return "ultra";
    }
    return "high";
}

const char* performanceModeName(PerformanceMode mode) noexcept
{
    switch (mode) {
    case PerformanceMode::Quality: return "quality";
    case PerformanceMode::Balanced: return "balanced";
    case PerformanceMode::Performance: return "performance";
    }
    return "balanced";
}

} // namespace projectunity::editor
