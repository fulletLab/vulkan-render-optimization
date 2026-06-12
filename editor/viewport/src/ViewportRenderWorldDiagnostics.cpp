#include "ViewportRenderWorldDiagnostics.hpp"

#include <projectunity/core/Log.hpp>
#include <projectunity/renderer/RendererTypes.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <sstream>

namespace projectunity::editor {
namespace {

struct VisibleDrawLogRow {
    bool overview {false};
    std::uint64_t sceneNodeId {0};
    std::uint64_t modelAssetId {0};
    std::uint64_t chunkId {0};
    std::uint64_t drawCount {0};
    std::uint64_t triangleCount {0};
    std::uint64_t lodDrawCount {0};
    std::uint64_t behindCenterCount {0};
    float nearestDepth {std::numeric_limits<float>::max()};
    float radius {0.0F};
    std::array<float, 3> center {0.0F, 0.0F, 0.0F};
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
    if (draw.primitive == nullptr) {
        return 0;
    }
    if (draw.lodIndex > 0U && draw.lodIndex - 1U < draw.primitive->lods.size()) {
        const auto& lodIndices = draw.primitive->lods[draw.lodIndex - 1U].indices;
        if (!lodIndices.empty()) {
            return static_cast<std::uint64_t>(lodIndices.size() / 3U);
        }
    }
    return static_cast<std::uint64_t>(draw.primitive->indices.size() / 3U);
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
    signature = mixHash(signature, stats.largestRenderChunkTriangleCount);
    signature = mixHash(signature, floatBits(stats.maxRenderChunkExtent));
    if (signature == lastDebugSignature) {
        return;
    }

    std::ostringstream message;
    message << "RenderWorld chunks total=" << stats.renderChunkCount
            << " visible=" << stats.visibleRenderChunkCount
            << " instances=" << stats.renderInstanceCount
            << "/" << stats.visibleRenderInstanceCount
            << " maxExtent=" << stats.maxRenderChunkExtent
            << " large=" << stats.largeRenderChunkCount
            << " top=";
    const auto topCount = std::min<std::size_t>(sortedRows.size(), 10U);
    for (std::size_t index = 0; index < topCount; ++index) {
        const auto& chunk = sortedRows[index];
        message << "[" << index
                << " tri=" << chunk.triangleCount
                << " inst=" << chunk.instanceCount
                << " extent=" << chunk.maxExtent
                << " vis=" << (chunk.visible ? "yes" : "no")
                << "]";
    }
    core::logInfo(core::LogCategory::Renderer, message.str());
    lastDebugSignature = signature;
}

void appendViewportVisibleDrawDiagnostics(
    std::ostringstream& message,
    const std::vector<renderer::RenderMeshDraw>& draws)
{
    if (draws.empty()) {
        message << " visibleTop=[]";
        return;
    }
    std::vector<VisibleDrawLogRow> rows;
    rows.reserve(std::min<std::size_t>(draws.size(), 64U));
    std::uint64_t overviewDraws = 0;
    std::uint64_t meshDraws = 0;
    std::uint64_t behindCenters = 0;
    for (const auto& draw : draws) {
        const auto modelId = draw.modelAssetId.isValid() ? draw.modelAssetId.value() : 0U;
        const auto overview = !draw.castsShadow;
        if (overview) {
            ++overviewDraws;
        } else {
            ++meshDraws;
        }
        if (draw.sortDepth < 0.0F) {
            ++behindCenters;
        }
        auto existing = std::find_if(rows.begin(), rows.end(), [&](const auto& row) {
            return row.overview == overview && row.sceneNodeId == draw.sceneNodeId && row.modelAssetId == modelId;
        });
        if (existing == rows.end()) {
            VisibleDrawLogRow row;
            row.overview = overview;
            row.sceneNodeId = draw.sceneNodeId;
            row.modelAssetId = modelId;
            row.chunkId = draw.renderChunkId;
            row.nearestDepth = draw.sortDepth;
            row.radius = draw.worldBoundsRadius;
            row.center = draw.worldBoundsCenter;
            existing = rows.insert(rows.end(), row);
        }
        ++existing->drawCount;
        existing->triangleCount += renderDrawTriangleCount(draw);
        existing->lodDrawCount += draw.lodIndex > 0U ? 1U : 0U;
        existing->behindCenterCount += draw.sortDepth < 0.0F ? 1U : 0U;
        existing->nearestDepth = std::min(existing->nearestDepth, draw.sortDepth);
        if (draw.worldBoundsRadius > existing->radius) {
            existing->radius = draw.worldBoundsRadius;
            existing->center = draw.worldBoundsCenter;
            existing->chunkId = draw.renderChunkId;
        }
    }
    std::sort(rows.begin(), rows.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.triangleCount != rhs.triangleCount) {
            return lhs.triangleCount > rhs.triangleCount;
        }
        return lhs.drawCount > rhs.drawCount;
    });
    message << " visibleSummary mesh=" << meshDraws
            << " overview=" << overviewDraws
            << " behindCenters=" << behindCenters
            << " visibleTop=";
    const auto topCount = std::min<std::size_t>(rows.size(), 8U);
    for (std::size_t index = 0; index < topCount; ++index) {
        const auto& row = rows[index];
        message << "[" << index
                << " kind=" << (row.overview ? "overview" : "mesh")
                << " node=" << row.sceneNodeId
                << " model=" << row.modelAssetId
                << " chunk=" << row.chunkId
                << " draws=" << row.drawCount
                << " tri=" << row.triangleCount
                << " lod=" << row.lodDrawCount
                << " behind=" << row.behindCenterCount
                << " depth=" << row.nearestDepth
                << " center=(" << row.center[0] << "," << row.center[1] << "," << row.center[2] << ")"
                << " r=" << row.radius
                << "]";
    }
}

} // namespace projectunity::editor
