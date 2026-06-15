#include "ViewportRenderWorldDiagnostics.hpp"

#include <projectunity/core/Log.hpp>
#include <projectunity/renderer/RendererTypes.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <sstream>

namespace projectunity::editor {
namespace {

struct ProjectedNdcRect {
    bool valid {false};
    bool nearClipped {false};
    float minimumX {std::numeric_limits<float>::max()};
    float minimumY {std::numeric_limits<float>::max()};
    float maximumX {std::numeric_limits<float>::lowest()};
    float maximumY {std::numeric_limits<float>::lowest()};
};

[[nodiscard]] std::uint64_t mixHash(std::uint64_t seed, std::uint64_t value) noexcept
{
    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U));
}

[[nodiscard]] std::uint64_t floatBits(float value) noexcept
{
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(value));
    return bits;
}

[[nodiscard]] std::uint64_t renderDrawTriangleCount(const renderer::RenderMeshDraw& draw) noexcept
{
    return renderer::renderMeshDrawTriangleCount(draw);
}

[[nodiscard]] const char* lodReasonText(renderer::RenderLodSelectionReason reason) noexcept
{
    switch (reason) {
    case renderer::RenderLodSelectionReason::FullResolution: return "full-resolution";
    case renderer::RenderLodSelectionReason::ScreenError: return "screen-error";
    case renderer::RenderLodSelectionReason::HysteresisHold: return "lod-hysteresis";
    case renderer::RenderLodSelectionReason::HlodScreenSize: return "hlod-screen-size";
    case renderer::RenderLodSelectionReason::HlodChunkBudget: return "hlod-chunk-budget";
    case renderer::RenderLodSelectionReason::HlodDrawBudget: return "hlod-draw-budget";
    case renderer::RenderLodSelectionReason::HlodDebugOverride: return "hlod-debug-override";
    case renderer::RenderLodSelectionReason::HlodHysteresisHold: return "hlod-hysteresis";
    case renderer::RenderLodSelectionReason::Unspecified: break;
    }
    return "unspecified";
}

[[nodiscard]] float matrixAt(const renderer::RenderMatrix4& matrix, int row, int column) noexcept
{
    return matrix.values[static_cast<std::size_t>(column * 4 + row)];
}

[[nodiscard]] ProjectedNdcRect projectDrawBounds(const renderer::RenderMeshDraw& draw) noexcept
{
    ProjectedNdcRect result;
    if (draw.primitive == nullptr) {
        return result;
    }
    const auto minimum = draw.primitive->bounds.minimum;
    const auto maximum = draw.primitive->bounds.maximum;
    for (std::uint32_t corner = 0; corner < 8U; ++corner) {
        const math::Vec3 point {
            (corner & 1U) != 0U ? maximum.x : minimum.x,
            (corner & 2U) != 0U ? maximum.y : minimum.y,
            (corner & 4U) != 0U ? maximum.z : minimum.z,
        };
        const auto clipX = matrixAt(draw.modelViewProjection, 0, 0) * point.x
            + matrixAt(draw.modelViewProjection, 0, 1) * point.y
            + matrixAt(draw.modelViewProjection, 0, 2) * point.z
            + matrixAt(draw.modelViewProjection, 0, 3);
        const auto clipY = matrixAt(draw.modelViewProjection, 1, 0) * point.x
            + matrixAt(draw.modelViewProjection, 1, 1) * point.y
            + matrixAt(draw.modelViewProjection, 1, 2) * point.z
            + matrixAt(draw.modelViewProjection, 1, 3);
        const auto clipW = matrixAt(draw.modelViewProjection, 3, 0) * point.x
            + matrixAt(draw.modelViewProjection, 3, 1) * point.y
            + matrixAt(draw.modelViewProjection, 3, 2) * point.z
            + matrixAt(draw.modelViewProjection, 3, 3);
        if (!std::isfinite(clipW) || clipW <= 0.0001F) {
            result.nearClipped = true;
            continue;
        }
        const auto ndcX = clipX / clipW;
        const auto ndcY = clipY / clipW;
        if (!std::isfinite(ndcX) || !std::isfinite(ndcY)) {
            continue;
        }
        result.minimumX = std::min(result.minimumX, ndcX);
        result.minimumY = std::min(result.minimumY, ndcY);
        result.maximumX = std::max(result.maximumX, ndcX);
        result.maximumY = std::max(result.maximumY, ndcY);
        result.valid = true;
    }
    return result;
}

} // namespace

[[nodiscard]] bool renderWorldChunkDiagnosticsEnabled() noexcept
{
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t length = 0;
    if (::_dupenv_s(&value, &length, "PROJECTUNITY_RENDERWORLD_DIAGNOSTICS") != 0 || value == nullptr) {
        return false;
    }
    const bool enabled = length > 1U && value[0] != '\0' && value[0] != '0';
    std::free(value);
    return enabled;
#else
    const auto* value = std::getenv("PROJECTUNITY_RENDERWORLD_DIAGNOSTICS");
    return value != nullptr && value[0] != '\0' && value[0] != '0';
#endif
}

float renderWorldChunkMaxExtent(const std::array<math::Vec3, 8>& corners) noexcept
{
    auto minimum = corners.front();
    auto maximum = corners.front();
    for (const auto corner : corners) {
        minimum.x = std::min(minimum.x, corner.x);
        minimum.y = std::min(minimum.y, corner.y);
        minimum.z = std::min(minimum.z, corner.z);
        maximum.x = std::max(maximum.x, corner.x);
        maximum.y = std::max(maximum.y, corner.y);
        maximum.z = std::max(maximum.z, corner.z);
    }
    return std::max({maximum.x - minimum.x, maximum.y - minimum.y, maximum.z - minimum.z});
}

void logRenderWorldChunkDiagnostics(
    const ViewportRenderWorldStats& stats,
    const std::vector<ViewportRenderWorldChunkLogRow>& rows,
    const std::vector<renderer::RenderMeshDraw>& draws,
    const ViewportRenderWorldCamera& camera,
    std::uint64_t& lastDebugSignature)
{
    if (rows.empty() || !renderWorldChunkDiagnosticsEnabled()) {
        return;
    }
    auto sortedRows = rows;
    std::sort(sortedRows.begin(), sortedRows.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.triangleCount != rhs.triangleCount) {
            return lhs.triangleCount > rhs.triangleCount;
        }
        return lhs.instanceCount > rhs.instanceCount;
    });
    auto signature = mixHash(stats.renderChunkCount, stats.visibleRenderChunkCount);
    signature = mixHash(signature, stats.renderInstanceCount);
    signature = mixHash(signature, stats.visibleRenderInstanceCount);
    signature = mixHash(signature, stats.finalDrawPacketCount);
    signature = mixHash(signature, stats.finalTriangleCount);
    signature = mixHash(signature, stats.hlodMeshDrawCount);
    signature = mixHash(signature, stats.hlodCollapsedChunkCount);
    signature = mixHash(signature, stats.largestRenderChunkTriangleCount);
    signature = mixHash(signature, floatBits(stats.maxRenderChunkExtent));
    signature = mixHash(signature, floatBits(camera.forward.x));
    signature = mixHash(signature, floatBits(camera.forward.y));
    signature = mixHash(signature, floatBits(camera.forward.z));
    if (signature == lastDebugSignature) {
        return;
    }

    std::ostringstream message;
    const auto cameraPitch = std::asin(std::clamp(camera.forward.normalized().y, -1.0F, 1.0F)) * 57.2957795F;
    message << "RenderWorld selectedAssetMode="
            << (stats.hlodMeshDrawCount > 0U ? "HLOD" : "DetailedChunks")
            << " chunks total=" << stats.renderChunkCount
            << " visibleChunksBeforeBudget=" << stats.visibleRenderChunkCount
            << " visibleChunksAfterBudget=" << stats.finalVisibleChunkCount
            << " collapsed=" << stats.hlodCollapsedChunkCount
            << " instances=" << stats.renderInstanceCount
            << "/" << stats.visibleRenderInstanceCount
            << " drawPackets=" << stats.finalDrawPacketCount
            << " triangles=" << stats.finalTriangleCount
            << " shadowCasters=" << stats.shadowCandidateInstances
            << " shadowRejectedByPolicy=" << stats.shadowPolicyRejectedInstances
            << " screenCoverage=" << stats.hlodScreenCoverage
            << " cameraDistance=" << stats.hlodCameraDistance
            << " cameraForward=(" << camera.forward.x << "," << camera.forward.y << "," << camera.forward.z << ")"
            << " cameraPitch=" << cameraPitch
            << " maxExtent=" << stats.maxRenderChunkExtent
            << " large=" << stats.largeRenderChunkCount
            << " top=";
    const auto topCount = std::min<std::size_t>(sortedRows.size(), 10U);
    for (std::size_t index = 0; index < topCount; ++index) {
        const auto& chunk = sortedRows[index];
        message << "[" << index
                << " node=" << chunk.sceneNodeId
                << " asset=" << chunk.modelAssetId
                << " chunk=" << chunk.chunkId
                << " tri=" << chunk.triangleCount
                << " inst=" << chunk.instanceCount
                << " extent=" << chunk.maxExtent
                << " bounds=("
                << chunk.boundsMinimum.x << "," << chunk.boundsMinimum.y << "," << chunk.boundsMinimum.z
                << ")-("
                << chunk.boundsMaximum.x << "," << chunk.boundsMaximum.y << "," << chunk.boundsMaximum.z
                << ")"
                << " distanceToCamera=" << chunk.distanceToCamera
                << " distanceToBounds=" << chunk.distanceToBounds
                << " projectedScreenSize=" << chunk.projectedRadiusPixels
                << " lod=" << chunk.lod0DrawCount << "/" << chunk.lod1DrawCount << "/" << chunk.lod2PlusDrawCount
                << " nearestInstance=" << chunk.nearestRenderInstanceId
                << " nearestDistance=" << chunk.nearestInstanceDistance
                << " nearestLod=" << chunk.nearestSelectedLod
                << " previousLod=" << chunk.nearestPreviousLod
                << " hysteresis=" << (chunk.nearestHysteresisActive ? "active" : "inactive")
                << " selectionReason=" << lodReasonText(chunk.nearestSelectionReason)
                << " insideRoot=" << (chunk.cameraInsideRootBounds ? "yes" : "no")
                << " insideChunk=" << (chunk.cameraInsideChunkBounds ? "yes" : "no")
                << " reason=" << (chunk.reason == nullptr ? "unknown" : chunk.reason)
                << " vis=" << (chunk.visible ? "yes" : "no")
                << "]";
    }
    appendViewportVisibleDrawDiagnostics(message, draws, camera);
    core::logInfo(core::LogCategory::Renderer, message.str());
    lastDebugSignature = signature;
}

void appendViewportVisibleDrawDiagnostics(
    std::ostringstream& message,
    const std::vector<renderer::RenderMeshDraw>& draws,
    const ViewportRenderWorldCamera& camera)
{
    if (draws.empty()) {
        message << " visibleTop=[]";
        return;
    }
    std::vector<const renderer::RenderMeshDraw*> rows;
    rows.reserve(draws.size());
    std::uint64_t overviewDraws = 0;
    std::uint64_t meshDraws = 0;
    std::uint64_t behindCenters = 0;
    for (const auto& draw : draws) {
        const auto overview = renderer::isOverviewRenderMeshDraw(draw);
        if (overview) {
            ++overviewDraws;
        } else {
            ++meshDraws;
        }
        if (draw.sortDepth < 0.0F) {
            ++behindCenters;
        }
        rows.push_back(&draw);
    }
    std::sort(rows.begin(), rows.end(), [](const auto* lhs, const auto* rhs) {
        const auto lhsTriangles = renderDrawTriangleCount(*lhs);
        const auto rhsTriangles = renderDrawTriangleCount(*rhs);
        if (lhsTriangles != rhsTriangles) {
            return lhsTriangles > rhsTriangles;
        }
        return lhs->renderInstanceId < rhs->renderInstanceId;
    });
    message << " visibleSummary mesh=" << meshDraws
            << " overview=" << overviewDraws
            << " behindCenters=" << behindCenters
            << " visibleTop=";
    const auto topCount = std::min<std::size_t>(rows.size(), 16U);
    const auto cameraPitch = std::asin(std::clamp(camera.forward.normalized().y, -1.0F, 1.0F)) * 57.2957795F;
    for (std::size_t index = 0; index < topCount; ++index) {
        const auto& draw = *rows[index];
        const auto ndcRect = projectDrawBounds(draw);
        message << "[" << index
                << " assetId=" << (draw.modelAssetId.isValid() ? draw.modelAssetId.value() : 0U)
                << " instanceId=" << draw.renderInstanceId
                << " chunkId=" << draw.renderChunkId
                << " node=" << draw.sceneNodeId
                << " triangles=" << renderDrawTriangleCount(draw)
                << " distanceToCamera=" << draw.distanceToCameraCenter
                << " distanceToBounds=" << draw.distanceToCameraBounds
                << " screenPx=" << draw.projectedRadiusPixels
                << " selectedLOD=" << draw.lodIndex
                << " selectedMesh=" << (renderer::isOverviewRenderMeshDraw(draw) ? "hlodProxy" : "mesh")
                << " overviewMesh=" << (renderer::isOverviewRenderMeshDraw(draw) ? "yes" : "no")
                << " reason=" << lodReasonText(draw.lodSelectionReason)
                << " cameraForward=(" << camera.forward.x << "," << camera.forward.y << "," << camera.forward.z << ")"
                << " cameraPitch=" << cameraPitch
                << " insideBounds=" << (draw.cameraInsideChunkBounds ? "yes" : "no")
                << " ndcRect=";
        if (ndcRect.valid) {
            message << "(" << ndcRect.minimumX << "," << ndcRect.minimumY
                    << ")-(" << ndcRect.maximumX << "," << ndcRect.maximumY << ")";
        } else {
            message << "invalid";
        }
        message << " nearClipped=" << (ndcRect.nearClipped ? "yes" : "no")
                << " hysteresis=" << (draw.lodHysteresisActive ? "active" : "inactive")
                << " previousLOD=" << draw.previousLodIndex
                << " projectedErrorPx=" << draw.projectedLodErrorPixels
                << "]";
    }
}

} // namespace projectunity::editor
