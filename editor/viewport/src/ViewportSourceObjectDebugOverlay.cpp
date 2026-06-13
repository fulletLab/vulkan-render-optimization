#include "ViewportSourceObjectDebugOverlay.hpp"

#include "ViewportRendererCulling.hpp"
#include "ViewportRendererOverlays.hpp"
#include "ViewportSceneLookup.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

namespace projectunity::editor {
namespace {

[[nodiscard]] float& at(renderer::RenderMatrix4& matrix, int row, int column)
{
    return matrix.values[static_cast<std::size_t>(column * 4 + row)];
}

[[nodiscard]] float at(const renderer::RenderMatrix4& matrix, int row, int column)
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
                at(result, row, column) += at(lhs, row, index) * at(rhs, index, column);
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
    at(matrix, 0, 0) = axisX.x;
    at(matrix, 1, 0) = axisX.y;
    at(matrix, 2, 0) = axisX.z;
    at(matrix, 0, 1) = axisY.x;
    at(matrix, 1, 1) = axisY.y;
    at(matrix, 2, 1) = axisY.z;
    at(matrix, 0, 2) = axisZ.x;
    at(matrix, 1, 2) = axisZ.y;
    at(matrix, 2, 2) = axisZ.z;
    at(matrix, 0, 3) = worldPosition.x;
    at(matrix, 1, 3) = worldPosition.y;
    at(matrix, 2, 3) = worldPosition.z;
    at(matrix, 3, 3) = 1.0F;
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

void appendSourceMarker(
    std::vector<renderer::RenderColorVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    const ViewportLabelCamera& labelCamera,
    const ViewportWorldBounds& bounds)
{
    const auto depth = math::dot(bounds.center - labelCamera.eye, labelCamera.forward);
    const auto size = std::clamp(bounds.radius * 0.05F, 0.045F, 0.28F);
    constexpr std::array<float, 4> color {0.0F, 0.95F, 1.0F, 0.58F};
    detail::appendLineQuad(
        vertices,
        indices,
        bounds.center - labelCamera.right * size,
        bounds.center + labelCamera.right * size,
        color,
        1.15F,
        labelCamera.forward,
        labelCamera.right,
        std::max(depth, 1.0F));
    detail::appendLineQuad(
        vertices,
        indices,
        bounds.center - labelCamera.up * size,
        bounds.center + labelCamera.up * size,
        color,
        1.15F,
        labelCamera.forward,
        labelCamera.right,
        std::max(depth, 1.0F));
}

} // namespace

void appendViewportSourceObjectDebugOverlay(
    std::vector<renderer::RenderColorVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    const scene::Scene& scene,
    const assets::IAssetManager& assetManager,
    const ViewportLabelCamera& labelCamera,
    float viewportWidthPixels,
    float aspectRatio,
    float nearPlane,
    float farPlane)
{
    appendViewportScreenLabel(
        vertices,
        indices,
        labelCamera,
        viewportWidthPixels,
        14.0F,
        120.0F,
        "SOURCE OBJECTS CYAN  ORIGINAL PRIMITIVE INSTANCES",
        false);

    struct SourceMarker {
        ViewportWorldBounds bounds;
        float depth {0.0F};
    };
    std::vector<SourceMarker> markers;
    const ViewportSceneEntityLookup lookup(scene);
    for (const auto& entity : scene.entities()) {
        if (!entity.meshRenderer.has_value() || !entity.meshRenderer->renderable) {
            continue;
        }
        const auto model = assetManager.model(entity.meshRenderer->modelAssetId);
        const auto worldPosition = lookup.worldPosition(entity.id);
        if (model == nullptr || !worldPosition.has_value()) {
            continue;
        }
        const auto entityMatrix = modelMatrix(entity, *worldPosition);
        const auto appendPrimitive = [&](std::uint32_t primitiveIndex, const renderer::RenderMatrix4& matrix) {
            if (primitiveIndex >= model->primitives.size()) {
                return;
            }
            const auto bounds = transformViewportBounds(matrix, model->primitives[primitiveIndex].bounds);
            if (!viewportBoundsVisible(
                    bounds,
                    labelCamera.eye,
                    labelCamera.right,
                    labelCamera.up,
                    labelCamera.forward,
                    labelCamera.verticalFovRadians,
                    aspectRatio,
                    nearPlane,
                    farPlane)) {
                return;
            }
            const auto depth = math::dot(bounds.center - labelCamera.eye, labelCamera.forward);
            if (depth <= nearPlane || !std::isfinite(depth)) {
                return;
            }
            markers.push_back({bounds, depth});
        };
        if (entity.meshRenderer->primitiveInstanceIndex.has_value()
            && *entity.meshRenderer->primitiveInstanceIndex < model->primitiveInstances.size()) {
            const auto& instance = model->primitiveInstances[*entity.meshRenderer->primitiveInstanceIndex];
            const auto matrix = multiply(
                multiply(entityMatrix, translationMatrix(instance.bounds.center * -1.0F)),
                renderMatrix(instance.transform));
            appendPrimitive(instance.primitiveIndex, matrix);
            continue;
        }
        if (!model->primitiveInstances.empty()) {
            for (const auto& instance : model->primitiveInstances) {
                appendPrimitive(instance.primitiveIndex, multiply(entityMatrix, renderMatrix(instance.transform)));
            }
            continue;
        }
        for (std::uint32_t primitiveIndex = 0; primitiveIndex < model->primitives.size(); ++primitiveIndex) {
            appendPrimitive(primitiveIndex, entityMatrix);
        }
    }

    constexpr std::size_t kMaxSourceMarkers = 2500U;
    const auto stride = markers.size() > kMaxSourceMarkers
        ? (markers.size() + kMaxSourceMarkers - 1U) / kMaxSourceMarkers
        : 1U;
    std::size_t drawn = 0;
    for (std::size_t index = 0; index < markers.size() && drawn < kMaxSourceMarkers; index += stride) {
        appendSourceMarker(vertices, indices, labelCamera, markers[index].bounds);
        ++drawn;
    }
}

} // namespace projectunity::editor
