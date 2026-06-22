#include <projectunity/editor/ViewportWidget.hpp>
#include "ViewportDrawDebugOverlay.hpp"
#include "ViewportLabelGeometry.hpp"
#include "ViewportRenderWorld.hpp"
#include "ViewportRenderWorldDiagnostics.hpp"
#include "ViewportRendererCulling.hpp"
#include "ViewportRendererOverlays.hpp"
#include "ViewportShadowFocus.hpp"
#include "ViewportSourceObjectDebugOverlay.hpp"
#include <projectunity/core/Log.hpp>
#include <projectunity/renderer/IRenderer.hpp>
#include <projectunity/renderer/RenderShadowSetup.hpp>
#include <QString>
#include <QTimer>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <vector>
namespace projectunity::editor {
namespace {
[[nodiscard]] std::uint64_t mixLogHash(std::uint64_t seed, std::uint64_t value) noexcept
{
    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U));
}

[[nodiscard]] bool environmentFlagEnabled(const char* name) noexcept
{
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t length = 0;
    if (::_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
        return false;
    }
    const bool enabled = length > 1U && value[0] != '\0' && value[0] != '0';
    std::free(value);
    return enabled;
#else
    const auto* value = std::getenv(name);
    return value != nullptr && value[0] != '\0' && value[0] != '0';
#endif
}

[[nodiscard]] bool renderWorldChunkBoundsDebugEnabled() noexcept { return environmentFlagEnabled("PROJECTUNITY_RENDERWORLD_CHUNK_BOUNDS"); }
[[nodiscard]] bool viewportCullingLogEnabled() noexcept { return environmentFlagEnabled("PROJECTUNITY_VIEWPORT_CULLING_LOGS"); }
[[nodiscard]] bool forceSelectedSourceModelDrawEnabled() noexcept { return environmentFlagEnabled("PROJECTUNITY_FORCE_DRAW_SELECTED_MODEL_SOURCE"); }
[[nodiscard]] bool dumpSelectedModelAssetEnabled() noexcept { return environmentFlagEnabled("PROJECTUNITY_DUMP_SELECTED_MODEL_ASSET"); }
[[nodiscard]] bool selectedAssetRenderWorldDebugEnabled() noexcept
{
    return environmentFlagEnabled("PROJECTUNITY_TRACE_SELECTED_ASSET")
        || environmentFlagEnabled("PROJECTUNITY_DISABLE_ALL_CULLING_FOR_SELECTED")
        || environmentFlagEnabled("PROJECTUNITY_RENDER_ALL_DRAW_PACKETS_FOR_SELECTED")
        || forceSelectedSourceModelDrawEnabled()
        || dumpSelectedModelAssetEnabled();
}

[[nodiscard]] std::uint64_t elapsedUs(std::chrono::steady_clock::time_point start) noexcept
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count());
}

[[nodiscard]] float& at(renderer::RenderMatrix4& matrix, int row, int column)
{
    return matrix.values[static_cast<std::size_t>(column * 4 + row)];
}
[[nodiscard]] float at(const renderer::RenderMatrix4& matrix, int row, int column)
{
    return matrix.values[static_cast<std::size_t>(column * 4 + row)];
}
[[nodiscard]] renderer::RenderMatrix4 multiply(const renderer::RenderMatrix4& lhs, const renderer::RenderMatrix4& rhs)
{
    renderer::RenderMatrix4 result;
    result.values.fill(0.0F);
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            for (int index = 0; index < 4; ++index) {
                at(result, row, column) += at(lhs, row, index) * at(rhs, index, column);
            }
        }
    }
    return result;
}
[[nodiscard]] renderer::RenderMatrix4 viewMatrix(
    math::Vec3 eye,
    math::Vec3 right,
    math::Vec3 up,
    math::Vec3 forward)
{
    renderer::RenderMatrix4 view;
    view.values.fill(0.0F);
    at(view, 0, 0) = right.x;
    at(view, 0, 1) = right.y;
    at(view, 0, 2) = right.z;
    at(view, 0, 3) = -math::dot(right, eye);
    at(view, 1, 0) = up.x;
    at(view, 1, 1) = up.y;
    at(view, 1, 2) = up.z;
    at(view, 1, 3) = -math::dot(up, eye);
    at(view, 2, 0) = forward.x;
    at(view, 2, 1) = forward.y;
    at(view, 2, 2) = forward.z;
    at(view, 2, 3) = -math::dot(forward, eye);
    at(view, 3, 3) = 1.0F;
    return view;
}
[[nodiscard]] renderer::RenderMatrix4 perspectiveMatrix(float verticalFovRadians, float aspect, float nearPlane, float farPlane)
{
    renderer::RenderMatrix4 projection;
    projection.values.fill(0.0F);
    const auto focal = 1.0F / std::tan(verticalFovRadians * 0.5F);
    at(projection, 0, 0) = focal / std::max(aspect, 0.001F);
    at(projection, 1, 1) = -focal;
    at(projection, 2, 2) = farPlane / (farPlane - nearPlane);
    at(projection, 2, 3) = -(nearPlane * farPlane) / (farPlane - nearPlane);
    at(projection, 3, 2) = 1.0F;
    return projection;
}
[[nodiscard]] float radians(float degrees)
{
    return degrees * 0.01745329251994329577F;
}
[[nodiscard]] math::Vec3 rotateEuler(math::Vec3 value, math::Vec3 rotationEuler)
{
    const auto sinX = std::sin(radians(rotationEuler.x));
    const auto cosX = std::cos(radians(rotationEuler.x));
    const auto sinY = std::sin(radians(rotationEuler.y));
    const auto cosY = std::cos(radians(rotationEuler.y));
    const auto sinZ = std::sin(radians(rotationEuler.z));
    const auto cosZ = std::cos(radians(rotationEuler.z));
    value = {value.x, value.y * cosX - value.z * sinX, value.y * sinX + value.z * cosX};
    value = {value.x * cosY + value.z * sinY, value.y, -value.x * sinY + value.z * cosY};
    return {value.x * cosZ - value.y * sinZ, value.x * sinZ + value.y * cosZ, value.z};
}
[[nodiscard]] math::Vec3 transformPoint(const scene::Entity& entity, math::Vec3 worldPosition, math::Vec3 point)
{
    return worldPosition + rotateEuler(
        {point.x * entity.transform.scale.x, point.y * entity.transform.scale.y, point.z * entity.transform.scale.z},
        entity.transform.rotationEuler);
}
[[nodiscard]] renderer::RenderMatrix4 renderMatrix(const std::array<float, 16>& values)
{
    renderer::RenderMatrix4 matrix;
    matrix.values = values;
    return matrix;
}
[[nodiscard]] renderer::RenderMatrix4 modelMatrix(const scene::Entity& entity, math::Vec3 worldPosition)
{
    renderer::RenderMatrix4 matrix;
    matrix.values.fill(0.0F);
    const auto axisX = rotateEuler({entity.transform.scale.x, 0.0F, 0.0F}, entity.transform.rotationEuler);
    const auto axisY = rotateEuler({0.0F, entity.transform.scale.y, 0.0F}, entity.transform.rotationEuler);
    const auto axisZ = rotateEuler({0.0F, 0.0F, entity.transform.scale.z}, entity.transform.rotationEuler);
    at(matrix, 0, 0) = axisX.x;
    at(matrix, 1, 0) = axisX.y;
    at(matrix, 2, 0) = axisX.z;
    at(matrix, 0, 1) = axisY.x;
    at(matrix, 1, 1) = axisY.y;
    at(matrix, 2, 1) = axisY.z;
    at(matrix, 0, 2) = axisZ.x;
    at(matrix, 1, 2) = axisZ.y;
    at(matrix, 2, 2) = axisZ.z;
    at(matrix, 0, 3) = worldPosition.x;
    at(matrix, 1, 3) = worldPosition.y;
    at(matrix, 2, 3) = worldPosition.z;
    at(matrix, 3, 3) = 1.0F;
    return matrix;
}
[[nodiscard]] const assets::TextureAsset* modelTexture(
    const assets::ModelAsset& model,
    std::optional<std::size_t> textureIndex) noexcept
{
    return textureIndex.has_value() && *textureIndex < model.textures.size()
        ? &model.textures[*textureIndex]
        : nullptr;
}
[[nodiscard]] std::string vec3Text(math::Vec3 value)
{
    std::ostringstream text;
    text << value.x << "," << value.y << "," << value.z;
    return text.str();
}
[[nodiscard]] std::string meshBoundsText(const assets::MeshBounds& bounds)
{
    std::ostringstream text;
    text << "center=(" << vec3Text(bounds.center) << ")"
         << " extents=(" << vec3Text(bounds.maximum - bounds.minimum) << ")"
         << " radius=" << bounds.radius;
    return text.str();
}
[[nodiscard]] std::optional<assets::AssetRecord> assetRecordForId(
    const assets::IAssetManager& assetManager,
    assets::AssetId id)
{
    for (const auto& record : assetManager.records()) {
        if (record.id == id) {
            return record;
        }
    }
    return std::nullopt;
}
[[nodiscard]] std::uint64_t modelLodCount(const assets::ModelAsset& model) noexcept
{
    std::uint64_t result = 0;
    for (const auto& primitive : model.primitives) {
        result += primitive.lods.size();
    }
    return result;
}
[[nodiscard]] math::Vec3 safeNormalized(math::Vec3 value, math::Vec3 fallback)
{
    const auto length = value.length();
    if (length <= 0.00001F || !std::isfinite(length)) {
        return fallback;
    }
    return value / length;
}
struct FrameBounds {
    bool valid {false};
    math::Vec3 minimum;
    math::Vec3 maximum;
    void includeSphere(math::Vec3 center, float radius)
    {
        if (!std::isfinite(radius) || radius < 0.0F) {
            return;
        }
        const math::Vec3 extent {radius, radius, radius};
        if (!valid) {
            minimum = center - extent;
            maximum = center + extent;
            valid = true;
            return;
        }
        minimum.x = std::min(minimum.x, center.x - radius);
        minimum.y = std::min(minimum.y, center.y - radius);
        minimum.z = std::min(minimum.z, center.z - radius);
        maximum.x = std::max(maximum.x, center.x + radius);
        maximum.y = std::max(maximum.y, center.y + radius);
        maximum.z = std::max(maximum.z, center.z + radius);
    }
    [[nodiscard]] math::Vec3 center() const
    {
        return (minimum + maximum) * 0.5F;
    }
    [[nodiscard]] float radius() const
    {
        return (maximum - center()).length();
    }
};
void includeMeshBounds(FrameBounds& bounds, const assets::MeshBounds& meshBounds)
{
    bounds.includeSphere(meshBounds.center, meshBounds.radius);
}
[[nodiscard]] FrameBounds modelPrimitiveBounds(const assets::ModelAsset& model)
{
    FrameBounds bounds;
    for (const auto& primitive : model.primitives) {
        includeMeshBounds(bounds, primitive.bounds);
    }
    return bounds;
}
[[nodiscard]] std::string frameBoundsText(const FrameBounds& bounds)
{
    if (!bounds.valid) {
        return "invalid";
    }
    std::ostringstream text;
    text << "center=(" << vec3Text(bounds.center()) << ")"
         << " extents=(" << vec3Text(bounds.maximum - bounds.minimum) << ")"
         << " radius=" << bounds.radius();
    return text.str();
}
struct ViewportCameraFrame {
    math::Vec3 eye;
    math::Vec3 right;
    math::Vec3 up;
    math::Vec3 forward;
    float verticalFovRadians {1.04719755F};
    float aspectRatio {1.0F};
    float nearPlane {0.05F};
    float farPlane {4000.0F};
};
struct SourceModelDrawStats {
    std::uint64_t sourceDrawsBuilt {0};
    std::uint64_t sourceDrawsSubmitted {0};
    std::uint64_t removedRenderWorldDrawPackets {0};
    bool selectedEntityFound {false};
    bool selectedModelFound {false};
};
[[nodiscard]] FrameBounds modelInstanceBounds(const assets::ModelAsset& model)
{
    FrameBounds bounds;
    if (!model.primitiveInstances.empty()) {
        for (const auto& instance : model.primitiveInstances) {
            includeMeshBounds(bounds, instance.bounds);
        }
        return bounds;
    }
    return modelPrimitiveBounds(model);
}
[[nodiscard]] SourceModelDrawStats appendSelectedSourceModelDraws(
    const scene::Entity& entity,
    const assets::IAssetManager& assetManager,
    const assets::ModelAsset& model,
    math::Vec3 worldPosition,
    const ViewportCameraFrame& cameraFrame,
    const renderer::RenderMatrix4& viewProjection,
    std::uint64_t renderWorldDrawPackets,
    std::uint64_t renderWorldChunks,
    bool dumpInventory,
    bool forceSourceDraw,
    std::vector<renderer::RenderMeshDraw>& meshDraws,
    FrameBounds& visibleBounds,
    std::uint64_t& lastSignature)
{
    SourceModelDrawStats stats;
    stats.selectedEntityFound = true;
    stats.selectedModelFound = true;
    const auto record = assetRecordForId(assetManager, model.id);
    const auto sourceName = record.has_value() ? record->sourceName : std::string {"<unknown>"};
    const auto cacheFile = record.has_value() ? record->cacheFile : std::string {};
    const auto hasFfultCache = !cacheFile.empty() || sourceName.find(".ffult") != std::string::npos;
    const auto rootLocalBounds = modelInstanceBounds(model);
    const auto primitiveBounds = modelPrimitiveBounds(model);

    auto signature = mixLogHash(entity.id.value(), model.id.value());
    signature = mixLogHash(signature, model.primitives.size());
    signature = mixLogHash(signature, model.primitiveInstances.size());
    signature = mixLogHash(signature, model.editorInstances.size());
    signature = mixLogHash(signature, renderWorldDrawPackets);
    signature = mixLogHash(signature, renderWorldChunks);
    signature = mixLogHash(signature, forceSourceDraw ? 1U : 0U);
    signature = mixLogHash(signature, dumpInventory ? 1U : 0U);

    const auto rootMatrix = modelMatrix(entity, worldPosition);
    const auto selectedModelId = entity.meshRenderer.has_value()
        ? entity.meshRenderer->modelAssetId
        : model.id;

    if (forceSourceDraw) {
        const auto before = meshDraws.size();
        meshDraws.erase(
            std::remove_if(
                meshDraws.begin(),
                meshDraws.end(),
                [&](const renderer::RenderMeshDraw& draw) {
                    return draw.sceneNodeId == entity.id.value()
                        || (selectedModelId.isValid() && draw.modelAssetId == selectedModelId);
                }),
            meshDraws.end());
        stats.removedRenderWorldDrawPackets = before - meshDraws.size();
    }

    std::ostringstream details;
    const auto appendPrimitiveDraw = [&](
        std::uint32_t primitiveIndex,
        std::uint32_t sourceIndex,
        const renderer::RenderMatrix4& matrix,
        bool flipsWinding,
        const char* sourceKind,
        const std::string& nodeName) {
        ++stats.sourceDrawsBuilt;
        if (primitiveIndex >= model.primitives.size()) {
            if (stats.sourceDrawsBuilt <= 50U) {
                details << " primitiveIndex=" << primitiveIndex
                        << " " << sourceKind << "Index=" << sourceIndex
                        << " nodeName=" << nodeName
                        << " submitted=false reason=primitiveIndexOutOfRange";
            }
            return;
        }
        const auto& primitive = model.primitives[primitiveIndex];
        if (primitive.materialIndex >= model.materials.size()) {
            if (stats.sourceDrawsBuilt <= 50U) {
                details << " primitiveIndex=" << primitiveIndex
                        << " " << sourceKind << "Index=" << sourceIndex
                        << " nodeName=" << nodeName
                        << " localBounds=" << meshBoundsText(primitive.bounds)
                        << " materialIndex=" << primitive.materialIndex
                        << " submitted=false reason=materialIndexOutOfRange";
            }
            return;
        }
        const auto& material = model.materials[primitive.materialIndex];
        const auto worldBounds = transformViewportBounds(matrix, primitive.bounds);
        if (forceSourceDraw) {
            auto draw = renderer::RenderMeshDraw {
                selectedModelId,
                primitiveIndex,
                0U,
                &primitive,
                &material,
                modelTexture(model, material.baseColorTexture),
                modelTexture(model, material.normalTexture),
                modelTexture(model, material.metallicRoughnessTexture),
                modelTexture(model, material.occlusionTexture),
                modelTexture(model, material.emissiveTexture),
                math::dot(worldBounds.center - cameraFrame.eye, cameraFrame.forward),
                {worldBounds.center.x, worldBounds.center.y, worldBounds.center.z},
                worldBounds.radius,
                matrix,
                multiply(viewProjection, matrix),
                flipsWinding,
                true,
                mixLogHash(entity.id.value(), mixLogHash(primitiveIndex, sourceIndex)),
                mixLogHash(entity.id.value(), mixLogHash(sourceIndex, 0x50A5CEULL)),
                entity.id.value(),
            };
            draw.worldBoundsHalfExtent = {
                (worldBounds.corners[7].x - worldBounds.corners[0].x) * 0.5F,
                (worldBounds.corners[7].y - worldBounds.corners[0].y) * 0.5F,
                (worldBounds.corners[7].z - worldBounds.corners[0].z) * 0.5F,
            };
            draw.distanceToCameraCenter = (worldBounds.center - cameraFrame.eye).length();
            draw.distanceToCameraBounds = std::max(draw.distanceToCameraCenter - worldBounds.radius, 0.0F);
            draw.projectedRadiusPixels = 0.0F;
            draw.previousLodIndex = 0U;
            draw.projectedLodErrorPixels = 0.0F;
            draw.lodSelectionReason = renderer::RenderLodSelectionReason::FullResolution;
            draw.materialIndex = static_cast<std::uint32_t>(primitive.materialIndex);
            draw.generatedTerrainModel = entity.terrain.has_value()
                && entity.terrain->generatedModelAssetId.isValid()
                && selectedModelId == entity.terrain->generatedModelAssetId;
            meshDraws.push_back(draw);
            visibleBounds.includeSphere(worldBounds.center, worldBounds.radius);
            ++stats.sourceDrawsSubmitted;
        }
        if (stats.sourceDrawsBuilt <= 50U) {
            details << " primitiveIndex=" << primitiveIndex
                    << " " << sourceKind << "Index=" << sourceIndex
                    << " nodeName=" << nodeName
                    << " localBounds=" << meshBoundsText(primitive.bounds)
                    << " worldBounds=center=(" << vec3Text(worldBounds.center) << ")"
                    << " radius=" << worldBounds.radius
                    << " materialIndex=" << primitive.materialIndex
                    << " submitted=" << (forceSourceDraw ? "true" : "false")
                    << " reason=" << (forceSourceDraw ? "source-direct" : "inventory-only");
        }
    };

    if (!model.primitiveInstances.empty()) {
        for (std::uint32_t index = 0; index < model.primitiveInstances.size(); ++index) {
            const auto& instance = model.primitiveInstances[index];
            const auto nodeName = index < model.editorInstances.size()
                ? model.editorInstances[index].name
                : std::string {"<primitiveInstance>"};
            appendPrimitiveDraw(
                instance.primitiveIndex,
                index,
                multiply(rootMatrix, renderMatrix(instance.transform)),
                instance.flipsWinding,
                "primitiveInstance",
                nodeName);
        }
    } else {
        for (std::uint32_t primitiveIndex = 0; primitiveIndex < model.primitives.size(); ++primitiveIndex) {
            appendPrimitiveDraw(
                primitiveIndex,
                primitiveIndex,
                rootMatrix,
                false,
                "primitive",
                primitiveIndex < model.editorInstances.size()
                    ? model.editorInstances[primitiveIndex].name
                    : std::string {"<primitive>"});
        }
    }

    signature = mixLogHash(signature, stats.sourceDrawsBuilt);
    signature = mixLogHash(signature, stats.sourceDrawsSubmitted);
    signature = mixLogHash(signature, meshDraws.size());
    if (signature != lastSignature) {
        std::ostringstream message;
        if (dumpInventory || forceSourceDraw) {
            message << "[ModelAssetInventory]"
                    << " modelAssetId=" << (model.id.isValid() ? model.id.value() : 0U)
                    << " name=" << model.name
                    << " sourcePath=" << sourceName
                    << " meshCount=" << (model.primitiveInstances.empty() ? model.primitives.size() : model.primitiveInstances.size())
                    << " primitiveCount=" << model.primitives.size()
                    << " materialCount=" << model.materials.size()
                    << " textureCount=" << model.textures.size()
                    << " nodeCount=" << model.editorInstances.size()
                    << " editorInstanceCount=" << model.editorInstances.size()
                    << " primitiveInstanceCount=" << model.primitiveInstances.size()
                    << " lodCount=" << modelLodCount(model)
                    << " clusterCount=" << model.primitiveClusters.size()
                    << " batchCount=" << model.primitiveInstances.size()
                    << " chunkCount=" << model.primitiveClusters.size()
                    << " rootBounds=" << frameBoundsText(rootLocalBounds)
                    << " sumPrimitiveBounds=" << frameBoundsText(primitiveBounds)
                    << " hasFfultCache=" << (hasFfultCache ? "true" : "false")
                    << " cachePath=" << cacheFile
                    << " [RenderWorldInventory]"
                    << " entityId=" << entity.id.value()
                    << " modelAssetId=" << (selectedModelId.isValid() ? selectedModelId.value() : 0U)
                    << " renderRecords=1"
                    << " chunks=" << renderWorldChunks
                    << " drawPackets=" << renderWorldDrawPackets
                    << " overviewDraws=see_SelectedAssetTrace"
                    << " proxyDraws=see_SelectedAssetTrace"
                    << " finalSubmitted=" << meshDraws.size();
        }
        if (forceSourceDraw) {
            message << " [ForceSourceModelDraw]"
                    << " entityId=" << entity.id.value()
                    << " entityName=" << entity.name
                    << " modelAssetId=" << (selectedModelId.isValid() ? selectedModelId.value() : 0U)
                    << " modelName=" << model.name
                    << " primitiveCount=" << model.primitives.size()
                    << " editorInstanceCount=" << model.editorInstances.size()
                    << " meshPrimitiveInstanceCount=" << model.primitiveInstances.size()
                    << " nodeCount=" << model.editorInstances.size()
                    << " sourceDrawsBuilt=" << stats.sourceDrawsBuilt
                    << " sourceDrawsSubmitted=" << stats.sourceDrawsSubmitted
                    << " renderWorldDrawPackets=" << renderWorldDrawPackets
                    << " renderWorldChunks=" << renderWorldChunks
                    << " removedRenderWorldDrawPackets=" << stats.removedRenderWorldDrawPackets
                    << " mode=SOURCE_MODEL_DIRECT";
        }
        message << details.str();
        if (stats.sourceDrawsBuilt > 50U) {
            message << " sourceDrawDetailTruncated=" << (stats.sourceDrawsBuilt - 50U);
        }
        core::logInfo(core::LogCategory::Renderer, message.str());
        lastSignature = signature;
    }
    return stats;
}
[[nodiscard]] const char* viewportModeName(ViewportMode mode) noexcept
{
    return mode == ViewportMode::Game ? "Game" : "Scene";
}

} // namespace

void ViewportWidget::setShadowUpdateMode(renderer::RenderShadowUpdateMode mode)
{
    shadowUpdateMode_ = mode;
    if (mode == renderer::RenderShadowUpdateMode::Off) {
        frozenShadowSelection_.reset();
        rendererShadowMeshDraws_.clear(); rendererShadowCandidateInstances_ = rendererShadowPolicyRejectedInstances_ = rendererShadowVisibleInstances_ = 0; rendererShadowOnlyCandidateInstances_ = rendererShadowOnlyRejectedInstances_ = 0;
    }
    update();
}

renderer::RenderShadowUpdateMode ViewportWidget::shadowUpdateMode() const noexcept
{
    return shadowUpdateMode_;
}

void ViewportWidget::setVSyncEnabled(bool enabled)
{
    if (vSyncEnabled_ == enabled) {
        return;
    }
    vSyncEnabled_ = enabled;
    if (renderer_ != nullptr && rendererSurfaceHandle_ != nullptr) {
        renderer_->releaseSurface(rendererSurfaceHandle_);
    }
    rendererSurfaceHandle_ = nullptr;
    rendererSurfaceWidth_ = 0;
    rendererSurfaceHeight_ = 0;
    rendererSurfaceAttempted_ = false;
    rendererSurfaceReady_ = false;
    rendererSurfaceResizePending_ = false;
    hasLastRendererFrameTime_ = false;
    update();
}

bool ViewportWidget::vSyncEnabled() const noexcept
{
    return vSyncEnabled_;
}

void ViewportWidget::setFrameRateLimitFps(int fps)
{
    const auto clamped = std::clamp(fps, 0, 1000);
    if (frameRateLimitFps_ == clamped) {
        return;
    }
    frameRateLimitFps_ = clamped;
    hasLastRendererFrameTime_ = false;
    if (frameRateLimitTimer_ != nullptr) {
        frameRateLimitTimer_->stop();
    }
    update();
}

int ViewportWidget::frameRateLimitFps() const noexcept
{
    return frameRateLimitFps_;
}

void ViewportWidget::setAssetLodSettings(ViewportAssetLodSettings settings)
{
    assetLodSettings_ = sanitizeViewportAssetLodSettings(settings);
    update();
}

ViewportAssetLodSettings ViewportWidget::assetLodSettings() const noexcept
{
    return assetLodSettings_;
}

void ViewportWidget::setTextureDebugSettings(renderer::RenderTextureDebugSettings settings)
{
    if (textureDebugSettings_ == settings) {
        return;
    }
    textureDebugSettings_ = settings;
    update();
}

void ViewportWidget::setEditorSunLight(renderer::RenderLight light)
{
    light.type = renderer::RenderLightType::Directional;
    editorSunLight_ = light;
    update();
}

bool ViewportWidget::ensureRendererSurface()
{
    if (rendererSurfaceResizePending_) {
        return false;
    }
    if (renderer_ == nullptr || !renderer_->isReady() || width() <= 0 || height() <= 0) {
        return false;
    }
    auto* handle = reinterpret_cast<void*>(winId());
    if (handle == nullptr) {
        return false;
    }
    const auto currentWidth = width();
    const auto currentHeight = height();
    if (rendererSurfaceAttempted_
        && rendererSurfaceHandle_ == handle
        && rendererSurfaceWidth_ == currentWidth
        && rendererSurfaceHeight_ == currentHeight) {
        return rendererSurfaceReady_;
    }
    rendererSurfaceHandle_ = handle;
    rendererSurfaceWidth_ = currentWidth;
    rendererSurfaceHeight_ = currentHeight;
    rendererSurfaceAttempted_ = true;
    renderer::ViewportRenderSurfaceDesc desc;
    desc.nativeWindowHandle = handle;
    desc.width = static_cast<std::uint32_t>(currentWidth);
    desc.height = static_cast<std::uint32_t>(currentHeight);
    desc.vsync = vSyncEnabled_;
    std::string error;
    rendererSurfaceReady_ = renderer_->prepareSurface(desc, &error);
    if (!rendererSurfaceReady_) {
        core::logWarning(
            core::LogCategory::Renderer,
            QStringLiteral("Viewport Vulkan surface unavailable: %1")
                .arg(QString::fromStdString(error.empty() ? "unknown error" : error))
                .toStdString());
    }
    return rendererSurfaceReady_;
}
bool ViewportWidget::renderRendererFrame()
{
    gpuMeshFrameRendered_ = false;
    if (frameRateLimitFps_ > 0 && hasLastRendererFrameTime_) {
        const auto now = std::chrono::steady_clock::now();
        const auto targetUs = 1'000'000 / std::max(frameRateLimitFps_, 1);
        const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
            now - lastRendererFrameTime_).count();
        if (elapsedUs < targetUs) {
            if (frameRateLimitTimer_ != nullptr && !frameRateLimitTimer_->isActive()) {
                const auto waitMs = static_cast<int>(std::max<std::int64_t>(1, (targetUs - elapsedUs + 999) / 1000));
                frameRateLimitTimer_->start(waitMs);
            }
            return true;
        }
    }
    if (!ensureRendererSurface() || renderer_ == nullptr || rendererSurfaceHandle_ == nullptr) {
        return false;
    }
    renderer::ViewportRenderSurfaceDesc desc;
    desc.nativeWindowHandle = rendererSurfaceHandle_;
    desc.width = static_cast<std::uint32_t>(rendererSurfaceWidth_);
    desc.height = static_cast<std::uint32_t>(rendererSurfaceHeight_);
    desc.vsync = vSyncEnabled_;
    renderer::RenderFrame frame;
    const auto runtimeSnapshot = mode_ == ViewportMode::Game && gameRuntimeSnapshotEnabled_;
    frame.renderPath = mode_ == ViewportMode::Scene
        ? renderer::RenderFramePath::SceneView
        : (runtimeSnapshot ? renderer::RenderFramePath::GameRuntime : renderer::RenderFramePath::GameView);
    frame.textureDebug = textureDebugSettings_;
    frame.staticUploadBudgetBytes = assetLodSettings_.staticUploadBudgetBytes;
    frame.staticUploadBatchBudget = assetLodSettings_.staticUploadBatchBudget;
    frame.unlimitedStaticUploads = assetLodSettings_.unlimitedStaticUploads;
    const auto editorBuildStart = std::chrono::steady_clock::now();
    frame.clearColor.red = mode_ == ViewportMode::Scene ? 0.12F : 0.02F;
    frame.clearColor.green = mode_ == ViewportMode::Scene ? 0.13F : 0.02F;
    frame.clearColor.blue = mode_ == ViewportMode::Scene ? 0.15F : 0.025F;
    frame.clearColor.alpha = 1.0F;
    frame.environment = environmentSettings_;
    rendererMeshDraws_.clear();
    if (shadowUpdateMode_ != renderer::RenderShadowUpdateMode::Frozen) {
        rendererShadowMeshDraws_.clear(); rendererShadowCandidateInstances_ = rendererShadowPolicyRejectedInstances_ = rendererShadowVisibleInstances_ = 0; rendererShadowOnlyCandidateInstances_ = rendererShadowOnlyRejectedInstances_ = 0;
    }
    rendererLights_.clear();
    bool hasMeshSceneContent = false;
    FrameBounds visibleBounds;
    ViewportCameraFrame cameraFrame {
        cameraPosition(),
        cameraRight(),
        cameraUp(),
        cameraForward(),
        camera_.verticalFovRadians,
        aspectRatio(),
    };
    const char* cameraSource = mode_ == ViewportMode::Game ? "game-editor-fallback" : "scene-editor-camera";
    if (mode_ == ViewportMode::Game && scene_ != nullptr) {
        bool cameraFromSceneEntity = false;
        for (const auto& entity : scene_->entities()) {
            if (gameCameraEntityId_.isValid() && entity.id != gameCameraEntityId_) { continue; }
            if (!entity.camera.has_value()) { continue; }
            const auto worldPosition = entityWorldPosition(entity.id);
            if (!worldPosition.has_value()) {
                continue;
            }
            const auto& imported = *entity.camera;
            if (imported.projection != scene::CameraComponentProjection::Perspective) {
                continue;
            }
            cameraFrame.eye = *worldPosition;
            cameraFrame.forward = safeNormalized(
                rotateEuler(imported.direction, entity.transform.rotationEuler),
                cameraFrame.forward);
            cameraFrame.right = safeNormalized(
                rotateEuler(imported.right, entity.transform.rotationEuler),
                cameraFrame.right);
            cameraFrame.up = safeNormalized(
                rotateEuler(imported.up, entity.transform.rotationEuler),
                cameraFrame.up);
            cameraFrame.right = safeNormalized(
                cameraFrame.right - cameraFrame.forward * math::dot(cameraFrame.right, cameraFrame.forward),
                safeNormalized(math::cross(cameraFrame.up, cameraFrame.forward), cameraFrame.right));
            cameraFrame.up = safeNormalized(math::cross(cameraFrame.forward, cameraFrame.right), cameraFrame.up);
            cameraFrame.verticalFovRadians = imported.verticalFovRadians;
            cameraFrame.aspectRatio = imported.aspectRatio > 0.0F ? imported.aspectRatio : aspectRatio();
            cameraFrame.nearPlane = imported.nearPlane;
            cameraFrame.farPlane = imported.farPlane;
            cameraFromSceneEntity = true;
            cameraSource = "game-scene-camera";
            break;
        }
        if (!cameraFromSceneEntity && assetManager_ != nullptr) {
        for (const auto& entity : scene_->entities()) {
            if (!entity.meshRenderer.has_value()) {
                continue;
            }
            const auto model = assetManager_->model(entity.meshRenderer->modelAssetId);
            const auto worldPosition = entityWorldPosition(entity.id);
            if (model == nullptr || !worldPosition.has_value() || model->cameras.empty()) {
                continue;
            }
            const auto imported = std::find_if(model->cameras.begin(), model->cameras.end(), [](const assets::ImportedCameraAsset& camera) {
                return camera.projection == assets::ImportedCameraProjection::Perspective;
            });
            if (imported == model->cameras.end()) {
                continue;
            }
            cameraFrame.eye = transformPoint(entity, *worldPosition, imported->position);
            cameraFrame.forward = safeNormalized(
                rotateEuler(imported->direction, entity.transform.rotationEuler),
                cameraFrame.forward);
            cameraFrame.right = safeNormalized(
                rotateEuler(imported->right, entity.transform.rotationEuler),
                cameraFrame.right);
            cameraFrame.up = safeNormalized(
                rotateEuler(imported->up, entity.transform.rotationEuler),
                cameraFrame.up);
            cameraFrame.right = safeNormalized(
                cameraFrame.right - cameraFrame.forward * math::dot(cameraFrame.right, cameraFrame.forward),
                safeNormalized(math::cross(cameraFrame.up, cameraFrame.forward), cameraFrame.right));
            cameraFrame.up = safeNormalized(math::cross(cameraFrame.forward, cameraFrame.right), cameraFrame.up);
            cameraFrame.verticalFovRadians = imported->verticalFovRadians;
            cameraFrame.aspectRatio = imported->aspectRatio > 0.0F ? imported->aspectRatio : aspectRatio();
            cameraFrame.nearPlane = imported->nearPlane;
            cameraFrame.farPlane = imported->farPlane;
            cameraSource = "game-imported-camera";
            break;
        }
        }
    }
    const auto& right = cameraFrame.right;
    const auto& up = cameraFrame.up;
    const auto& forward = cameraFrame.forward;
    const auto& eye = cameraFrame.eye;
    const auto view = viewMatrix(eye, right, up, forward);
    const auto projection = perspectiveMatrix(
        cameraFrame.verticalFovRadians,
        cameraFrame.aspectRatio,
        cameraFrame.nearPlane,
        cameraFrame.farPlane);
    const auto viewProjection = multiply(projection, view);
    frame.viewProjection = viewProjection;
    frame.cameraPosition = {eye.x, eye.y, eye.z};
    std::vector<ViewportRenderWorldChunkDebug> renderWorldDebugChunks;
    if (renderWorld_ != nullptr) {
        const auto renderWorldStart = std::chrono::steady_clock::now();
        const ViewportRenderWorldCamera renderWorldCamera {
            eye,
            right,
            up,
            forward,
            cameraFrame.verticalFovRadians,
            cameraFrame.aspectRatio,
            cameraFrame.nearPlane,
            cameraFrame.farPlane,
        };
        const auto renderWorldSelectedEntityId = runtimeSnapshot && !selectedAssetRenderWorldDebugEnabled()
            ? scene::EntityId {}
            : selectedEntityId_;
        const auto renderWorldFrame = renderWorld_->buildFrame(
            scene_,
            assetManager_,
            renderWorldSelectedEntityId,
            renderWorldCamera,
            viewProjection,
            height(),
            assetLodSettings_,
            runtimeSnapshot,
            rendererMeshDraws_,
            rendererLights_);
        frame.renderWorldBuildCpuTimeUs = elapsedUs(renderWorldStart);
        frame.renderWorldRebuiltRecordCount = renderWorldFrame.stats.rebuiltRecordCount;
        frame.renderWorldReusedRecordCount = renderWorldFrame.stats.reusedRecordCount;
        frame.shadowCandidateInstances = renderWorldFrame.stats.shadowCandidateInstances;
        frame.shadowPolicyRejectedInstances = renderWorldFrame.stats.shadowPolicyRejectedInstances;
        hasMeshSceneContent = renderWorldFrame.hasMeshSceneContent;
        frame.sceneNodeCount = renderWorldFrame.stats.sceneNodeCount;
        frame.renderChunkCount = renderWorldFrame.stats.renderChunkCount;
        frame.visibleRenderChunkCount = renderWorldFrame.stats.visibleRenderChunkCount;
        frame.renderInstanceCount = renderWorldFrame.stats.renderInstanceCount;
        frame.visibleRenderInstanceCount = renderWorldFrame.stats.visibleRenderInstanceCount;
        frame.largeRenderChunkCount = renderWorldFrame.stats.largeRenderChunkCount;
        frame.largestRenderChunkTriangleCount = renderWorldFrame.stats.largestRenderChunkTriangleCount;
        frame.largestRenderChunkInstanceCount = renderWorldFrame.stats.largestRenderChunkInstanceCount;
        frame.maxRenderChunkExtent = renderWorldFrame.stats.maxRenderChunkExtent;
        frame.candidateMeshDrawCount = renderWorldFrame.stats.candidateMeshDrawCount;
        frame.culledMeshDrawCount = renderWorldFrame.stats.culledMeshDrawCount;
        frame.candidateTriangleCount = renderWorldFrame.stats.candidateTriangleCount;
        frame.culledTriangleCount = renderWorldFrame.stats.culledTriangleCount;
        frame.lodMeshDrawCount = renderWorldFrame.stats.lodMeshDrawCount;
        frame.lodTriangleReductionCount = renderWorldFrame.stats.lodTriangleReductionCount;
        frame.hlodMeshDrawCount = renderWorldFrame.stats.hlodMeshDrawCount;
        frame.hlodCandidateDrawCount = renderWorldFrame.stats.hlodCandidateDrawCount;
        frame.hlodTriangleReductionCount = renderWorldFrame.stats.hlodTriangleReductionCount;
        frame.hlodRejectedNoOverviewCount = renderWorldFrame.stats.hlodRejectedNoOverviewCount;
        frame.hlodRejectedVisibleWorkCount = renderWorldFrame.stats.hlodRejectedVisibleWorkCount;
        frame.hlodRejectedCoverageCount = renderWorldFrame.stats.hlodRejectedCoverageCount;
        frame.hlodRejectedScreenCount = renderWorldFrame.stats.hlodRejectedScreenCount;
        frame.hlodRejectedClusterScreenCount = renderWorldFrame.stats.hlodRejectedClusterScreenCount;
        frame.hlodCollapsedChunkCount = renderWorldFrame.stats.hlodCollapsedChunkCount; frame.hlodScreenCoverage = renderWorldFrame.stats.hlodScreenCoverage; frame.hlodCameraDistance = renderWorldFrame.stats.hlodCameraDistance;
        frame.occlusionTestedChunkCount = renderWorldFrame.stats.occlusionTestedChunkCount;
        frame.occlusionRejectedChunkCount = renderWorldFrame.stats.occlusionRejectedChunkCount;
        frame.occlusionOccluderChunkCount = renderWorldFrame.stats.occlusionOccluderChunkCount;
        frame.occlusionRejectedInstanceCount = renderWorldFrame.stats.occlusionRejectedInstanceCount;
        frame.occlusionRejectedTriangleCount = renderWorldFrame.stats.occlusionRejectedTriangleCount;
        frame.spatialCellCount = renderWorldFrame.stats.spatialCellCount;
        frame.spatialCellTestCount = renderWorldFrame.stats.spatialCellTestCount;
        frame.spatialCellRejectedCount = renderWorldFrame.stats.spatialCellRejectedCount;
        frame.spatialCellCandidateChunkCount = renderWorldFrame.stats.spatialCellCandidateChunkCount;
        frame.renderWorldDrawPacketCount = renderWorldFrame.stats.finalDrawPacketCount; frame.renderWorldTriangleCount = renderWorldFrame.stats.finalTriangleCount; frame.renderWorldFinalVisibleChunkCount = renderWorldFrame.stats.finalVisibleChunkCount; frame.renderWorldBudgetDegradedDrawCount = renderWorldFrame.stats.budgetDegradedDrawCount;
        if (renderWorldFrame.visibleBoundsValid) {
            visibleBounds.includeSphere(renderWorldFrame.visibleBoundsCenter, renderWorldFrame.visibleBoundsRadius);
        }
        if (viewportCullingLogEnabled()) {
            auto cullingSignature = mixLogHash(renderWorldFrame.stats.visibleRenderChunkCount, renderWorldFrame.stats.visibleRenderInstanceCount);
            cullingSignature = mixLogHash(cullingSignature, renderWorldFrame.stats.occlusionRejectedChunkCount);
            cullingSignature = mixLogHash(cullingSignature, renderWorldFrame.stats.occlusionRejectedInstanceCount);
            cullingSignature = mixLogHash(cullingSignature, renderWorldFrame.stats.culledMeshDrawCount);
            cullingSignature = mixLogHash(cullingSignature, static_cast<std::uint64_t>(mode_));
            ++cullingLogFrameCounter_;
            const auto framesSinceLog = cullingLogFrameCounter_ - lastCullingLogFrame_;
            const auto signatureChanged = cullingSignature != lastCullingLogSignature_;
            if (cullingLogFrameCounter_ == 1U || cullingLogFrameCounter_ % 60U == 0U || (signatureChanged && framesSinceLog >= 15U)) {
                std::ostringstream message;
                message << "Viewport culling mode=" << viewportModeName(mode_)
                        << " camera=" << cameraSource
                        << " eye=(" << eye.x << "," << eye.y << "," << eye.z << ")"
                        << " forward=(" << forward.x << "," << forward.y << "," << forward.z << ")"
                        << " fov=" << cameraFrame.verticalFovRadians
                        << " aspect=" << cameraFrame.aspectRatio
                        << " near=" << cameraFrame.nearPlane
                        << " far=" << cameraFrame.farPlane
                        << " chunks=" << renderWorldFrame.stats.visibleRenderChunkCount << "/" << renderWorldFrame.stats.renderChunkCount
                        << " instances=" << renderWorldFrame.stats.visibleRenderInstanceCount << "/" << renderWorldFrame.stats.renderInstanceCount
                        << " occlusion tested/rejected/occluders="
                        << renderWorldFrame.stats.occlusionTestedChunkCount << "/"
                        << renderWorldFrame.stats.occlusionRejectedChunkCount << "/"
                        << renderWorldFrame.stats.occlusionOccluderChunkCount
                        << " rejectedInstances=" << renderWorldFrame.stats.occlusionRejectedInstanceCount
                        << " rejectedTriangles=" << renderWorldFrame.stats.occlusionRejectedTriangleCount
                        << " meshDrawCandidates=" << renderWorldFrame.stats.candidateMeshDrawCount
                        << " culledDraws=" << renderWorldFrame.stats.culledMeshDrawCount;
                appendViewportVisibleDrawDiagnostics(message, rendererMeshDraws_, renderWorldCamera);
                core::logInfo(core::LogCategory::Renderer, message.str());
                lastCullingLogFrame_ = cullingLogFrameCounter_;
                lastCullingLogSignature_ = cullingSignature;
            }
        }
        renderWorldDebugChunks = renderWorldFrame.debugChunks;
    }
    if ((forceSelectedSourceModelDrawEnabled() || dumpSelectedModelAssetEnabled())
        && scene_ != nullptr && assetManager_ != nullptr && selectedEntityId_.isValid()) {
        const scene::Entity* selectedEntity = nullptr;
        for (const auto& entity : scene_->entities()) {
            if (entity.id == selectedEntityId_) {
                selectedEntity = &entity;
                break;
            }
        }
        if (selectedEntity != nullptr && selectedEntity->meshRenderer.has_value()) {
            const auto model = assetManager_->model(selectedEntity->meshRenderer->modelAssetId);
            const auto selectedWorldPosition = entityWorldPosition(selectedEntity->id);
            if (model != nullptr && selectedWorldPosition.has_value()) {
                const auto renderWorldDrawPackets = static_cast<std::uint64_t>(rendererMeshDraws_.size());
                const auto renderWorldChunks = frame.renderChunkCount;
                const auto sourceStats = appendSelectedSourceModelDraws(
                    *selectedEntity,
                    *assetManager_,
                    *model,
                    *selectedWorldPosition,
                    cameraFrame,
                    viewProjection,
                    renderWorldDrawPackets,
                    renderWorldChunks,
                    dumpSelectedModelAssetEnabled(),
                    forceSelectedSourceModelDrawEnabled(),
                    rendererMeshDraws_,
                    visibleBounds,
                    lastSourceModelDebugSignature_);
                if (sourceStats.sourceDrawsSubmitted > 0U) {
                    frame.renderWorldDrawPacketCount = static_cast<std::uint64_t>(rendererMeshDraws_.size());
                }
            } else {
                const auto signature = mixLogHash(selectedEntityId_.value(), model == nullptr ? 0U : 1U);
                if (signature != lastSourceModelDebugSignature_) {
                    std::ostringstream message;
                    message << "[ForceSourceModelDraw]"
                            << " entityId=" << selectedEntityId_.value()
                            << " entityName=" << selectedEntity->name
                            << " selectedEntityFound=true"
                            << " selectedModelFound=" << (model != nullptr ? "true" : "false")
                            << " worldPositionFound=" << (selectedWorldPosition.has_value() ? "true" : "false")
                            << " mode=SOURCE_MODEL_DIRECT";
                    core::logInfo(core::LogCategory::Renderer, message.str());
                    lastSourceModelDebugSignature_ = signature;
                }
            }
        } else {
            const auto signature = mixLogHash(selectedEntityId_.value(), 0x51E1EC7ULL);
            if (signature != lastSourceModelDebugSignature_) {
                std::ostringstream message;
                message << "[ForceSourceModelDraw]"
                        << " entityId=" << selectedEntityId_.value()
                        << " selectedEntityFound=" << (selectedEntity != nullptr ? "true" : "false")
                        << " selectedModelFound=false"
                        << " mode=SOURCE_MODEL_DIRECT";
                core::logInfo(core::LogCategory::Renderer, message.str());
                lastSourceModelDebugSignature_ = signature;
            }
        }
    }
    {
        auto sunLight = editorSunLight_;
        const auto sunDirection = safeNormalized(
            {sunLight.direction[0], sunLight.direction[1], sunLight.direction[2]},
            {0.35F, -0.82F, 0.45F});
        sunLight.type = renderer::RenderLightType::Directional;
        sunLight.direction = {sunDirection.x, sunDirection.y, sunDirection.z};
        rendererLights_.insert(rendererLights_.begin(), sunLight);
        if (rendererLights_.size() > renderer::kMaxFrameLights) {
            rendererLights_.resize(renderer::kMaxFrameLights);
        }
    }
    if (visibleBounds.valid) {
        const auto center = visibleBounds.center();
        frame.visibleBoundsCenter = {center.x, center.y, center.z};
        frame.visibleBoundsRadius = visibleBounds.radius();
    } else {
        frame.visibleBoundsCenter = {camera_.target.x, camera_.target.y, camera_.target.z};
        frame.visibleBoundsRadius = std::max(camera_.distance, 1.0F);
    }
    frame.lights = std::span<const renderer::RenderLight>(rendererLights_);
    const auto shadowsRequested = shadowUpdateMode_ != renderer::RenderShadowUpdateMode::Off;
    const auto shadowFocus = stableViewportShadowFocus(
        mode_,
        cameraFrame.eye,
        cameraFrame.forward,
        camera_.target,
        camera_.distance,
        cameraFrame.farPlane,
        visibleBounds.valid,
        visibleBounds.valid ? visibleBounds.radius() : 0.0F);
    const std::array<float, 3> shadowFocusCenter {
        shadowFocus.center.x,
        shadowFocus.center.y,
        shadowFocus.center.z,
    };
    auto shadowSelection = shadowsRequested
        ? renderer::chooseShadowMap(frame.lights, shadowFocusCenter, shadowFocus.radius)
        : renderer::RenderShadowMapSelection {};
    if (shadowUpdateMode_ == renderer::RenderShadowUpdateMode::Frozen) {
        if (frozenShadowSelection_.has_value()) {
            shadowSelection = *frozenShadowSelection_;
        } else if (shadowSelection.enabled) {
            frozenShadowSelection_ = shadowSelection;
        }
    } else if (shadowUpdateMode_ == renderer::RenderShadowUpdateMode::Live && shadowSelection.enabled) {
        frozenShadowSelection_ = shadowSelection;
    }
    ViewportRenderWorldStats shadowStats;
    const auto canReuseFrozenShadowCasters = shadowUpdateMode_ == renderer::RenderShadowUpdateMode::Frozen
        && !rendererShadowMeshDraws_.empty();
    const auto* shadowLight = shadowSelection.lightIndex < rendererLights_.size() ? &rendererLights_[shadowSelection.lightIndex] : nullptr;
    if (shadowUpdateMode_ != renderer::RenderShadowUpdateMode::Off
        && shadowSelection.enabled
        && renderWorld_ != nullptr) {
        if (canReuseFrozenShadowCasters) {
            shadowStats.shadowCandidateInstances = rendererShadowCandidateInstances_;
            shadowStats.shadowPolicyRejectedInstances = rendererShadowPolicyRejectedInstances_; shadowStats.shadowVisibleInstances = rendererShadowVisibleInstances_;
            shadowStats.shadowOnlyCandidateInstances = rendererShadowOnlyCandidateInstances_; shadowStats.shadowOnlyRejectedInstances = rendererShadowOnlyRejectedInstances_;
        } else {
            renderWorld_->collectShadowCasters(
                shadowSelection,
                shadowLight,
                {cameraFrame.eye, cameraFrame.right, cameraFrame.up, cameraFrame.forward, cameraFrame.verticalFovRadians, cameraFrame.aspectRatio, cameraFrame.nearPlane, cameraFrame.farPlane},
                height(),
                assetLodSettings_,
                (mode_ == ViewportMode::Game && gameRuntimeSnapshotEnabled_) ? scene::EntityId {} : selectedEntityId_,
                rendererShadowMeshDraws_,
                shadowStats);
            rendererShadowCandidateInstances_ = shadowStats.shadowCandidateInstances;
            rendererShadowPolicyRejectedInstances_ = shadowStats.shadowPolicyRejectedInstances; rendererShadowVisibleInstances_ = shadowStats.shadowVisibleInstances;
            rendererShadowOnlyCandidateInstances_ = shadowStats.shadowOnlyCandidateInstances; rendererShadowOnlyRejectedInstances_ = shadowStats.shadowOnlyRejectedInstances;
        }
    }
    frame.shadowUpdateMode = shadowUpdateMode_;
    frame.shadowCandidateInstances = shadowStats.shadowCandidateInstances;
    frame.shadowPolicyRejectedInstances = shadowStats.shadowPolicyRejectedInstances; frame.shadowVisibleInstances = shadowStats.shadowVisibleInstances;
    frame.shadowOnlyCandidateInstances = shadowStats.shadowOnlyCandidateInstances; frame.shadowOnlyRejectedInstances = shadowStats.shadowOnlyRejectedInstances; frame.shadowHlodProxyDrawCount = shadowStats.shadowHlodProxyDrawCount;
    if (shadowUpdateMode_ != renderer::RenderShadowUpdateMode::Off
        && !rendererShadowMeshDraws_.empty()
        && shadowSelection.enabled) {
        frame.shadowViewProjection = shadowSelection.viewProjection;
        frame.shadowViewProjections = shadowSelection.viewProjections;
        frame.shadowCascadeSplits = shadowSelection.cascadeSplits;
        frame.shadowLightIndex = shadowSelection.lightIndex;
        frame.shadowViewCount = shadowSelection.viewCount;
        frame.shadowCascadeCount = shadowSelection.cascadeCount;
        frame.shadowDepthFarPlane = shadowSelection.depthFarPlane;
        frame.shadowMode = shadowSelection.mode;
        frame.shadowsEnabled = true;
    }
    frame.shadowMeshDraws = std::span<const renderer::RenderMeshDraw>(rendererShadowMeshDraws_);
    frame.meshDraws = std::span<const renderer::RenderMeshDraw>(rendererMeshDraws_);
    frame.meshDebugOpacity = mode_ == ViewportMode::Scene && assetXrayDebugEnabled_ ? 0.26F : 1.0F; frame.meshWireOverlayEnabled = mode_ == ViewportMode::Scene && meshWireOverlayEnabled_;
    frame.selectedMeshWireOverlayEnabled = mode_ == ViewportMode::Scene && selectedEntityId_.isValid();
    frame.selectedMeshWireOverlaySceneNodeId = selectedEntityId_.isValid() ? selectedEntityId_.value() : 0U;
    rendererGizmoVertices_.clear();
    rendererGizmoIndices_.clear();
    rendererColorMeshDraws_.clear();
    if (mode_ == ViewportMode::Scene) {
        const ViewportLabelCamera labelCamera {
            eye,
            right,
            up,
            forward,
            cameraFrame.verticalFovRadians,
            static_cast<float>(std::max(height(), 1)),
        };
        detail::appendGrid(rendererGizmoVertices_, rendererGizmoIndices_, forward, right, camera_.distance);
        detail::appendAxes(rendererGizmoVertices_, rendererGizmoIndices_, forward, right, camera_.distance);
        if (sunDirectionDebugEnabled_) {
            detail::appendSunDirection(
                rendererGizmoVertices_,
                rendererGizmoIndices_,
                camera_.target,
                safeNormalized({editorSunLight_.direction[0], editorSunLight_.direction[1], editorSunLight_.direction[2]},
                    {0.35F, -0.82F, 0.45F}),
                forward,
                right,
                up,
                camera_.distance);
        }
        if (renderWorldDebugChunks.size() > 1U && (renderWorldChunkBoundsDebugEnabled() || assetXrayDebugEnabled_)) {
            for (const auto& chunk : renderWorldDebugChunks) {
                const auto color = chunk.visible
                    ? (chunk.large
                        ? std::array<float, 4> {1.0F, 0.72F, 0.22F, 0.38F}
                        : std::array<float, 4> {0.32F, 0.86F, 0.62F, 0.22F})
                    : std::array<float, 4> {0.28F, 0.48F, 1.0F, 0.18F};
                detail::appendBounds(
                    rendererGizmoVertices_,
                    rendererGizmoIndices_,
                    chunk.corners,
                    color,
                    chunk.large ? 1.4F : 0.85F,
                    forward,
                    right,
                    camera_.distance);
            }
        }
        if (scene_ != nullptr) {
            auto primitiveProxyCount = std::size_t {0};
            if (scene_->entityCount() <= 96U) {
                primitiveProxyCount = std::count_if(scene_->entities().begin(), scene_->entities().end(), [](const scene::Entity& entity) {
                    return entity.meshRenderer.has_value()
                        && (entity.meshRenderer->primitiveInstanceIndex.has_value()
                            || entity.meshRenderer->editorInstanceIndex.has_value())
                        && !entity.meshRenderer->renderable;
                });
            }
            const auto denseImportedHierarchy = primitiveProxyCount > 24U || scene_->entityCount() > 96U;
            if (!denseImportedHierarchy) {
                detail::appendHierarchyLinks(
                    rendererGizmoVertices_,
                    rendererGizmoIndices_,
                    *scene_,
                    [this](scene::EntityId id) {
                        return entityWorldPosition(id);
                    },
                    selectedEntityId_,
                    false,
                    forward,
                    right,
                    camera_.distance);
            }
            std::size_t labelsSubmitted = 0;
            const auto appendEntityOverlay = [&](const scene::Entity& entity) {
                if (labelsSubmitted >= 128U) {
                    return;
                }
                const auto isPrimitiveProxy = entity.meshRenderer.has_value()
                    && (entity.meshRenderer->primitiveInstanceIndex.has_value()
                        || entity.meshRenderer->editorInstanceIndex.has_value())
                    && !entity.meshRenderer->renderable;
                const auto selected = entity.id == selectedEntityId_;
                const auto showMarker = !entity.meshRenderer.has_value()
                    || selected
                    || (isPrimitiveProxy && !denseImportedHierarchy);
                const auto showLabel = selected
                    || entity.camera.has_value()
                    || entity.light.has_value()
                    || (!denseImportedHierarchy && !isPrimitiveProxy);
                if (!showMarker && !showLabel) {
                    return;
                }
                const auto position = entityWorldPosition(entity.id);
                if (!position.has_value()) {
                    return;
                }
                if (showMarker) {
                    detail::appendEntityMarker(
                        rendererGizmoVertices_,
                        rendererGizmoIndices_,
                        *position,
                        entityPickRadius(entity),
                        selected,
                        forward,
                        right,
                        up,
                        camera_.distance);
                }
                if (!showLabel) {
                    return;
                }
                appendViewportLabel(
                    rendererGizmoVertices_,
                    rendererGizmoIndices_,
                    labelCamera,
                    *position,
                    entity.name,
                    selected);
                ++labelsSubmitted;
            };
            if (denseImportedHierarchy) {
                if (selectedEntityId_.isValid()) {
                    if (const auto* selectedEntity = scene_->findEntity(selectedEntityId_)) {
                        appendEntityOverlay(*selectedEntity);
                    }
                }
            } else {
                for (const auto& entity : scene_->entities()) {
                    if (labelsSubmitted >= 128U) {
                        break;
                    }
                    appendEntityOverlay(entity);
                }
            }
        }
        const auto vertexCountBeforeGizmo = rendererGizmoVertices_.size();
        const auto indexCountBeforeGizmo = rendererGizmoIndices_.size();
        const auto gizmoVertexOffset = static_cast<std::uint32_t>(rendererGizmoVertices_.size());
        (void)updateGizmoFrame(false);
        const auto& gizmo = gizmoBackend_->mesh();
        rendererGizmoVertices_.reserve(rendererGizmoVertices_.size() + gizmo.vertices.size());
        rendererGizmoIndices_.reserve(rendererGizmoIndices_.size() + gizmo.triangles.size() * 3U);
        for (const auto& vertex : gizmo.vertices) {
            rendererGizmoVertices_.push_back({
                {vertex.position.x, vertex.position.y, vertex.position.z},
                vertex.color,
            });
        }
        for (const auto& triangle : gizmo.triangles) {
            if (triangle.x >= gizmo.vertices.size() || triangle.y >= gizmo.vertices.size() || triangle.z >= gizmo.vertices.size()) {
                continue;
            }
            rendererGizmoIndices_.insert(
                rendererGizmoIndices_.end(),
                {gizmoVertexOffset + triangle.x, gizmoVertexOffset + triangle.y, gizmoVertexOffset + triangle.z});
        }
        if (selectedEntityId_.isValid()
            && rendererGizmoVertices_.size() == vertexCountBeforeGizmo
            && rendererGizmoIndices_.size() == indexCountBeforeGizmo) {
            core::logWarning(
                core::LogCategory::Renderer,
                QStringLiteral("Selected viewport gizmo produced no Vulkan color geometry: vertices=%1 indices=%2")
                    .arg(static_cast<qulonglong>(gizmo.vertices.size()))
                    .arg(static_cast<qulonglong>(gizmo.triangles.size() * 3U))
                    .toStdString());
        }
    }
    if (lodDebugOverlayEnabled_ || shadowDebugOverlayEnabled_) {
        appendViewportDrawDebugOverlay(
            rendererGizmoVertices_,
            rendererGizmoIndices_,
            std::span<const renderer::RenderMeshDraw>(rendererMeshDraws_),
            std::span<const renderer::RenderMeshDraw>(rendererShadowMeshDraws_), frame,
            ViewportLabelCamera {
                eye, right, up, forward, cameraFrame.verticalFovRadians,
                static_cast<float>(std::max(height(), 1)),
            },
            static_cast<float>(std::max(width(), 1)),
            right,
            up,
            safeNormalized({editorSunLight_.direction[0], editorSunLight_.direction[1], editorSunLight_.direction[2]},
                {0.35F, -0.82F, 0.45F}),
            lodDebugOverlayEnabled_, shadowDebugOverlayEnabled_);
    }
    if (sourceObjectDebugOverlayEnabled_ && scene_ != nullptr && assetManager_ != nullptr) {
        appendViewportSourceObjectDebugOverlay(
            rendererGizmoVertices_,
            rendererGizmoIndices_,
            *scene_,
            *assetManager_,
            ViewportLabelCamera {
                eye, right, up, forward, cameraFrame.verticalFovRadians,
                static_cast<float>(std::max(height(), 1)),
            },
            static_cast<float>(std::max(width(), 1)),
            cameraFrame.aspectRatio,
            cameraFrame.nearPlane,
            cameraFrame.farPlane);
    }
    if (!rendererGizmoVertices_.empty() && !rendererGizmoIndices_.empty()) {
        rendererColorMeshDraws_.push_back({
            std::span<const renderer::RenderColorVertex>(rendererGizmoVertices_),
            std::span<const std::uint32_t>(rendererGizmoIndices_),
            viewProjection,
        });
    }
    frame.colorMeshDraws = std::span<const renderer::RenderColorMeshDraw>(rendererColorMeshDraws_);
    frame.editorBuildCpuTimeUs = elapsedUs(editorBuildStart);
    if (rendererMeshDraws_.empty() && rendererColorMeshDraws_.empty() && !hasMeshSceneContent) {
        return false;
    }
    std::string error;
    if (renderer_->renderSurfaceFrame(desc, frame, &error)) {
        lastRendererFrameTime_ = std::chrono::steady_clock::now();
        hasLastRendererFrameTime_ = true;
        lastRendererStats_ = renderer_->stats();
        gpuMeshFrameRendered_ = !rendererMeshDraws_.empty();
        return true;
    }
    gpuMeshFrameRendered_ = false;
    rendererSurfaceAttempted_ = false;
    rendererSurfaceReady_ = false;
    core::logWarning(
        core::LogCategory::Renderer,
        QStringLiteral("Viewport Vulkan frame unavailable: %1")
            .arg(QString::fromStdString(error.empty() ? "unknown error" : error))
            .toStdString());
    return false;
}
} // namespace projectunity::editor
