#include "ViewportRenderWorldDiagnostics.hpp"

#include <projectunity/core/Log.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>
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
    core::logInfo(core::LogCategory::Renderer, message.str());
    lastDebugSignature = signature;
}

} // namespace projectunity::editor
