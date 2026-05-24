#pragma once

#include <projectunity/math/Vec3.hpp>
#include <projectunity/renderer/RendererTypes.hpp>

#include <array>
#include <cstdint>
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

} // namespace projectunity::editor::detail
