#include "ViewportRenderWorldSelection.hpp"

#include "ViewportRenderWorldProxy.hpp"
#include "ViewportRendererCulling.hpp"

#include <cmath>
#include <optional>

namespace projectunity::editor {
namespace {

[[nodiscard]] std::uint64_t mixHash(std::uint64_t seed, std::uint64_t value) noexcept
{
    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U));
}

[[nodiscard]] float& matrixAt(renderer::RenderMatrix4& matrix, int row, int column)
{
    return matrix.values[static_cast<std::size_t>(column * 4 + row)];
}

[[nodiscard]] float matrixAt(const renderer::RenderMatrix4& matrix, int row, int column)
{
    return matrix.values[static_cast<std::size_t>(column * 4 + row)];
}

[[nodiscard]] renderer::RenderMatrix4 multiply(const renderer::RenderMatrix4& lhs, const renderer::RenderMatrix4& rhs)
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

[[nodiscard]] float radians(float degrees)
{
    return degrees * 0.01745329251994329577F;
}

[[nodiscard]] math::Vec3 rotateEuler(math::Vec3 value, math::Vec3 rotationEuler)
{
    const auto sinX = std::sin(radians(rotationEuler.x));
    const auto cosX = std::cos(radians(rotationEuler.x));
    const auto sinY = std::sin(radians(rotationEuler.y));
    const auto cosY = std::cos(radians(rotationEuler.y));
    const auto sinZ = std::sin(radians(rotationEuler.z));
    const auto cosZ = std::cos(radians(rotationEuler.z));
    value = {value.x, value.y * cosX - value.z * sinX, value.y * sinX + value.z * cosX};
    value = {value.x * cosY + value.z * sinY, value.y, -value.x * sinY + value.z * cosY};
    return {value.x * cosZ - value.y * sinZ, value.x * sinZ + value.y * cosZ, value.z};
}

[[nodiscard]] renderer::RenderMatrix4 modelMatrix(const scene::Entity& entity, math::Vec3 worldPosition)
{
    renderer::RenderMatrix4 matrix;
    matrix.values.fill(0.0F);
    const auto axisX = rotateEuler({entity.transform.scale.x, 0.0F, 0.0F}, entity.transform.rotationEuler);
    const auto axisY = rotateEuler({0.0F, entity.transform.scale.y, 0.0F}, entity.transform.rotationEuler);
    const auto axisZ = rotateEuler({0.0F, 0.0F, entity.transform.scale.z}, entity.transform.rotationEuler);
    matrixAt(matrix, 0, 0) = axisX.x;
    matrixAt(matrix, 1, 0) = axisX.y;
    matrixAt(matrix, 2, 0) = axisX.z;
    matrixAt(matrix, 0, 1) = axisY.x;
    matrixAt(matrix, 1, 1) = axisY.y;
    matrixAt(matrix, 2, 1) = axisY.z;
    matrixAt(matrix, 0, 2) = axisZ.x;
    matrixAt(matrix, 1, 2) = axisZ.y;
    matrixAt(matrix, 2, 2) = axisZ.z;
    matrixAt(matrix, 0, 3) = worldPosition.x;
    matrixAt(matrix, 1, 3) = worldPosition.y;
    matrixAt(matrix, 2, 3) = worldPosition.z;
    matrixAt(matrix, 3, 3) = 1.0F;
    return matrix;
}

[[nodiscard]] renderer::RenderMatrix4 renderMatrix(const std::array<float, 16>& values)
{
    renderer::RenderMatrix4 matrix;
    matrix.values = values;
    return matrix;
}

[[nodiscard]] renderer::RenderMatrix4 translationMatrix(math::Vec3 offset)
{
    renderer::RenderMatrix4 matrix;
    matrix.values = {
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        offset.x, offset.y, offset.z, 1.0F,
    };
    return matrix;
}

[[nodiscard]] std::optional<math::Vec3> entityWorldPosition(const scene::Scene& scene, scene::EntityId id)
{
    math::Vec3 position {0.0F, 0.0F, 0.0F};
    std::optional<scene::EntityId> currentId = id;
    std::size_t guard = 0;
    while (currentId.has_value() && guard++ < 128U) {
        const auto* entity = scene.findEntity(*currentId);
        if (entity == nullptr) {
            return std::nullopt;
        }
        position = position + entity->transform.position;
        currentId = entity->parent;
    }
    return position;
}

[[nodiscard]] const assets::TextureAsset* modelTexture(
    const assets::ModelAsset& model,
    std::optional<std::size_t> textureIndex) noexcept
{
    return textureIndex.has_value() && *textureIndex < model.textures.size()
        ? &model.textures[*textureIndex]
        : nullptr;
}

} // namespace

void appendSelectedPrimitiveOverrideDraw(
    const scene::Scene& scene,
    const assets::IAssetManager& assetManager,
    scene::EntityId selectedEntityId,
    const ViewportRenderWorldCamera& camera,
    const renderer::RenderMatrix4& viewProjection,
    std::vector<renderer::RenderMeshDraw>& meshDraws,
    ViewportRenderWorldStats& stats,
    ViewportFrameBounds& visibleBounds,
    std::uint64_t& visibleSourceTriangleCount)
{
    const auto* entity = selectedEntityId.isValid() ? scene.findEntity(selectedEntityId) : nullptr;
    if (entity == nullptr || !entity->meshRenderer.has_value()) {
        return;
    }
    const auto model = assetManager.model(entity->meshRenderer->modelAssetId);
    const auto selectedPosition = entityWorldPosition(scene, entity->id);
    if (model == nullptr || !selectedPosition.has_value()) {
        return;
    }
    const auto instanceIndex = primitiveInstanceIndexForProxy(*model, *entity->meshRenderer);
    if (!instanceIndex.has_value() || *instanceIndex >= model->primitiveInstances.size()) {
        return;
    }
    const auto& source = model->primitiveInstances[*instanceIndex];
    if (source.primitiveIndex >= model->primitives.size()) {
        return;
    }
    const auto& primitive = model->primitives[source.primitiveIndex];
    if (primitive.materialIndex >= model->materials.size()) {
        return;
    }

    const auto selectedMatrix = modelMatrix(*entity, *selectedPosition);
    const auto matrix = multiply(
        multiply(selectedMatrix, translationMatrix(source.bounds.center * -1.0F)),
        renderMatrix(source.transform));
    const auto worldBounds = transformViewportBounds(matrix, primitive.bounds);
    if (!viewportBoundsVisible(
            worldBounds,
            camera.eye,
            camera.right,
            camera.up,
            camera.forward,
            camera.verticalFovRadians,
            camera.aspectRatio,
            camera.nearPlane,
            camera.farPlane)) {
        return;
    }

    const auto& material = model->materials[primitive.materialIndex];
    const auto sortDepth = math::dot(worldBounds.center - camera.eye, camera.forward);
    const auto sourceTriangleCount = static_cast<std::uint64_t>(primitive.indices.size() / 3U);
    visibleSourceTriangleCount += sourceTriangleCount;
    ++stats.visibleRenderInstanceCount;
    visibleBounds.includeSphere(worldBounds.center, worldBounds.radius);
    meshDraws.push_back({
        model->id,
        source.primitiveIndex,
        0U,
        &primitive,
        &material,
        modelTexture(*model, material.baseColorTexture),
        modelTexture(*model, material.normalTexture),
        modelTexture(*model, material.metallicRoughnessTexture),
        modelTexture(*model, material.occlusionTexture),
        modelTexture(*model, material.emissiveTexture),
        sortDepth,
        {worldBounds.center.x, worldBounds.center.y, worldBounds.center.z},
        worldBounds.radius,
        matrix,
        multiply(viewProjection, matrix),
        source.flipsWinding,
        true,
        mixHash(entity->id.value(), mixHash(source.primitiveIndex, *instanceIndex)),
        mixHash(entity->id.value(), *instanceIndex),
        entity->id.value(),
    });
}

} // namespace projectunity::editor
