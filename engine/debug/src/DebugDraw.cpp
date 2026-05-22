#include <projectunity/debug/DebugDraw.hpp>

#include <projectunity/core/Log.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace projectunity::debug {
namespace {

[[nodiscard]] bool isFinite(float value)
{
    return std::isfinite(value);
}

[[nodiscard]] bool isFinite(math::Vec3 value)
{
    return isFinite(value.x) && isFinite(value.y) && isFinite(value.z);
}

[[nodiscard]] bool isFinite(DebugColor value)
{
    return isFinite(value.r) && isFinite(value.g) && isFinite(value.b) && isFinite(value.a);
}

[[nodiscard]] bool isValidLine(math::Vec3 start, math::Vec3 end, DebugColor color, float thickness)
{
    return isFinite(start)
        && isFinite(end)
        && isFinite(color)
        && isFinite(thickness)
        && thickness > 0.0F;
}

[[nodiscard]] bool appendRejected(const char* message)
{
    core::logWarning(core::LogCategory::Renderer, message);
    return false;
}

} // namespace

bool DebugDrawList::line(math::Vec3 start, math::Vec3 end, DebugColor color, float thickness)
{
    if (!isValidLine(start, end, color, thickness)) {
        return appendRejected("Debug draw rejected an invalid line");
    }

    lines_.push_back({start, end, color, thickness});
    return true;
}

bool DebugDrawList::ray(math::Vec3 origin, math::Vec3 direction, float length, DebugColor color, float thickness)
{
    if (!isFinite(origin)
        || !isFinite(direction)
        || !isFinite(length)
        || !isFinite(color)
        || direction.lengthSquared() <= 0.000001F
        || length <= 0.0F
        || thickness <= 0.0F) {
        return appendRejected("Debug draw rejected an invalid ray");
    }

    const auto normal = direction.normalized();
    const auto end = origin + normal * length;
    const auto shaftAdded = line(origin, end, color, thickness);
    const auto arrowLength = std::min(length * 0.18F, 0.35F);
    const auto reference = std::fabs(normal.y) < 0.98F
        ? math::Vec3 {0.0F, 1.0F, 0.0F}
        : math::Vec3 {1.0F, 0.0F, 0.0F};
    const auto side = math::cross(normal, reference).normalized();
    const auto up = math::cross(side, normal).normalized();
    const auto arrowBase = end - normal * arrowLength;

    return shaftAdded
        && line(end, arrowBase + side * (arrowLength * 0.45F), color, thickness)
        && line(end, arrowBase - side * (arrowLength * 0.45F), color, thickness)
        && line(end, arrowBase + up * (arrowLength * 0.45F), color, thickness)
        && line(end, arrowBase - up * (arrowLength * 0.45F), color, thickness);
}

bool DebugDrawList::aabb(math::Vec3 minimum, math::Vec3 maximum, DebugColor color, float thickness)
{
    if (!isFinite(minimum)
        || !isFinite(maximum)
        || minimum.x > maximum.x
        || minimum.y > maximum.y
        || minimum.z > maximum.z) {
        return appendRejected("Debug draw rejected an invalid AABB");
    }

    const std::array<math::Vec3, 8> corners {
        math::Vec3 {minimum.x, minimum.y, minimum.z},
        math::Vec3 {maximum.x, minimum.y, minimum.z},
        math::Vec3 {maximum.x, maximum.y, minimum.z},
        math::Vec3 {minimum.x, maximum.y, minimum.z},
        math::Vec3 {minimum.x, minimum.y, maximum.z},
        math::Vec3 {maximum.x, minimum.y, maximum.z},
        math::Vec3 {maximum.x, maximum.y, maximum.z},
        math::Vec3 {minimum.x, maximum.y, maximum.z},
    };
    constexpr std::array<std::pair<std::size_t, std::size_t>, 12> edges {{
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7},
    }};

    for (const auto& [start, end] : edges) {
        if (!line(corners[start], corners[end], color, thickness)) {
            return false;
        }
    }
    return true;
}

bool DebugDrawList::frustum(const FrustumCorners& corners, DebugColor color, float thickness)
{
    for (const auto corner : corners) {
        if (!isFinite(corner)) {
            return appendRejected("Debug draw rejected an invalid frustum");
        }
    }

    constexpr std::array<std::pair<std::size_t, std::size_t>, 12> edges {{
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7},
    }};
    for (const auto& [start, end] : edges) {
        if (!line(corners[start], corners[end], color, thickness)) {
            return false;
        }
    }
    return true;
}

bool DebugDrawList::grid(const DebugGrid& settings)
{
    if (!isFinite(settings.origin)
        || !isFinite(settings.xAxis)
        || !isFinite(settings.zAxis)
        || !isFinite(settings.halfExtent)
        || !isFinite(settings.color)
        || !isFinite(settings.centerColor)
        || !isFinite(settings.thickness)
        || settings.xAxis.lengthSquared() <= 0.000001F
        || settings.zAxis.lengthSquared() <= 0.000001F
        || settings.halfExtent <= 0.0F
        || settings.divisions == 0
        || settings.thickness <= 0.0F) {
        return appendRejected("Debug draw rejected invalid grid settings");
    }

    const auto xAxis = settings.xAxis.normalized();
    const auto zAxis = settings.zAxis.normalized();
    const auto step = settings.halfExtent * 2.0F / static_cast<float>(settings.divisions);
    for (std::size_t index = 0; index <= settings.divisions; ++index) {
        const auto offset = -settings.halfExtent + static_cast<float>(index) * step;
        const auto isCenter = std::fabs(offset) <= step * 0.25F;
        const auto color = isCenter ? settings.centerColor : settings.color;
        const auto xOffset = xAxis * offset;
        const auto zOffset = zAxis * offset;
        if (!line(settings.origin + xOffset - zAxis * settings.halfExtent, settings.origin + xOffset + zAxis * settings.halfExtent, color, settings.thickness)
            || !line(settings.origin + zOffset - xAxis * settings.halfExtent, settings.origin + zOffset + xAxis * settings.halfExtent, color, settings.thickness)) {
            return false;
        }
    }
    return true;
}

const std::vector<DebugLine>& DebugDrawList::lines() const noexcept
{
    return lines_;
}

std::size_t DebugDrawList::lineCount() const noexcept
{
    return lines_.size();
}

void DebugDrawList::clear() noexcept
{
    lines_.clear();
}

} // namespace projectunity::debug
