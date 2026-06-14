#include "ViewportRenderWorld.hpp"

#include "ViewportMeshLod.hpp"
#include "ViewportRenderWorldRecord.hpp"
#include "ViewportRenderWorldShadowPolicy.hpp"
#include "ViewportRendererCulling.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <optional>

namespace projectunity::editor {
namespace {

[[nodiscard]] float matrixAt(const renderer::RenderMatrix4& matrix, int row, int column)
{
    return matrix.values[static_cast<std::size_t>(column * 4 + row)];
}

[[nodiscard]] float& matrixAt(renderer::RenderMatrix4& matrix, int row, int column)
{
    return matrix.values[static_cast<std::size_t>(column * 4 + row)];
}

[[nodiscard]] renderer::RenderMatrix4 multiply(
    const renderer::RenderMatrix4& lhs,
    const renderer::RenderMatrix4& rhs)
{
    renderer::RenderMatrix4 result;
    result.values.fill(0.0F);
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            for (int index = 0; index < 4; ++index) {
                matrixAt(result, row, column) += matrixAt(lhs, row, index) * matrixAt(rhs, index, column);
            }
        }
    }
    return result;
}

[[nodiscard]] const assets::TextureAsset* modelTexture(
    const assets::ModelAsset& model,
    std::optional<std::size_t> textureIndex) noexcept
{
    return textureIndex.has_value() && *textureIndex < model.textures.size()
        ? &model.textures[*textureIndex]
        : nullptr;
}

[[nodiscard]] math::Vec3 safeNormalized(math::Vec3 value, math::Vec3 fallback) noexcept
{
    const auto valueLength = value.length();
    if (valueLength <= 0.00001F || !std::isfinite(valueLength)) {
        return fallback;
    }
    return value / valueLength;
}

[[nodiscard]] ViewportWorldBounds sphereBounds(math::Vec3 center, float radius) noexcept
{
    assets::MeshBounds bounds;
    bounds.minimum = center - math::Vec3 {radius, radius, radius};
    bounds.maximum = center + math::Vec3 {radius, radius, radius};
    return transformViewportBounds(renderer::RenderMatrix4 {}, bounds);
}

[[nodiscard]] bool sphereVisibleInCamera(math::Vec3 center, float radius, const ViewportRenderWorldCamera& camera)
{
    return viewportBoundsVisible(
        sphereBounds(center, radius),
        camera.eye,
        camera.right,
        camera.up,
        camera.forward,
        camera.verticalFovRadians,
        camera.aspectRatio,
        camera.nearPlane,
        camera.farPlane);
}

[[nodiscard]] float shadowProjectionReach(
    const renderer::RenderShadowMapSelection& selection,
    const ViewportRenderWorldCamera& camera) noexcept
{
    float reach = 0.0F;
    for (const auto split : selection.cascadeSplits) {
        if (std::isfinite(split)) {
            reach = std::max(reach, split);
        }
    }
    if (reach <= 0.0F && std::isfinite(selection.depthFarPlane)) {
        reach = selection.depthFarPlane;
    }
    if (reach <= 0.0F) {
        reach = std::min(camera.farPlane, 256.0F);
    }
    return std::clamp(reach * 2.5F, 32.0F, std::max(camera.farPlane, 32.0F));
}

template<typename Instance>
[[nodiscard]] bool instanceIntersectsShadowSelection(
    const renderer::RenderShadowMapSelection& selection,
    const Instance& instance)
{
    const auto viewCount = std::max<std::uint32_t>(selection.viewCount, 1U);
    const std::array<float, 3> center {
        instance.worldBounds.center.x,
        instance.worldBounds.center.y,
        instance.worldBounds.center.z,
    };
    for (std::uint32_t viewIndex = 0; viewIndex < viewCount && viewIndex < renderer::kMaxShadowViews; ++viewIndex) {
        const auto& matrix = viewIndex < selection.viewCount
            ? selection.viewProjections[viewIndex]
            : selection.viewProjection;
        if (renderer::shadowSphereIntersects(matrix, center, instance.worldBounds.radius)) {
            return true;
        }
    }
    return false;
}

template<typename Instance>
[[nodiscard]] bool offscreenDirectionalShadowMayReachCamera(
    const renderer::RenderShadowMapSelection& selection,
    const renderer::RenderLight* light,
    const ViewportRenderWorldCamera& camera,
    const Instance& instance)
{
    if (light == nullptr || light->type != renderer::RenderLightType::Directional) {
        return true;
    }
    const auto direction = safeNormalized(
        {light->direction[0], light->direction[1], light->direction[2]},
        {0.35F, -0.82F, 0.45F});
    const auto reach = shadowProjectionReach(selection, camera);
    constexpr std::array<float, 6> kSamples {0.08F, 0.18F, 0.32F, 0.50F, 0.72F, 1.0F};
    for (const auto sample : kSamples) {
        if (sphereVisibleInCamera(instance.worldBounds.center + direction * (reach * sample), instance.worldBounds.radius, camera)) {
            return true;
        }
    }
    return false;
}

} // namespace

void ViewportRenderWorld::collectShadowCasters(
    const renderer::RenderShadowMapSelection& shadowSelection,
    const renderer::RenderLight* shadowLight,
    const ViewportRenderWorldCamera& camera,
    int viewportHeight,
    scene::EntityId selectedEntityId,
    std::vector<renderer::RenderMeshDraw>& shadowMeshDraws,
    ViewportRenderWorldStats& stats) const
{
    shadowMeshDraws.clear();
    if (!shadowSelection.enabled) {
        return;
    }
    for (const auto* record : orderedRecords_) {
        if (record == nullptr) {
            continue;
        }
        for (const auto& instance : record->instances) {
            if (instance.primitiveIndex >= instance.model->primitives.size()) {
                continue;
            }
            const auto& primitive = instance.model->primitives[instance.primitiveIndex];
            if (primitive.materialIndex >= instance.model->materials.size()) {
                continue;
            }
            const auto& material = instance.model->materials[primitive.materialIndex];
            if (material.alphaMode == assets::MaterialAlphaMode::Blend) {
                continue;
            }
            const auto visibleToCamera = viewportBoundsVisible(
                instance.worldBounds,
                camera.eye,
                camera.right,
                camera.up,
                camera.forward,
                camera.verticalFovRadians,
                camera.aspectRatio,
                camera.nearPlane,
                camera.farPlane);
            const auto intersectsShadowSelection = instanceIntersectsShadowSelection(shadowSelection, instance);
            if (visibleToCamera) {
                ++stats.shadowVisibleInstances;
                if (!intersectsShadowSelection) {
                    continue;
                }
            } else {
                ++stats.shadowOnlyCandidateInstances;
                if (!offscreenDirectionalShadowMayReachCamera(shadowSelection, shadowLight, camera, instance)) {
                    ++stats.shadowOnlyRejectedInstances;
                    continue;
                }
                // Offscreen casters can still matter when their projected directional
                // shadow reaches the camera; do not reject them only because the caster
                // itself is outside the current camera frustum.
            }
            const auto cameraDistance = (instance.worldBounds.center - camera.eye).length();
            const auto sortDepth = std::max(cameraDistance, camera.nearPlane);
            const auto lodDistance = viewportLodDistanceToBounds(
                camera.eye,
                instance.worldBounds.center,
                instance.worldBounds.radius,
                camera.nearPlane);
            const auto forceFullResolution = instance.sceneNodeId == selectedEntityId && record->instances.size() <= 4U;
            const auto lodIndex = selectViewportMeshLod(
                primitive,
                instance.worldBounds.radius,
                lodDistance,
                camera.verticalFovRadians,
                static_cast<float>(std::max(viewportHeight, 1)),
                forceFullResolution);
            shadowMeshDraws.push_back({
                instance.modelAssetId,
                instance.primitiveIndex,
                lodIndex,
                &primitive,
                &material,
                modelTexture(*instance.model, material.baseColorTexture),
                modelTexture(*instance.model, material.normalTexture),
                modelTexture(*instance.model, material.metallicRoughnessTexture),
                modelTexture(*instance.model, material.occlusionTexture),
                modelTexture(*instance.model, material.emissiveTexture),
                sortDepth,
                {instance.worldBounds.center.x, instance.worldBounds.center.y, instance.worldBounds.center.z},
                instance.worldBounds.radius,
                instance.modelMatrix,
                multiply(shadowSelection.viewProjection, instance.modelMatrix),
                instance.flipsWinding,
                true,
                instance.renderInstanceId,
                instance.renderChunkId,
                instance.sceneNodeId.value(),
            });
        }
    }
    applyViewportShadowPolicy(shadowMeshDraws, selectedEntityId, camera, viewportHeight, stats);
}

} // namespace projectunity::editor
