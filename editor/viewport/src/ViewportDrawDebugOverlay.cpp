#include "ViewportDrawDebugOverlay.hpp"

#include "ViewportRendererOverlays.hpp"

#include <projectunity/core/Log.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <unordered_map>

namespace projectunity::editor {
namespace {

enum class ShadowDebugState : std::uint8_t {
    None,
    Rejected,
    Active,
};

enum class LodDebugColor : std::uint8_t {
    Red,
    Yellow,
    Purple,
    Hlod,
};

[[nodiscard]] std::uint64_t mixLogHash(std::uint64_t seed, std::uint64_t value) noexcept
{
    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U));
}

struct LabelProjectionDebug {
    float dotDepth {0.0F};
    float screenX {0.0F};
    float screenY {0.0F};
    bool inFront {false};
    bool visible {false};
    bool labelDepthAccepted {false};
};

[[nodiscard]] LabelProjectionDebug projectLabelDebug(
    const ViewportLabelCamera& camera,
    float viewportWidthPixels,
    math::Vec3 anchor) noexcept
{
    LabelProjectionDebug result;
    const auto toAnchor = anchor - camera.eye;
    result.dotDepth = math::dot(toAnchor, camera.forward);
    result.inFront = std::isfinite(result.dotDepth) && result.dotDepth > 0.05F;
    result.labelDepthAccepted = result.inFront && result.dotDepth <= 320.0F;
    const auto tangent = std::tan(camera.verticalFovRadians * 0.5F);
    const auto viewportHeight = std::max(camera.viewportHeightPixels, 1.0F);
    const auto viewportWidth = std::max(viewportWidthPixels, 1.0F);
    if (!result.inFront || !std::isfinite(tangent) || tangent <= 0.0F) {
        return result;
    }
    const auto halfHeight = tangent * result.dotDepth;
    const auto halfWidth = halfHeight * (viewportWidth / viewportHeight);
    if (!std::isfinite(halfHeight) || !std::isfinite(halfWidth) || halfHeight <= 0.0F || halfWidth <= 0.0F) {
        return result;
    }
    const auto localX = math::dot(toAnchor, camera.right);
    const auto localY = math::dot(toAnchor, camera.up);
    const auto ndcX = localX / halfWidth;
    const auto ndcY = localY / halfHeight;
    result.screenX = (ndcX * 0.5F + 0.5F) * viewportWidth;
    result.screenY = (0.5F - ndcY * 0.5F) * viewportHeight;
    result.visible = std::isfinite(ndcX) && std::isfinite(ndcY)
        && ndcX >= -1.0F && ndcX <= 1.0F
        && ndcY >= -1.0F && ndcY <= 1.0F;
    return result;
}

[[nodiscard]] bool isOverviewDraw(const renderer::RenderMeshDraw& draw) noexcept
{
    return renderer::isOverviewRenderMeshDraw(draw);
}

[[nodiscard]] std::uint64_t sourceTriangleCount(const renderer::RenderMeshDraw& draw) noexcept
{
    return draw.primitive == nullptr ? 0U : static_cast<std::uint64_t>(draw.primitive->indices.size() / 3U);
}

[[nodiscard]] std::uint64_t selectedTriangleCount(const renderer::RenderMeshDraw& draw) noexcept
{
    return renderer::renderMeshDrawTriangleCount(draw);
}

[[nodiscard]] const char* hlodReasonText(const renderer::RenderFrame& frame) noexcept
{
    if (frame.hlodMeshDrawCount > 0U) {
        return "active";
    }
    if (frame.hlodRejectedNoOverviewCount > 0U) {
        return "none:no-overview-mesh";
    }
    if (frame.hlodCandidateDrawCount == 0U) {
        return "none:not-built";
    }
    if (frame.hlodRejectedVisibleWorkCount > 0U) {
        return "rejected:visible-work";
    }
    if (frame.hlodRejectedCoverageCount > 0U) {
        return "rejected:coverage";
    }
    if (frame.hlodRejectedScreenCount > 0U) {
        return "rejected:too-close/screen";
    }
    if (frame.hlodRejectedClusterScreenCount > 0U) {
        return "rejected:cluster-screen";
    }
    return "rejected:unknown";
}

[[nodiscard]] math::Vec3 drawCenter(const renderer::RenderMeshDraw& draw) noexcept
{
    return {draw.worldBoundsCenter[0], draw.worldBoundsCenter[1], draw.worldBoundsCenter[2]};
}

[[nodiscard]] float maxHalfExtent(std::array<float, 3> halfExtent) noexcept
{
    return std::max({std::fabs(halfExtent[0]), std::fabs(halfExtent[1]), std::fabs(halfExtent[2])});
}

[[nodiscard]] float drawDistanceToCenter(const renderer::RenderMeshDraw& draw, const ViewportLabelCamera& camera) noexcept
{
    if (draw.distanceToCameraCenter > 0.0F && std::isfinite(draw.distanceToCameraCenter)) {
        return draw.distanceToCameraCenter;
    }
    const auto distance = (drawCenter(draw) - camera.eye).length();
    return std::isfinite(distance) ? distance : 0.0F;
}

[[nodiscard]] float drawDistanceToBounds(const renderer::RenderMeshDraw& draw, float distanceToCenter) noexcept
{
    if ((draw.distanceToCameraBounds > 0.0F || draw.cameraInsideChunkBounds)
        && std::isfinite(draw.distanceToCameraBounds)) {
        return draw.distanceToCameraBounds;
    }
    return std::max(distanceToCenter - std::max(draw.worldBoundsRadius, 0.0F), 0.0F);
}

[[nodiscard]] float drawProjectedRadiusPixels(
    const renderer::RenderMeshDraw& draw,
    float verticalFovRadians,
    float viewportHeight,
    float distanceToCenter) noexcept
{
    if (draw.projectedRadiusPixels > 0.0F && std::isfinite(draw.projectedRadiusPixels)) {
        return draw.projectedRadiusPixels;
    }
    if (draw.worldBoundsRadius <= 0.0F || distanceToCenter <= 0.05F || viewportHeight <= 0.0F) {
        return 0.0F;
    }
    const auto projectionScale = (viewportHeight * 0.5F) / std::max(std::tan(verticalFovRadians * 0.5F), 0.001F);
    const auto projected = draw.worldBoundsRadius * projectionScale / std::max(distanceToCenter, 0.05F);
    return std::isfinite(projected) ? projected : 0.0F;
}

[[nodiscard]] LodDebugColor lodDebugColorClass(
    const renderer::RenderMeshDraw& draw,
    const ViewportLabelCamera& camera) noexcept
{
    if (isOverviewDraw(draw)) {
        return LodDebugColor::Hlod;
    }
    const auto distanceToCenter = drawDistanceToCenter(draw, camera);
    const auto extent = maxHalfExtent(draw.worldBoundsHalfExtent);
    const auto closeDistance = std::clamp(extent * 2.0F, 8.0F, 32.0F);
    const auto mediumDistance = std::clamp(extent * 8.0F, 48.0F, 180.0F);
    if (distanceToCenter <= closeDistance) {
        return LodDebugColor::Red;
    }
    if (distanceToCenter <= mediumDistance) {
        return LodDebugColor::Yellow;
    }
    return LodDebugColor::Purple;
}

[[nodiscard]] std::array<float, 4> lodDebugLineColor(LodDebugColor color) noexcept
{
    switch (color) {
    case LodDebugColor::Red:
        return {1.0F, 0.18F, 0.08F, 0.78F};
    case LodDebugColor::Yellow:
        return {1.0F, 0.78F, 0.08F, 0.76F};
    case LodDebugColor::Purple:
        return {0.58F, 0.27F, 1.0F, 0.82F};
    case LodDebugColor::Hlod:
        return {0.78F, 0.25F, 1.0F, 0.88F};
    }
    return {0.58F, 0.27F, 1.0F, 0.82F};
}

[[nodiscard]] const char* lodDebugColorName(LodDebugColor color) noexcept
{
    switch (color) {
    case LodDebugColor::Red:
        return "red";
    case LodDebugColor::Yellow:
        return "yellow";
    case LodDebugColor::Purple:
        return "purple";
    case LodDebugColor::Hlod:
        return "hlod";
    }
    return "unknown";
}

[[nodiscard]] std::string compactTriangleText(std::uint64_t triangles)
{
    std::ostringstream text;
    if (triangles >= 1'000'000ULL) {
        text << (triangles / 1'000'000ULL) << "M";
    } else if (triangles >= 1'000ULL) {
        text << (triangles / 1'000ULL) << "K";
    } else {
        text << triangles;
    }
    return text.str();
}

[[nodiscard]] std::uint32_t estimatedCascadeIndex(const renderer::RenderFrame& frame, float depth) noexcept
{
    if (!frame.shadowsEnabled || frame.shadowMode != renderer::RenderShadowMode::DirectionalCascades) {
        return 0U;
    }
    const auto count = std::clamp<std::uint32_t>(frame.shadowCascadeCount == 0U ? 1U : frame.shadowCascadeCount, 1U, 4U);
    for (std::uint32_t index = 0; index < count; ++index) {
        if (depth <= frame.shadowCascadeSplits[index]) {
            return index;
        }
    }
    return count - 1U;
}

} // namespace

void appendViewportDrawDebugOverlay(
    std::vector<renderer::RenderColorVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    std::span<const renderer::RenderMeshDraw> meshDraws,
    std::span<const renderer::RenderMeshDraw> shadowDraws,
    const renderer::RenderFrame& frame,
    const ViewportLabelCamera& labelCamera,
    float viewportWidthPixels,
    math::Vec3 cameraRight,
    math::Vec3 cameraUp,
    math::Vec3 sunDirection,
    bool lodDebugEnabled,
    bool shadowDebugEnabled)
{
    if ((!lodDebugEnabled && !shadowDebugEnabled) || meshDraws.empty()) {
        return;
    }

    std::unordered_map<std::uint64_t, ShadowDebugState> shadowByInstance;
    shadowByInstance.reserve(shadowDraws.size());
    for (const auto& draw : shadowDraws) {
        if (draw.renderInstanceId == 0U) {
            continue;
        }
        const auto state = draw.castsShadow ? ShadowDebugState::Active : ShadowDebugState::Rejected;
        auto& stored = shadowByInstance[draw.renderInstanceId];
        if (state == ShadowDebugState::Active || stored == ShadowDebugState::None) {
            stored = state;
        }
    }

    struct DebugDrawRow {
        const renderer::RenderMeshDraw* draw {nullptr};
        ShadowDebugState shadowState {ShadowDebugState::None};
        std::uint64_t selectedTriangles {0};
        std::uint64_t sourceTriangles {0};
        LodDebugColor debugColor {LodDebugColor::Purple};
        float distanceToCenter {0.0F};
        float distanceToBounds {0.0F};
        float projectedRadius {0.0F};
        float score {0.0F};
    };
    std::vector<DebugDrawRow> rows;
    rows.reserve(meshDraws.size());
    for (const auto& draw : meshDraws) {
        const auto selectedTriangles = selectedTriangleCount(draw);
        const auto sourceTriangles = sourceTriangleCount(draw);
        const auto shadowIt = shadowByInstance.find(draw.renderInstanceId);
        const auto shadowState = shadowIt == shadowByInstance.end() ? ShadowDebugState::None : shadowIt->second;
        const auto shadowRisk = draw.flipsWinding || (draw.material != nullptr && draw.material->doubleSided);
        const auto distanceToCenter = drawDistanceToCenter(draw, labelCamera);
        const auto distanceToBounds = drawDistanceToBounds(draw, distanceToCenter);
        const auto projectedRadius = drawProjectedRadiusPixels(
            draw,
            labelCamera.verticalFovRadians,
            labelCamera.viewportHeightPixels,
            distanceToCenter);
        const auto debugColor = lodDebugColorClass(draw, labelCamera);
        auto score = static_cast<float>(selectedTriangles) * 0.001F + projectedRadius * 12.0F;
        if (shadowState == ShadowDebugState::Active) {
            score += 5000.0F;
        } else if (shadowState == ShadowDebugState::Rejected) {
            score += 1500.0F;
        }
        if (shadowRisk) {
            score += 2500.0F;
        }
        if (draw.lodIndex == 0U && sourceTriangles >= 8000U && draw.sortDepth > draw.worldBoundsRadius * 10.0F) {
            score += 1000.0F;
        }
        if (isOverviewDraw(draw)) {
            score += 2200.0F;
        }
        rows.push_back({&draw, shadowState, selectedTriangles, sourceTriangles, debugColor, distanceToCenter, distanceToBounds, projectedRadius, score});
    }

    if (lodDebugEnabled) {
        std::uint64_t hlodDraws = 0;
        std::uint64_t l0Draws = 0;
        std::uint64_t l1Draws = 0;
        std::uint64_t l2PlusDraws = 0;
        std::uint64_t hlodTriangles = 0;
        std::uint64_t l0Triangles = 0;
        std::uint64_t l1Triangles = 0;
        std::uint64_t l2PlusTriangles = 0;
        std::uint64_t redDraws = 0;
        std::uint64_t yellowDraws = 0;
        std::uint64_t purpleDraws = 0;
        for (const auto& row : rows) {
            const auto& draw = *row.draw;
            if (isOverviewDraw(draw)) {
                ++hlodDraws;
                hlodTriangles += row.selectedTriangles;
            } else if (draw.lodIndex == 0U) {
                ++l0Draws;
                l0Triangles += row.selectedTriangles;
            } else if (draw.lodIndex == 1U) {
                ++l1Draws;
                l1Triangles += row.selectedTriangles;
            } else {
                ++l2PlusDraws;
                l2PlusTriangles += row.selectedTriangles;
            }
            if (row.debugColor == LodDebugColor::Red) {
                ++redDraws;
            } else if (row.debugColor == LodDebugColor::Yellow) {
                ++yellowDraws;
            } else if (row.debugColor == LodDebugColor::Purple) {
                ++purpleDraws;
            }
        }
        std::ostringstream summary;
        summary << "LOD DEBUG  HLOD " << hlodDraws << "/" << compactTriangleText(hlodTriangles)
                << " (" << hlodReasonText(frame) << " " << frame.hlodCandidateDrawCount << ")"
                << "  L0 " << l0Draws << "/" << compactTriangleText(l0Triangles)
                << "  L1 " << l1Draws << "/" << compactTriangleText(l1Triangles)
                << "  L2+ " << l2PlusDraws << "/" << compactTriangleText(l2PlusTriangles)
                << "  VIS R/Y/P " << redDraws << "/" << yellowDraws << "/" << purpleDraws;
        appendViewportScreenLabel(
            vertices,
            indices,
            labelCamera,
            viewportWidthPixels,
            14.0F,
            72.0F,
            summary.str(),
            false);
    }
    if (shadowDebugEnabled) {
        appendViewportScreenLabel(
            vertices,
            indices,
            labelCamera,
            viewportWidthPixels,
            14.0F,
            lodDebugEnabled ? 96.0F : 72.0F,
            "SHADOW MARKS  SH1 YELLOW  DBL AMBER  FLP RED",
            true);
    }

    constexpr std::size_t kMaxMarkers = 1800U;
    const auto markerStride = rows.size() > kMaxMarkers
        ? (rows.size() + kMaxMarkers - 1U) / kMaxMarkers
        : 1U;
    std::size_t markersDrawn = 0;
    for (std::size_t index = 0; index < rows.size() && markersDrawn < kMaxMarkers; index += markerStride) {
        const auto& row = rows[index];
        const auto& draw = *row.draw;
        const auto center = drawCenter(draw);
        const auto shadowRisk = draw.flipsWinding || (draw.material != nullptr && draw.material->doubleSided);
        const auto markerSize = std::clamp(draw.worldBoundsRadius * 0.08F, 0.08F, 0.55F);
        if (lodDebugEnabled) {
            detail::appendLineQuad(
                vertices,
                indices,
                center - cameraRight * markerSize,
                center + cameraRight * markerSize,
                lodDebugLineColor(row.debugColor),
                1.35F,
                labelCamera.forward,
                cameraRight,
                std::max(draw.sortDepth, 1.0F));
            detail::appendLineQuad(
                vertices,
                indices,
                center - cameraUp * markerSize,
                center + cameraUp * markerSize,
                lodDebugLineColor(row.debugColor),
                1.35F,
                labelCamera.forward,
                cameraRight,
                std::max(draw.sortDepth, 1.0F));
        }
        if (shadowDebugEnabled && row.shadowState != ShadowDebugState::None) {
            const auto color = row.shadowState == ShadowDebugState::Active
                ? std::array<float, 4> {1.0F, 0.86F, 0.12F, 0.96F}
                : std::array<float, 4> {1.0F, 0.48F, 0.08F, 0.80F};
            const auto length = markerSize * 2.4F;
            detail::appendLineQuad(
                vertices,
                indices,
                center,
                center + sunDirection * length,
                color,
                1.75F,
                labelCamera.forward,
                cameraRight,
                std::max(draw.sortDepth, 1.0F));
        }
        if (shadowDebugEnabled && shadowRisk) {
            const auto riskColor = draw.flipsWinding
                ? std::array<float, 4> {1.0F, 0.05F, 0.03F, 0.92F}
                : std::array<float, 4> {1.0F, 0.56F, 0.08F, 0.72F};
            detail::appendLineQuad(
                vertices,
                indices,
                center - cameraRight * (markerSize * 0.65F),
                center + cameraRight * (markerSize * 0.65F),
                riskColor,
                1.65F,
                labelCamera.forward,
                cameraRight,
                std::max(draw.sortDepth, 1.0F));
        }
        ++markersDrawn;
    }

    std::vector<DebugDrawRow> labels = rows;
    std::sort(labels.begin(), labels.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.score > rhs.score;
    });
    const auto labelCount = std::min<std::size_t>(labels.size(), 10U);
    if (lodDebugEnabled) {
        static std::uint64_t frameCounter = 0;
        static std::uint64_t lastSignature = 0;
        ++frameCounter;
        auto signature = mixLogHash(static_cast<std::uint64_t>(rows.size()), static_cast<std::uint64_t>(labelCount));
        for (std::size_t index = 0; index < labelCount; ++index) {
            const auto& draw = *labels[index].draw;
            const auto center = drawCenter(draw);
            const auto projection = projectLabelDebug(labelCamera, viewportWidthPixels, center);
            signature = mixLogHash(signature, draw.renderInstanceId);
            signature = mixLogHash(signature, draw.renderChunkId);
            signature = mixLogHash(signature, static_cast<std::uint64_t>(draw.lodIndex));
            signature = mixLogHash(signature, static_cast<std::uint64_t>(labels[index].debugColor));
            signature = mixLogHash(signature, static_cast<std::uint64_t>(std::max(projection.dotDepth, 0.0F) * 100.0F));
            signature = mixLogHash(signature, static_cast<std::uint64_t>(std::max(draw.sortDepth, 0.0F) * 100.0F));
        }
        if (frameCounter == 1U || frameCounter % 30U == 0U || signature != lastSignature) {
            lastSignature = signature;
            std::ostringstream message;
            message << "LOD label projection camera eye=(" << labelCamera.eye.x << "," << labelCamera.eye.y << "," << labelCamera.eye.z << ")"
                    << " forward=(" << labelCamera.forward.x << "," << labelCamera.forward.y << "," << labelCamera.forward.z << ")"
                    << " right=(" << labelCamera.right.x << "," << labelCamera.right.y << "," << labelCamera.right.z << ")"
                    << " up=(" << labelCamera.up.x << "," << labelCamera.up.y << "," << labelCamera.up.z << ")"
                    << " fov=" << labelCamera.verticalFovRadians
                    << " viewport=(" << viewportWidthPixels << "," << labelCamera.viewportHeightPixels << ")"
                    << " draws=" << rows.size()
                    << " labels=" << labelCount;
            for (std::size_t index = 0; index < labelCount; ++index) {
                const auto& row = labels[index];
                const auto& draw = *row.draw;
                const auto center = drawCenter(draw);
                const auto projection = projectLabelDebug(labelCamera, viewportWidthPixels, center);
                message << " [" << index
                        << " kind=" << (isOverviewDraw(draw) ? "HLOD" : "LOD")
                        << " lod=" << draw.lodIndex
                        << " debugColor=" << lodDebugColorName(row.debugColor)
                        << " sortDepth=" << draw.sortDepth
                        << " distanceToCenter=" << row.distanceToCenter
                        << " distanceToBounds=" << row.distanceToBounds
                        << " screenPx=" << row.projectedRadius
                        << " dotDepth=" << projection.dotDepth
                        << " screen=(" << projection.screenX << "," << projection.screenY << ")"
                        << " inFront=" << (projection.inFront ? 1 : 0)
                        << " visible=" << (projection.visible ? 1 : 0)
                        << " labelDepth=" << (projection.labelDepthAccepted ? 1 : 0)
                        << " center=(" << center.x << "," << center.y << "," << center.z << ")"
                        << " r=" << draw.worldBoundsRadius
                        << " chunkHalfExtent=(" << draw.worldBoundsHalfExtent[0] << "," << draw.worldBoundsHalfExtent[1] << "," << draw.worldBoundsHalfExtent[2] << ")"
                        << " rootHalfExtent=(" << draw.rootBoundsHalfExtent[0] << "," << draw.rootBoundsHalfExtent[1] << "," << draw.rootBoundsHalfExtent[2] << ")"
                        << " insideRoot=" << (draw.cameraInsideRootBounds ? 1 : 0)
                        << " insideChunk=" << (draw.cameraInsideChunkBounds ? 1 : 0)
                        << " tri=" << row.selectedTriangles << "/" << row.sourceTriangles
                        << " chunk=" << draw.renderChunkId
                        << "]";
            }
            message << " redDraws=";
            for (const auto& row : rows) {
                if (row.debugColor != LodDebugColor::Red || row.draw == nullptr) {
                    continue;
                }
                const auto& draw = *row.draw;
                const auto center = drawCenter(draw);
                message << "["
                        << "chunkId=" << draw.renderChunkId
                        << " chunkWorldCenter=(" << center.x << "," << center.y << "," << center.z << ")"
                        << " chunkWorldHalfExtent=(" << draw.worldBoundsHalfExtent[0] << "," << draw.worldBoundsHalfExtent[1] << "," << draw.worldBoundsHalfExtent[2] << ")"
                        << " distanceToCenter=" << row.distanceToCenter
                        << " distanceToBounds=" << row.distanceToBounds
                        << " screenPx=" << row.projectedRadius
                        << " selectedLOD=" << draw.lodIndex
                        << " debugColor=" << lodDebugColorName(row.debugColor)
                        << " insideRoot=" << (draw.cameraInsideRootBounds ? 1 : 0)
                        << " insideChunk=" << (draw.cameraInsideChunkBounds ? 1 : 0)
                        << " rootHalfExtent=(" << draw.rootBoundsHalfExtent[0] << "," << draw.rootBoundsHalfExtent[1] << "," << draw.rootBoundsHalfExtent[2] << ")"
                        << "]";
            }
            core::logInfo(core::LogCategory::Renderer, message.str());
        }
    }
    for (std::size_t index = 0; index < labelCount; ++index) {
        const auto& row = labels[index];
        const auto& draw = *row.draw;
        const auto center = drawCenter(draw);
        const auto shadowRisk = draw.flipsWinding || (draw.material != nullptr && draw.material->doubleSided);
        const auto cascade = estimatedCascadeIndex(frame, draw.sortDepth);
        const auto shadowText = row.shadowState == ShadowDebugState::Active
            ? "SH1"
            : (row.shadowState == ShadowDebugState::Rejected ? "SHR" : "SH0");
        std::ostringstream label;
        if (isOverviewDraw(draw)) {
            label << "HLOD ";
        }
        label << (row.debugColor == LodDebugColor::Red ? "R " : (row.debugColor == LodDebugColor::Yellow ? "Y " : "P "))
              << "D" << static_cast<int>(std::max(row.distanceToCenter, 0.0F))
              << " L" << draw.lodIndex
              << " T" << compactTriangleText(row.selectedTriangles)
              << "/" << compactTriangleText(row.sourceTriangles)
              << " " << shadowText
              << " C" << cascade;
        if (shadowRisk) {
            label << (draw.flipsWinding ? " FLP" : " DBL");
        }
        appendViewportLabel(
            vertices,
            indices,
            labelCamera,
            center + cameraUp * std::clamp(draw.worldBoundsRadius * 0.2F, 0.35F, 2.0F),
            label.str(),
            shadowRisk || row.shadowState == ShadowDebugState::Active);
    }
}

} // namespace projectunity::editor
