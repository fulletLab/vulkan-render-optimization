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
    RenderShadowMode mode {RenderShadowMode::None};
    RenderMatrix4 viewProjection;
    std::array<RenderMatrix4, kMaxShadowViews> viewProjections {};
    std::array<float, kMaxShadowCascades> cascadeSplits {};
    std::uint32_t viewCount {0};
    std::uint32_t cascadeCount {0};
    float depthFarPlane {0.0F};
};

[[nodiscard]] RenderShadowMapSelection chooseShadowMap(
    std::span<const RenderLight> lights,
    std::array<float, 3> visibleBoundsCenter,
    float visibleBoundsRadius);

[[nodiscard]] bool shadowSphereIntersects(
    const RenderMatrix4& shadowViewProjection,
    std::array<float, 3> worldCenter,
    float worldRadius);

} // namespace projectunity::renderer
