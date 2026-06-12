#include "ViewportRenderWorld.hpp"

#include "ViewportMeshLod.hpp"
#include "ViewportRenderWorldRecord.hpp"
#include "ViewportRenderWorldShadowPolicy.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
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

} // namespace

void ViewportRenderWorld::collectShadowCasters(
    const renderer::RenderShadowMapSelection& shadowSelection,
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
            if (instance.primitiveIndex >= instance.model->primitives.size()
                || !instanceIntersectsShadowSelection(shadowSelection, instance)) {
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
            const auto sortDepth = math::dot(instance.worldBounds.center - camera.eye, camera.forward);
            const auto forceFullResolution = instance.sceneNodeId == selectedEntityId && record->instances.size() <= 4U;
            const auto lodIndex = selectViewportMeshLod(
                primitive,
                instance.worldBounds.radius,
                std::max(sortDepth, camera.nearPlane),
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
                0U,
                instance.sceneNodeId.value(),
            });
        }
    }
    applyViewportShadowPolicy(shadowMeshDraws, selectedEntityId, camera, viewportHeight, stats);
}

} // namespace projectunity::editor
