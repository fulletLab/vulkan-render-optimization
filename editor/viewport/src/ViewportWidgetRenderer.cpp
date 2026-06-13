#include <projectunity/editor/ViewportWidget.hpp>
#include "ViewportLabelGeometry.hpp"
#include "ViewportRenderWorld.hpp"
#include "ViewportRenderWorldDiagnostics.hpp"
#include "ViewportRendererOverlays.hpp"
#include "ViewportShadowFocus.hpp"
#include <projectunity/core/Log.hpp>
#include <projectunity/renderer/IRenderer.hpp>
#include <projectunity/renderer/RenderShadowSetup.hpp>
#include <QString>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <optional>
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
    desc.vsync = true;
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
    if (!ensureRendererSurface() || renderer_ == nullptr || rendererSurfaceHandle_ == nullptr) {
        return false;
    }
    renderer::ViewportRenderSurfaceDesc desc;
    desc.nativeWindowHandle = rendererSurfaceHandle_;
    desc.width = static_cast<std::uint32_t>(rendererSurfaceWidth_);
    desc.height = static_cast<std::uint32_t>(rendererSurfaceHeight_);
    desc.vsync = true;
    renderer::RenderFrame frame;
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
                safeNormalized(math::cross(cameraFrame.forward, cameraFrame.up), cameraFrame.right));
            cameraFrame.up = safeNormalized(math::cross(cameraFrame.right, cameraFrame.forward), cameraFrame.up);
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
                safeNormalized(math::cross(cameraFrame.forward, cameraFrame.up), cameraFrame.right));
            cameraFrame.up = safeNormalized(math::cross(cameraFrame.right, cameraFrame.forward), cameraFrame.up);
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
        const auto renderWorldFrame = renderWorld_->buildFrame(
            scene_,
            assetManager_,
            selectedEntityId_,
            renderWorldCamera,
            viewProjection,
            height(),
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
        frame.occlusionTestedChunkCount = renderWorldFrame.stats.occlusionTestedChunkCount;
        frame.occlusionRejectedChunkCount = renderWorldFrame.stats.occlusionRejectedChunkCount;
        frame.occlusionOccluderChunkCount = renderWorldFrame.stats.occlusionOccluderChunkCount;
        frame.occlusionRejectedInstanceCount = renderWorldFrame.stats.occlusionRejectedInstanceCount;
        frame.occlusionRejectedTriangleCount = renderWorldFrame.stats.occlusionRejectedTriangleCount;
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
                appendViewportVisibleDrawDiagnostics(message, rendererMeshDraws_);
                core::logInfo(core::LogCategory::Renderer, message.str());
                lastCullingLogFrame_ = cullingLogFrameCounter_;
                lastCullingLogSignature_ = cullingSignature;
            }
        }
        renderWorldDebugChunks = renderWorldFrame.debugChunks;
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
                selectedEntityId_,
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
    frame.shadowOnlyCandidateInstances = shadowStats.shadowOnlyCandidateInstances; frame.shadowOnlyRejectedInstances = shadowStats.shadowOnlyRejectedInstances;
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
        {
            const auto sunDirection = safeNormalized(
                {editorSunLight_.direction[0], editorSunLight_.direction[1], editorSunLight_.direction[2]},
                {0.35F, -0.82F, 0.45F});
            const auto anchor = camera_.target;
            const auto rayLength = std::clamp(camera_.distance * 0.65F, 4.0F, 28.0F);
            const auto headLength = std::clamp(rayLength * 0.12F, 0.55F, 2.2F);
            const auto rayStart = anchor - sunDirection * rayLength;
            constexpr std::array<float, 4> sunColor {1.0F, 0.82F, 0.22F, 0.95F};
            detail::appendLineQuad(
                rendererGizmoVertices_,
                rendererGizmoIndices_,
                rayStart,
                anchor,
                sunColor,
                2.2F,
                forward,
                right,
                camera_.distance);
            detail::appendLineQuad(
                rendererGizmoVertices_,
                rendererGizmoIndices_,
                anchor,
                anchor - sunDirection * headLength + right * (headLength * 0.45F),
                sunColor,
                1.8F,
                forward,
                right,
                camera_.distance);
            detail::appendLineQuad(
                rendererGizmoVertices_,
                rendererGizmoIndices_,
                anchor,
                anchor - sunDirection * headLength - right * (headLength * 0.45F),
                sunColor,
                1.8F,
                forward,
                right,
                camera_.distance);
            detail::appendLineQuad(
                rendererGizmoVertices_,
                rendererGizmoIndices_,
                rayStart - right * (headLength * 0.35F),
                rayStart + right * (headLength * 0.35F),
                sunColor,
                1.6F,
                forward,
                right,
                camera_.distance);
            detail::appendLineQuad(
                rendererGizmoVertices_,
                rendererGizmoIndices_,
                rayStart - up * (headLength * 0.35F),
                rayStart + up * (headLength * 0.35F),
                sunColor,
                1.6F,
                forward,
                right,
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
