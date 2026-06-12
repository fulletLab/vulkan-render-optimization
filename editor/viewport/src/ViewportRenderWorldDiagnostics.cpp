#include "ViewportRenderWorldDiagnostics.hpp"

#include <projectunity/core/Log.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>

namespace projectunity::editor {
namespace {

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
    struct ModelSummary {
        std::uint64_t chunks {0};
        std::uint64_t visible {0};
        std::uint64_t selected {0};
        std::uint64_t occluders {0};
        std::uint64_t occlusionRejected {0};
        std::uint64_t triangles {0};
        std::uint64_t rejectedTriangles {0};
    };
    std::map<std::string, ModelSummary> modelSummaries;
    for (const auto& row : rows) {
        const auto modelName = row.modelName.empty() ? std::string {"<unknown>"} : row.modelName;
        auto& summary = modelSummaries[modelName];
        ++summary.chunks;
        summary.triangles += row.triangleCount;
        if (row.visible) {
            ++summary.visible;
        }
        if (row.selected) {
            ++summary.selected;
        }
        if (row.occluder) {
            ++summary.occluders;
        }
        if (row.occlusionRejected) {
            ++summary.occlusionRejected;
            summary.rejectedTriangles += row.triangleCount;
        }
    }
    std::vector<std::pair<std::string, ModelSummary>> sortedModels(
        modelSummaries.begin(),
        modelSummaries.end());
    std::sort(sortedModels.begin(), sortedModels.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.second.triangles != rhs.second.triangles) {
            return lhs.second.triangles > rhs.second.triangles;
        }
        return lhs.second.chunks > rhs.second.chunks;
    });
    message << " models=";
    const auto modelCount = std::min<std::size_t>(sortedModels.size(), 6U);
    for (std::size_t index = 0; index < modelCount; ++index) {
        const auto& [name, summary] = sortedModels[index];
        message << "[" << name
                << " chunks=" << summary.visible << "/" << summary.chunks
                << " occRejected=" << summary.occlusionRejected
                << " occluders=" << summary.occluders
                << " selected=" << summary.selected
                << " rejTri=" << summary.rejectedTriangles
                << "]";
    }
    core::logInfo(core::LogCategory::Renderer, message.str());
    lastDebugSignature = signature;
}

} // namespace projectunity::editor
