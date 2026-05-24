#pragma once

#include <projectunity/assets/AssetManager.hpp>
#include <projectunity/math/Vec3.hpp>
#include <projectunity/renderer/IRenderer.hpp>
#include <projectunity/scene/Scene.hpp>

#include <array>

namespace projectunity::editor {

struct ViewportWorldBounds {
    std::array<math::Vec3, 8> corners;
    math::Vec3 center;
    float radius {0.0F};
};

[[nodiscard]] ViewportWorldBounds transformViewportBounds(
    const renderer::RenderMatrix4& matrix,
    const assets::MeshBounds& bounds);

[[nodiscard]] bool viewportBoundsVisible(
    const ViewportWorldBounds& bounds,
    math::Vec3 eye,
    math::Vec3 right,
    math::Vec3 up,
    math::Vec3 forward,
    float verticalFovRadians,
    float aspectRatio,
    float nearPlane,
    float farPlane);

[[nodiscard]] bool defaultPrimitiveProxyTransform(
    const scene::TransformComponent& transform,
    math::Vec3 expectedPosition) noexcept;

} // namespace projectunity::editor
