#include "ViewportMeshWireOverlay.hpp"

#include "ViewportRendererOverlays.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace projectunity::editor {
namespace {

constexpr std::uint64_t kSelectedWireTriangleBudget = 80'000ULL;
constexpr std::uint64_t kVisibleWireTriangleBudget = 8'000ULL;

[[nodiscard]] float at(const renderer::RenderMatrix4& matrix, int row, int column)
{
    return matrix.values[static_cast<std::size_t>(column * 4 + row)];
}

[[nodiscard]] math::Vec3 transformPoint(const renderer::RenderMatrix4& matrix, math::Vec3 point)
{
    return {
        at(matrix, 0, 0) * point.x + at(matrix, 0, 1) * point.y + at(matrix, 0, 2) * point.z + at(matrix, 0, 3),
        at(matrix, 1, 0) * point.x + at(matrix, 1, 1) * point.y + at(matrix, 1, 2) * point.z + at(matrix, 1, 3),
        at(matrix, 2, 0) * point.x + at(matrix, 2, 1) * point.y + at(matrix, 2, 2) * point.z + at(matrix, 2, 3),
    };
}

[[nodiscard]] const std::vector<std::uint32_t>* indicesForDraw(const renderer::RenderMeshDraw& draw) noexcept
{
    if (draw.primitive == nullptr) {
        return nullptr;
    }
    if (draw.lodIndex > 0U && draw.lodIndex - 1U < draw.primitive->lods.size()) {
        const auto& lodIndices = draw.primitive->lods[draw.lodIndex - 1U].indices;
        if (!lodIndices.empty()) {
            return &lodIndices;
        }
    }
    return &draw.primitive->indices;
}

void appendDrawWire(
    std::vector<renderer::RenderColorVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    const renderer::RenderMeshDraw& draw,
    std::uint64_t& remainingTriangleBudget,
    std::array<float, 4> color,
    float thickness,
    math::Vec3 cameraForward,
    math::Vec3 cameraRight,
    float cameraDistance)
{
    if (remainingTriangleBudget == 0U || draw.primitive == nullptr || draw.primitive->vertices.empty()) {
        return;
    }
    const auto* drawIndices = indicesForDraw(draw);
    if (drawIndices == nullptr || drawIndices->size() < 3U) {
        return;
    }

    const auto triangleCount = static_cast<std::uint64_t>(drawIndices->size() / 3U);
    if (triangleCount == 0U) {
        return;
    }
    const auto triangleStep = static_cast<std::size_t>(std::max<std::uint64_t>(
        1U,
        (triangleCount + remainingTriangleBudget - 1U) / remainingTriangleBudget));

    for (std::size_t index = 2U; index < drawIndices->size() && remainingTriangleBudget > 0U; index += 3U * triangleStep) {
        const auto i0 = (*drawIndices)[index - 2U];
        const auto i1 = (*drawIndices)[index - 1U];
        const auto i2 = (*drawIndices)[index];
        if (i0 >= draw.primitive->vertices.size()
            || i1 >= draw.primitive->vertices.size()
            || i2 >= draw.primitive->vertices.size()) {
            continue;
        }
        const auto a = transformPoint(draw.modelMatrix, draw.primitive->vertices[i0].position);
        const auto b = transformPoint(draw.modelMatrix, draw.primitive->vertices[i1].position);
        const auto c = transformPoint(draw.modelMatrix, draw.primitive->vertices[i2].position);
        detail::appendLineQuad(vertices, indices, a, b, color, thickness, cameraForward, cameraRight, cameraDistance);
        detail::appendLineQuad(vertices, indices, b, c, color, thickness, cameraForward, cameraRight, cameraDistance);
        detail::appendLineQuad(vertices, indices, c, a, color, thickness, cameraForward, cameraRight, cameraDistance);
        --remainingTriangleBudget;
    }
}

} // namespace

void appendViewportMeshWireOverlay(
    std::vector<renderer::RenderColorVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    std::span<const renderer::RenderMeshDraw> meshDraws,
    scene::EntityId selectedEntityId,
    bool includeVisibleMeshes,
    math::Vec3 cameraForward,
    math::Vec3 cameraRight,
    float cameraDistance)
{
    auto selectedBudget = kSelectedWireTriangleBudget;
    auto visibleBudget = includeVisibleMeshes ? kVisibleWireTriangleBudget : 0ULL;
    for (const auto& draw : meshDraws) {
        const auto selected = selectedEntityId.isValid() && draw.sceneNodeId == selectedEntityId.value();
        if (!selected && visibleBudget == 0U) {
            continue;
        }
        if (selected) {
            appendDrawWire(
                vertices,
                indices,
                draw,
                selectedBudget,
                {1.0F, 0.78F, 0.18F, 0.70F},
                1.15F,
                cameraForward,
                cameraRight,
                cameraDistance);
            continue;
        }
        appendDrawWire(
            vertices,
            indices,
            draw,
            visibleBudget,
            {0.20F, 0.82F, 1.0F, 0.26F},
            0.65F,
            cameraForward,
            cameraRight,
            cameraDistance);
    }
}

} // namespace projectunity::editor
