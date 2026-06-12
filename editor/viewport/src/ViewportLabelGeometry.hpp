#pragma once

#include <projectunity/math/Vec3.hpp>
#include <projectunity/renderer/RendererTypes.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace projectunity::editor {

struct ViewportLabelCamera {
    math::Vec3 eye;
    math::Vec3 right;
    math::Vec3 up;
    math::Vec3 forward;
    float verticalFovRadians {1.04719755F};
    float viewportHeightPixels {720.0F};
};

void appendViewportLabel(
    std::vector<renderer::RenderColorVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    const ViewportLabelCamera& camera,
    math::Vec3 anchor,
    std::string_view text,
    bool selected);

void appendViewportScreenLabel(
    std::vector<renderer::RenderColorVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    const ViewportLabelCamera& camera,
    float viewportWidthPixels,
    float screenX,
    float screenY,
    std::string_view text,
    bool selected);

} // namespace projectunity::editor
