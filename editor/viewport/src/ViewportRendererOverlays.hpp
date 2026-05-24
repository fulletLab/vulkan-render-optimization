#pragma once

#include <projectunity/math/Vec3.hpp>
#include <projectunity/renderer/RendererTypes.hpp>
#include <projectunity/scene/Scene.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace projectunity::editor::detail {

void appendLineQuad(
    std::vector<renderer::RenderColorVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    math::Vec3 start,
    math::Vec3 end,
    std::array<float, 4> color,
    float thickness,
    math::Vec3 cameraForward,
    math::Vec3 cameraRight,
    float cameraDistance);

void appendGrid(
    std::vector<renderer::RenderColorVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    math::Vec3 cameraForward,
    math::Vec3 cameraRight,
    float cameraDistance);

void appendAxes(
    std::vector<renderer::RenderColorVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    math::Vec3 cameraForward,
    math::Vec3 cameraRight,
    float cameraDistance);

void appendEntityMarker(
    std::vector<renderer::RenderColorVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    math::Vec3 position,
    float halfSize,
    bool selected,
    math::Vec3 cameraForward,
    math::Vec3 cameraRight,
    math::Vec3 cameraUp,
    float cameraDistance);

void appendHierarchyLinks(
    std::vector<renderer::RenderColorVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    const scene::Scene& scene,
    const std::function<std::optional<math::Vec3>(scene::EntityId)>& worldPositionFor,
    scene::EntityId selectedEntityId,
    bool skipPrimitivePartLinks,
    math::Vec3 cameraForward,
    math::Vec3 cameraRight,
    float cameraDistance);

} // namespace projectunity::editor::detail
