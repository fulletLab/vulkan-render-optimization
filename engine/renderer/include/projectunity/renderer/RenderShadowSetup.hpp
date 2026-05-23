#pragma once

#include <projectunity/renderer/RendererTypes.hpp>

#include <array>
#include <cstdint>
#include <span>

namespace projectunity::renderer {

struct RenderShadowMapSelection {
    bool enabled {false};
    std::uint32_t lightIndex {0};
    RenderLightType lightType {RenderLightType::Directional};
    RenderMatrix4 viewProjection;
};

[[nodiscard]] RenderShadowMapSelection chooseShadowMap(
    std::span<const RenderLight> lights,
    std::array<float, 3> visibleBoundsCenter,
    float visibleBoundsRadius);

} // namespace projectunity::renderer
