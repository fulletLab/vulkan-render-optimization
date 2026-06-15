#pragma once

#include "ViewportMeshLod.hpp"
#include "ViewportRenderWorld.hpp"
#include "ViewportRendererCulling.hpp"
#include "ViewportRenderWorldSettings.hpp"

#include <projectunity/math/Vec3.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace projectunity::editor {

enum class ViewportHlodReason : std::uint8_t {
    None,
    ScreenSize,
    ChunkBudget,
    DrawBudget,
    DebugOverride,
    Hysteresis,
};

struct ViewportHlodEvaluation {
    float distance {0.0F};
    float distanceToCenter {0.0F};
    float projectedRadiusPixels {0.0F};
    float screenCoverage {0.0F};
    bool insideBounds {false};
};

[[nodiscard]] inline ViewportHlodEvaluation evaluateViewportHlod(
    const ViewportWorldBounds& bounds,
    const ViewportRenderWorldCamera& camera,
    int viewportHeight) noexcept
{
    ViewportHlodEvaluation result;
    result.distanceToCenter = (bounds.center - camera.eye).length();
    result.insideBounds = std::isfinite(result.distanceToCenter)
        && result.distanceToCenter <= bounds.radius;
    result.distance = std::isfinite(result.distanceToCenter)
        ? std::max(result.distanceToCenter - bounds.radius, 0.0F)
        : 0.0F;
    if (bounds.radius <= 0.0F || viewportHeight <= 0 || result.distanceToCenter <= 0.05F) {
        return result;
    }
    const auto tangent = std::tan(camera.verticalFovRadians * 0.5F);
    if (!std::isfinite(tangent) || tangent <= 0.0F) {
        return result;
    }
    const auto projectionScale = (static_cast<float>(viewportHeight) * 0.5F) / tangent;
    const auto tangentDistance = result.insideBounds
        ? camera.nearPlane
        : std::sqrt(std::max(
            result.distanceToCenter * result.distanceToCenter - bounds.radius * bounds.radius,
            camera.nearPlane * camera.nearPlane));
    result.projectedRadiusPixels = bounds.radius * projectionScale / std::max(tangentDistance, camera.nearPlane);
    if (!std::isfinite(result.projectedRadiusPixels)) {
        result.projectedRadiusPixels = 0.0F;
    }
    result.screenCoverage = result.projectedRadiusPixels / static_cast<float>(viewportHeight);
    return result;
}

[[nodiscard]] inline bool viewportRootHlodEligible(
    const ViewportHlodEvaluation& evaluation,
    const ViewportAssetLodSettings& settings,
    bool overChunkBudget,
    bool overDrawBudget,
    bool wasHlodActive,
    ViewportHlodReason& reason) noexcept
{
    reason = ViewportHlodReason::None;
    if (settings.debugOverride == ViewportHlodDebugOverride::ForceDetailed) {
        return false;
    }
    if (settings.debugOverride == ViewportHlodDebugOverride::ForceHlod) {
        reason = ViewportHlodReason::DebugOverride;
        return true;
    }
    if (evaluation.insideBounds) {
        return false;
    }
    const auto hysteresis = settings.hlodHysteresisRatio;
    const auto distanceScale = wasHlodActive ? 1.0F - hysteresis : 1.0F + hysteresis;
    const auto coverageScale = wasHlodActive ? 1.0F + hysteresis : 1.0F - hysteresis;
    const auto rootCollapseDistance = std::max(settings.chunkCollapseDistance, settings.hlodDistanceThreshold) * distanceScale;
    if (evaluation.distance >= rootCollapseDistance && overChunkBudget) {
        reason = ViewportHlodReason::ChunkBudget;
        return true;
    }
    if (evaluation.distance >= rootCollapseDistance && overDrawBudget) {
        reason = ViewportHlodReason::DrawBudget;
        return true;
    }
    if (evaluation.distance >= settings.hlodDistanceThreshold * distanceScale
        && evaluation.screenCoverage > 0.0F
        && evaluation.screenCoverage <= settings.hlodScreenSizeThreshold * coverageScale) {
        reason = wasHlodActive ? ViewportHlodReason::Hysteresis : ViewportHlodReason::ScreenSize;
        return true;
    }
    return false;
}

[[nodiscard]] inline bool viewportClusterHlodEligible(
    const ViewportHlodEvaluation& evaluation,
    const ViewportAssetLodSettings& settings,
    bool assetOverBudget,
    bool wasHlodActive,
    ViewportHlodReason& reason) noexcept
{
    reason = ViewportHlodReason::None;
    if (settings.debugOverride == ViewportHlodDebugOverride::ForceDetailed) {
        return false;
    }
    if (settings.debugOverride == ViewportHlodDebugOverride::ForceHlod) {
        reason = ViewportHlodReason::DebugOverride;
        return true;
    }
    if (evaluation.insideBounds) {
        return false;
    }
    const auto hysteresis = settings.hlodHysteresisRatio;
    const auto distanceScale = wasHlodActive ? 1.0F - hysteresis : 1.0F + hysteresis;
    const auto coverageScale = wasHlodActive ? 1.0F + hysteresis : 1.0F - hysteresis;
    const auto coverageThreshold = settings.hlodScreenSizeThreshold * (assetOverBudget ? 0.75F : 0.45F);
    const auto eligible = evaluation.distance >= settings.chunkCollapseDistance * distanceScale
        && evaluation.screenCoverage > 0.0F
        && evaluation.screenCoverage <= coverageThreshold * coverageScale;
    if (eligible) {
        reason = wasHlodActive ? ViewportHlodReason::Hysteresis : ViewportHlodReason::ScreenSize;
    }
    return eligible;
}

[[nodiscard]] inline const char* viewportHlodReasonText(ViewportHlodReason reason) noexcept
{
    switch (reason) {
    case ViewportHlodReason::ScreenSize:
        return "screen-size";
    case ViewportHlodReason::ChunkBudget:
        return "chunk-budget";
    case ViewportHlodReason::DrawBudget:
        return "draw-budget";
    case ViewportHlodReason::DebugOverride:
        return "debug-override";
    case ViewportHlodReason::Hysteresis:
        return "hysteresis-hold";
    case ViewportHlodReason::None:
        break;
    }
    return "inactive";
}

} // namespace projectunity::editor
