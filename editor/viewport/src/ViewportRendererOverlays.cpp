#include "ViewportRendererOverlays.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>

namespace projectunity::editor::detail {
namespace {

[[nodiscard]] math::Vec3 safeNormalized(math::Vec3 value, math::Vec3 fallback)
{
    const auto length = value.length();
    if (length <= 0.00001F || !std::isfinite(length)) {
        return fallback;
    }
    return value / length;
}

} // namespace

void appendLineQuad(
    std::vector<renderer::RenderColorVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    math::Vec3 start,
    math::Vec3 end,
    std::array<float, 4> color,
    float thickness,
    math::Vec3 cameraForward,
    math::Vec3 cameraRight,
    float cameraDistance)
{
    const auto direction = end - start;
    if (direction.lengthSquared() <= 0.0000001F) {
        return;
    }
    const auto side = safeNormalized(math::cross(direction, cameraForward), cameraRight);
    const auto halfWidth = std::clamp(cameraDistance * 0.00085F * std::max(thickness, 1.0F), 0.006F, 0.08F);
    const auto offset = side * halfWidth;
    const auto base = static_cast<std::uint32_t>(vertices.size());
    vertices.push_back({{start.x - offset.x, start.y - offset.y, start.z - offset.z}, color});
    vertices.push_back({{start.x + offset.x, start.y + offset.y, start.z + offset.z}, color});
    vertices.push_back({{end.x + offset.x, end.y + offset.y, end.z + offset.z}, color});
    vertices.push_back({{end.x - offset.x, end.y - offset.y, end.z - offset.z}, color});
    indices.insert(indices.end(), {base, base + 1U, base + 2U, base, base + 2U, base + 3U});
}

void appendGrid(
    std::vector<renderer::RenderColorVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    math::Vec3 cameraForward,
    math::Vec3 cameraRight,
    float cameraDistance)
{
    constexpr int divisions = 40;
    constexpr float halfExtent = 20.0F;
    constexpr float spacing = halfExtent * 2.0F / static_cast<float>(divisions);
    for (int line = 0; line <= divisions; ++line) {
        const auto coordinate = -halfExtent + static_cast<float>(line) * spacing;
        const auto centerLine = std::abs(coordinate) <= 0.0001F;
        const auto color = centerLine
            ? std::array<float, 4> {0.68F, 0.72F, 0.80F, 0.58F}
            : std::array<float, 4> {0.54F, 0.58F, 0.64F, 0.24F};
        appendLineQuad(
            vertices,
            indices,
            {-halfExtent, 0.0F, coordinate},
            {halfExtent, 0.0F, coordinate},
            color,
            centerLine ? 1.4F : 1.0F,
            cameraForward,
            cameraRight,
            cameraDistance);
        appendLineQuad(
            vertices,
            indices,
            {coordinate, 0.0F, -halfExtent},
            {coordinate, 0.0F, halfExtent},
            color,
            centerLine ? 1.4F : 1.0F,
            cameraForward,
            cameraRight,
            cameraDistance);
    }
}

void appendAxes(
    std::vector<renderer::RenderColorVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    math::Vec3 cameraForward,
    math::Vec3 cameraRight,
    float cameraDistance)
{
    appendLineQuad(vertices, indices, {}, {3.0F, 0.0F, 0.0F}, {0.86F, 0.31F, 0.31F, 0.95F}, 2.0F, cameraForward, cameraRight, cameraDistance);
    appendLineQuad(vertices, indices, {}, {0.0F, 3.0F, 0.0F}, {0.37F, 0.75F, 0.43F, 0.95F}, 2.0F, cameraForward, cameraRight, cameraDistance);
    appendLineQuad(vertices, indices, {}, {0.0F, 0.0F, 3.0F}, {0.31F, 0.53F, 0.90F, 0.95F}, 2.0F, cameraForward, cameraRight, cameraDistance);
}

void appendEntityMarker(
    std::vector<renderer::RenderColorVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    math::Vec3 position,
    float halfSize,
    bool selected,
    math::Vec3 cameraForward,
    math::Vec3 cameraRight,
    math::Vec3 cameraUp,
    float cameraDistance)
{
    const auto color = selected
        ? std::array<float, 4> {1.0F, 0.76F, 0.24F, 0.95F}
        : std::array<float, 4> {0.74F, 0.80F, 0.90F, 0.72F};
    const std::array<math::Vec3, 8> corners {
        position + math::Vec3 {-halfSize, -halfSize, -halfSize},
        position + math::Vec3 {halfSize, -halfSize, -halfSize},
        position + math::Vec3 {halfSize, halfSize, -halfSize},
        position + math::Vec3 {-halfSize, halfSize, -halfSize},
        position + math::Vec3 {-halfSize, -halfSize, halfSize},
        position + math::Vec3 {halfSize, -halfSize, halfSize},
        position + math::Vec3 {halfSize, halfSize, halfSize},
        position + math::Vec3 {-halfSize, halfSize, halfSize},
    };
    constexpr std::array<std::pair<int, int>, 12> edges {{
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7},
    }};
    for (const auto& edge : edges) {
        appendLineQuad(
            vertices,
            indices,
            corners[static_cast<std::size_t>(edge.first)],
            corners[static_cast<std::size_t>(edge.second)],
            color,
            selected ? 1.7F : 1.1F,
            cameraForward,
            cameraRight,
            cameraDistance);
    }
    const auto pivotRadius = std::clamp(halfSize * 0.22F, 0.05F, 0.18F);
    appendLineQuad(vertices, indices, position - cameraRight * pivotRadius, position + cameraRight * pivotRadius, color, selected ? 2.1F : 1.4F, cameraForward, cameraRight, cameraDistance);
    appendLineQuad(vertices, indices, position - cameraUp * pivotRadius, position + cameraUp * pivotRadius, color, selected ? 2.1F : 1.4F, cameraForward, cameraRight, cameraDistance);
}

void appendHierarchyLinks(
    std::vector<renderer::RenderColorVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    const scene::Scene& scene,
    const std::function<std::optional<math::Vec3>(scene::EntityId)>& worldPositionFor,
    scene::EntityId selectedEntityId,
    bool skipPrimitivePartLinks,
    math::Vec3 cameraForward,
    math::Vec3 cameraRight,
    float cameraDistance)
{
    for (const auto& entity : scene.entities()) {
        if (!entity.parent.has_value()) {
            continue;
        }
        if (skipPrimitivePartLinks
            && entity.id != selectedEntityId
            && entity.meshRenderer.has_value()
            && entity.meshRenderer->primitiveInstanceIndex.has_value()) {
            continue;
        }
        const auto childPosition = worldPositionFor(entity.id);
        const auto parentPosition = worldPositionFor(*entity.parent);
        if (!childPosition.has_value() || !parentPosition.has_value()) {
            continue;
        }
        appendLineQuad(
            vertices,
            indices,
            *parentPosition,
            *childPosition,
            {0.63F, 0.67F, 0.73F, 0.38F},
            1.0F,
            cameraForward,
            cameraRight,
            cameraDistance);
    }
}

} // namespace projectunity::editor::detail
