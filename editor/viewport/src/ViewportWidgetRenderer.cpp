#include <projectunity/editor/ViewportWidget.hpp>
#include "ViewportLabelGeometry.hpp"
#include <projectunity/core/Log.hpp>
#include <projectunity/renderer/IRenderer.hpp>
#include <projectunity/renderer/RenderShadowSetup.hpp>
#include <QString>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
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
[[nodiscard]] renderer::RenderMatrix4 viewMatrix(
    math::Vec3 eye,
    math::Vec3 right,
    math::Vec3 up,
    math::Vec3 forward)
{
    renderer::RenderMatrix4 view;
    view.values.fill(0.0F);
    at(view, 0, 0) = right.x;
    at(view, 0, 1) = right.y;
    at(view, 0, 2) = right.z;
    at(view, 0, 3) = -math::dot(right, eye);
    at(view, 1, 0) = up.x;
    at(view, 1, 1) = up.y;
    at(view, 1, 2) = up.z;
    at(view, 1, 3) = -math::dot(up, eye);
    at(view, 2, 0) = forward.x;
    at(view, 2, 1) = forward.y;
    at(view, 2, 2) = forward.z;
    at(view, 2, 3) = -math::dot(forward, eye);
    at(view, 3, 3) = 1.0F;
    return view;
}
[[nodiscard]] renderer::RenderMatrix4 perspectiveMatrix(float verticalFovRadians, float aspect, float nearPlane, float farPlane)
{
    renderer::RenderMatrix4 projection;
    projection.values.fill(0.0F);
    const auto focal = 1.0F / std::tan(verticalFovRadians * 0.5F);
    at(projection, 0, 0) = focal / std::max(aspect, 0.001F);
    at(projection, 1, 1) = -focal;
    at(projection, 2, 2) = farPlane / (farPlane - nearPlane);
    at(projection, 2, 3) = -(nearPlane * farPlane) / (farPlane - nearPlane);
    at(projection, 3, 2) = 1.0F;
    return projection;
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
[[nodiscard]] math::Vec3 transformPoint(const renderer::RenderMatrix4& matrix, math::Vec3 point)
{
    return {
        at(matrix, 0, 0) * point.x + at(matrix, 0, 1) * point.y + at(matrix, 0, 2) * point.z + at(matrix, 0, 3),
        at(matrix, 1, 0) * point.x + at(matrix, 1, 1) * point.y + at(matrix, 1, 2) * point.z + at(matrix, 1, 3),
        at(matrix, 2, 0) * point.x + at(matrix, 2, 1) * point.y + at(matrix, 2, 2) * point.z + at(matrix, 2, 3),
    };
}
[[nodiscard]] float maxScale(const renderer::RenderMatrix4& matrix)
{
    const auto axisLength = [&matrix](int column) {
        const math::Vec3 axis {at(matrix, 0, column), at(matrix, 1, column), at(matrix, 2, column)};
        return axis.length();
    };
    return std::max({axisLength(0), axisLength(1), axisLength(2)});
}
[[nodiscard]] float maxAbsScale(math::Vec3 scale)
{
    return std::max({std::fabs(scale.x), std::fabs(scale.y), std::fabs(scale.z)});
}
[[nodiscard]] math::Vec3 transformPoint(const scene::Entity& entity, math::Vec3 worldPosition, math::Vec3 point)
{
    return worldPosition + rotateEuler(
        {point.x * entity.transform.scale.x, point.y * entity.transform.scale.y, point.z * entity.transform.scale.z},
        entity.transform.rotationEuler);
}
[[nodiscard]] bool sphereVisible(
    math::Vec3 center,
    float radius,
    math::Vec3 eye,
    math::Vec3 right,
    math::Vec3 up,
    math::Vec3 forward,
    float verticalFovRadians,
    float aspectRatio)
{
    constexpr float nearPlane = 0.05F;
    constexpr float farPlane = 4000.0F;
    if (radius < 0.0F || !std::isfinite(radius)) {
        return true;
    }
    const auto relative = center - eye;
    const auto depth = math::dot(relative, forward);
    if (depth + radius < nearPlane || depth - radius > farPlane) {
        return false;
    }
    const auto extentDepth = std::max(depth, nearPlane);
    const auto halfHeight = std::tan(verticalFovRadians * 0.5F) * extentDepth;
    const auto halfWidth = halfHeight * std::max(aspectRatio, 0.001F);
    const auto x = math::dot(relative, right);
    const auto y = math::dot(relative, up);
    return std::fabs(x) <= halfWidth + radius && std::fabs(y) <= halfHeight + radius;
}
[[nodiscard]] std::size_t indexCountForLod(const assets::MeshPrimitive& primitive, std::uint32_t lodIndex)
{
    if (lodIndex == 0U || lodIndex - 1U >= primitive.lods.size()) {
        return primitive.indices.size();
    }
    const auto& indices = primitive.lods[lodIndex - 1U].indices;
    return indices.empty() ? primitive.indices.size() : indices.size();
}
[[nodiscard]] std::uint32_t selectMeshLod(
    const assets::MeshPrimitive& primitive,
    float boundsRadius,
    float depth,
    float verticalFovRadians,
    float viewportHeight) noexcept
{
    if (primitive.lods.empty()
        || boundsRadius <= 0.0F
        || depth <= 0.05F
        || viewportHeight < 1.0F
        || !std::isfinite(boundsRadius)
        || !std::isfinite(depth)) {
        return 0U;
    }
    const auto projectionScale = (viewportHeight * 0.5F) / std::max(std::tan(verticalFovRadians * 0.5F), 0.001F);
    const auto projectedRadius = boundsRadius * projectionScale / std::max(depth, 0.05F);
    if (!std::isfinite(projectedRadius)) {
        return 0U;
    }
    const auto available = static_cast<std::uint32_t>(std::min<std::size_t>(primitive.lods.size(), 3U));
    std::uint32_t requested = 0U;
    if (projectedRadius < 80.0F) {
        requested = available;
    } else if (projectedRadius < 160.0F) {
        requested = std::min<std::uint32_t>(2U, available);
    } else if (projectedRadius < 320.0F) {
        requested = std::min<std::uint32_t>(1U, available);
    }
    while (requested > 0U && indexCountForLod(primitive, requested) >= primitive.indices.size()) {
        --requested;
    }
    return requested;
}
[[nodiscard]] math::Vec3 safeNormalized(math::Vec3 value, math::Vec3 fallback)
{
    const auto length = value.length();
    if (length <= 0.00001F || !std::isfinite(length)) {
        return fallback;
    }
    return value / length;
}
[[nodiscard]] renderer::RenderLightType renderLightType(assets::ImportedLightType type)
{
    switch (type) {
    case assets::ImportedLightType::Directional:
        return renderer::RenderLightType::Directional;
    case assets::ImportedLightType::Point:
        return renderer::RenderLightType::Point;
    case assets::ImportedLightType::Spot:
        return renderer::RenderLightType::Spot;
    }
    return renderer::RenderLightType::Directional;
}
struct FrameBounds {
    bool valid {false};
    math::Vec3 minimum;
    math::Vec3 maximum;
    void includeSphere(math::Vec3 center, float radius)
    {
        if (!std::isfinite(radius) || radius < 0.0F) {
            return;
        }
        const math::Vec3 extent {radius, radius, radius};
        if (!valid) {
            minimum = center - extent;
            maximum = center + extent;
            valid = true;
            return;
        }
        minimum.x = std::min(minimum.x, center.x - radius);
        minimum.y = std::min(minimum.y, center.y - radius);
        minimum.z = std::min(minimum.z, center.z - radius);
        maximum.x = std::max(maximum.x, center.x + radius);
        maximum.y = std::max(maximum.y, center.y + radius);
        maximum.z = std::max(maximum.z, center.z + radius);
    }
    [[nodiscard]] math::Vec3 center() const
    {
        return (minimum + maximum) * 0.5F;
    }
    [[nodiscard]] float radius() const
    {
        return (maximum - center()).length();
    }
};
struct ViewportCameraFrame {
    math::Vec3 eye;
    math::Vec3 right;
    math::Vec3 up;
    math::Vec3 forward;
    float verticalFovRadians {1.04719755F};
    float aspectRatio {1.0F};
    float nearPlane {0.05F};
    float farPlane {4000.0F};
};
void appendLineQuad(
    std::vector<renderer::RenderColorVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    math::Vec3 start,
    math::Vec3 end,
    std::array<float, 4> color,
    float thickness,
    math::Vec3 cameraForward,
    math::Vec3 cameraRight,
    float cameraDistance)
{
    const auto direction = end - start;
    if (direction.lengthSquared() <= 0.0000001F) {
        return;
    }
    const auto side = safeNormalized(math::cross(direction, cameraForward), cameraRight);
    const auto halfWidth = std::clamp(cameraDistance * 0.00085F * std::max(thickness, 1.0F), 0.006F, 0.08F);
    const auto offset = side * halfWidth;
    const auto base = static_cast<std::uint32_t>(vertices.size());
    vertices.push_back({{start.x - offset.x, start.y - offset.y, start.z - offset.z}, color});
    vertices.push_back({{start.x + offset.x, start.y + offset.y, start.z + offset.z}, color});
    vertices.push_back({{end.x + offset.x, end.y + offset.y, end.z + offset.z}, color});
    vertices.push_back({{end.x - offset.x, end.y - offset.y, end.z - offset.z}, color});
    indices.insert(indices.end(), {base, base + 1U, base + 2U, base, base + 2U, base + 3U});
}
void appendGrid(
    std::vector<renderer::RenderColorVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    math::Vec3 cameraForward,
    math::Vec3 cameraRight,
    float cameraDistance)
{
    constexpr int divisions = 40;
    constexpr float halfExtent = 20.0F;
    constexpr float spacing = halfExtent * 2.0F / static_cast<float>(divisions);
    for (int line = 0; line <= divisions; ++line) {
        const auto coordinate = -halfExtent + static_cast<float>(line) * spacing;
        const auto centerLine = std::abs(coordinate) <= 0.0001F;
        const auto color = centerLine
            ? std::array<float, 4> {0.68F, 0.72F, 0.80F, 0.58F}
            : std::array<float, 4> {0.54F, 0.58F, 0.64F, 0.24F};
        appendLineQuad(
            vertices,
            indices,
            {-halfExtent, 0.0F, coordinate},
            {halfExtent, 0.0F, coordinate},
            color,
            centerLine ? 1.4F : 1.0F,
            cameraForward,
            cameraRight,
            cameraDistance);
        appendLineQuad(
            vertices,
            indices,
            {coordinate, 0.0F, -halfExtent},
            {coordinate, 0.0F, halfExtent},
            color,
            centerLine ? 1.4F : 1.0F,
            cameraForward,
            cameraRight,
            cameraDistance);
    }
}
void appendAxes(
    std::vector<renderer::RenderColorVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    math::Vec3 cameraForward,
    math::Vec3 cameraRight,
    float cameraDistance)
{
    appendLineQuad(vertices, indices, {}, {3.0F, 0.0F, 0.0F}, {0.86F, 0.31F, 0.31F, 0.95F}, 2.0F, cameraForward, cameraRight, cameraDistance);
    appendLineQuad(vertices, indices, {}, {0.0F, 3.0F, 0.0F}, {0.37F, 0.75F, 0.43F, 0.95F}, 2.0F, cameraForward, cameraRight, cameraDistance);
    appendLineQuad(vertices, indices, {}, {0.0F, 0.0F, 3.0F}, {0.31F, 0.53F, 0.90F, 0.95F}, 2.0F, cameraForward, cameraRight, cameraDistance);
}
void appendHierarchyLinks(
    std::vector<renderer::RenderColorVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    const scene::Scene& scene,
    const auto& worldPositionFor,
    math::Vec3 cameraForward,
    math::Vec3 cameraRight,
    float cameraDistance)
{
    for (const auto& entity : scene.entities()) {
        if (!entity.parent.has_value()) {
            continue;
        }
        const auto childPosition = worldPositionFor(entity.id);
        const auto parentPosition = worldPositionFor(*entity.parent);
        if (!childPosition.has_value() || !parentPosition.has_value()) {
            continue;
        }
        appendLineQuad(
            vertices,
            indices,
            *parentPosition,
            *childPosition,
            {0.63F, 0.67F, 0.73F, 0.38F},
            1.0F,
            cameraForward,
            cameraRight,
            cameraDistance);
    }
}
void appendEntityMarker(
    std::vector<renderer::RenderColorVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    math::Vec3 position,
    float halfSize,
    bool selected,
    math::Vec3 cameraForward,
    math::Vec3 cameraRight,
    math::Vec3 cameraUp,
    float cameraDistance)
{
    const auto color = selected
        ? std::array<float, 4> {1.0F, 0.76F, 0.24F, 0.95F}
        : std::array<float, 4> {0.74F, 0.80F, 0.90F, 0.72F};
    const std::array<math::Vec3, 8> corners {
        position + math::Vec3 {-halfSize, -halfSize, -halfSize},
        position + math::Vec3 {halfSize, -halfSize, -halfSize},
        position + math::Vec3 {halfSize, halfSize, -halfSize},
        position + math::Vec3 {-halfSize, halfSize, -halfSize},
        position + math::Vec3 {-halfSize, -halfSize, halfSize},
        position + math::Vec3 {halfSize, -halfSize, halfSize},
        position + math::Vec3 {halfSize, halfSize, halfSize},
        position + math::Vec3 {-halfSize, halfSize, halfSize},
    };
    constexpr std::array<std::pair<int, int>, 12> edges {{
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7},
    }};
    for (const auto& edge : edges) {
        appendLineQuad(
            vertices,
            indices,
            corners[static_cast<std::size_t>(edge.first)],
            corners[static_cast<std::size_t>(edge.second)],
            color,
            selected ? 1.7F : 1.1F,
            cameraForward,
            cameraRight,
            cameraDistance);
    }
    const auto pivotRadius = std::clamp(halfSize * 0.22F, 0.05F, 0.18F);
    appendLineQuad(vertices, indices, position - cameraRight * pivotRadius, position + cameraRight * pivotRadius, color, selected ? 2.1F : 1.4F, cameraForward, cameraRight, cameraDistance);
    appendLineQuad(vertices, indices, position - cameraUp * pivotRadius, position + cameraUp * pivotRadius, color, selected ? 2.1F : 1.4F, cameraForward, cameraRight, cameraDistance);
}
} // namespace
bool ViewportWidget::ensureRendererSurface()
{
    if (renderer_ == nullptr || !renderer_->isReady() || width() <= 0 || height() <= 0) {
        return false;
    }
    auto* handle = reinterpret_cast<void*>(winId());
    if (handle == nullptr) {
        return false;
    }
    const auto currentWidth = width();
    const auto currentHeight = height();
    if (rendererSurfaceAttempted_
        && rendererSurfaceHandle_ == handle
        && rendererSurfaceWidth_ == currentWidth
        && rendererSurfaceHeight_ == currentHeight) {
        return rendererSurfaceReady_;
    }
    rendererSurfaceHandle_ = handle;
    rendererSurfaceWidth_ = currentWidth;
    rendererSurfaceHeight_ = currentHeight;
    rendererSurfaceAttempted_ = true;
    renderer::ViewportRenderSurfaceDesc desc;
    desc.nativeWindowHandle = handle;
    desc.width = static_cast<std::uint32_t>(currentWidth);
    desc.height = static_cast<std::uint32_t>(currentHeight);
    desc.vsync = true;
    std::string error;
    rendererSurfaceReady_ = renderer_->prepareSurface(desc, &error);
    if (!rendererSurfaceReady_) {
        core::logWarning(
            core::LogCategory::Renderer,
            QStringLiteral("Viewport Vulkan surface unavailable: %1")
                .arg(QString::fromStdString(error.empty() ? "unknown error" : error))
                .toStdString());
    }
    return rendererSurfaceReady_;
}
bool ViewportWidget::renderRendererFrame()
{
    gpuMeshFrameRendered_ = false;
    if (!ensureRendererSurface() || renderer_ == nullptr || rendererSurfaceHandle_ == nullptr) {
        return false;
    }
    renderer::ViewportRenderSurfaceDesc desc;
    desc.nativeWindowHandle = rendererSurfaceHandle_;
    desc.width = static_cast<std::uint32_t>(rendererSurfaceWidth_);
    desc.height = static_cast<std::uint32_t>(rendererSurfaceHeight_);
    desc.vsync = true;
    renderer::RenderFrame frame;
    frame.clearColor.red = mode_ == ViewportMode::Scene ? 0.12F : 0.02F;
    frame.clearColor.green = mode_ == ViewportMode::Scene ? 0.13F : 0.02F;
    frame.clearColor.blue = mode_ == ViewportMode::Scene ? 0.15F : 0.025F;
    frame.clearColor.alpha = 1.0F;
    frame.environment = environmentSettings_;
    rendererMeshDraws_.clear();
    rendererLights_.clear();
    bool hasMeshSceneContent = false;
    FrameBounds visibleBounds;
    ViewportCameraFrame cameraFrame {
        cameraPosition(),
        cameraRight(),
        cameraUp(),
        cameraForward(),
        camera_.verticalFovRadians,
        aspectRatio(),
    };
    if (mode_ == ViewportMode::Game && scene_ != nullptr && assetManager_ != nullptr) {
        for (const auto& entity : scene_->entities()) {
            if (!entity.meshRenderer.has_value()) {
                continue;
            }
            const auto model = assetManager_->model(entity.meshRenderer->modelAssetId);
            const auto worldPosition = entityWorldPosition(entity.id);
            if (model == nullptr || !worldPosition.has_value() || model->cameras.empty()) {
                continue;
            }
            const auto imported = std::find_if(model->cameras.begin(), model->cameras.end(), [](const assets::ImportedCameraAsset& camera) {
                return camera.projection == assets::ImportedCameraProjection::Perspective;
            });
            if (imported == model->cameras.end()) {
                continue;
            }
            cameraFrame.eye = transformPoint(entity, *worldPosition, imported->position);
            cameraFrame.forward = safeNormalized(
                rotateEuler(imported->direction, entity.transform.rotationEuler),
                cameraFrame.forward);
            cameraFrame.right = safeNormalized(
                rotateEuler(imported->right, entity.transform.rotationEuler),
                cameraFrame.right);
            cameraFrame.up = safeNormalized(
                rotateEuler(imported->up, entity.transform.rotationEuler),
                cameraFrame.up);
            cameraFrame.right = safeNormalized(
                cameraFrame.right - cameraFrame.forward * math::dot(cameraFrame.right, cameraFrame.forward),
                safeNormalized(math::cross(cameraFrame.forward, cameraFrame.up), cameraFrame.right));
            cameraFrame.up = safeNormalized(math::cross(cameraFrame.right, cameraFrame.forward), cameraFrame.up);
            cameraFrame.verticalFovRadians = imported->verticalFovRadians;
            cameraFrame.aspectRatio = imported->aspectRatio > 0.0F ? imported->aspectRatio : aspectRatio();
            cameraFrame.nearPlane = imported->nearPlane;
            cameraFrame.farPlane = imported->farPlane;
            break;
        }
    }
    const auto& right = cameraFrame.right;
    const auto& up = cameraFrame.up;
    const auto& forward = cameraFrame.forward;
    const auto& eye = cameraFrame.eye;
    const auto view = viewMatrix(eye, right, up, forward);
    const auto projection = perspectiveMatrix(
        cameraFrame.verticalFovRadians,
        cameraFrame.aspectRatio,
        cameraFrame.nearPlane,
        cameraFrame.farPlane);
    const auto viewProjection = multiply(projection, view);
    frame.viewProjection = viewProjection;
    frame.cameraPosition = {eye.x, eye.y, eye.z};
    if (scene_ != nullptr && assetManager_ != nullptr) {
        for (const auto& entity : scene_->entities()) {
            if (!entity.meshRenderer.has_value()) {
                continue;
            }
            const auto worldPosition = entityWorldPosition(entity.id);
            const auto model = assetManager_->model(entity.meshRenderer->modelAssetId);
            if (!worldPosition.has_value() || model == nullptr) {
                continue;
            }
            for (const auto& importedLight : model->lights) {
                if (rendererLights_.size() >= renderer::kMaxFrameLights) {
                    break;
                }
                const auto position = transformPoint(entity, *worldPosition, importedLight.position);
                const auto direction = safeNormalized(
                    rotateEuler(importedLight.direction, entity.transform.rotationEuler),
                    {0.35F, -0.82F, 0.45F});
                renderer::RenderLight light;
                light.type = renderLightType(importedLight.type);
                light.position = {position.x, position.y, position.z};
                light.direction = {direction.x, direction.y, direction.z};
                light.color = importedLight.color;
                light.intensity = importedLight.intensity;
                light.range = importedLight.range * maxAbsScale(entity.transform.scale);
                light.innerConeAngle = importedLight.innerConeAngle;
                light.outerConeAngle = importedLight.outerConeAngle;
                rendererLights_.push_back(light);
            }
            hasMeshSceneContent = hasMeshSceneContent || !model->primitives.empty();
            const auto entityModelMatrix = modelMatrix(entity, *worldPosition);
            const auto modelTexture = [&model](std::optional<std::size_t> textureIndex) {
                return textureIndex.has_value() && *textureIndex < model->textures.size()
                    ? &model->textures[*textureIndex]
                    : nullptr;
            };
            const auto submitPrimitive = [&](std::size_t primitiveIndex, const renderer::RenderMatrix4& drawModelMatrix, bool flipsWinding) {
                if (primitiveIndex >= model->primitives.size()) {
                    return;
                }
                const auto& primitive = model->primitives[primitiveIndex];
                if (primitive.materialIndex >= model->materials.size()) {
                    return;
                }
                ++frame.candidateMeshDrawCount;
                const auto sourceTriangleCount = static_cast<std::uint64_t>(primitive.indices.size() / 3U);
                frame.candidateTriangleCount += sourceTriangleCount;
                const auto boundsCenter = transformPoint(drawModelMatrix, primitive.bounds.center);
                const auto boundsRadius = primitive.bounds.radius * maxScale(drawModelMatrix);
                if (!sphereVisible(
                        boundsCenter,
                        boundsRadius,
                        eye,
                        right,
                        up,
                        forward,
                        cameraFrame.verticalFovRadians,
                        cameraFrame.aspectRatio)) {
                    ++frame.culledMeshDrawCount;
                    frame.culledTriangleCount += sourceTriangleCount;
                    return;
                }
                visibleBounds.includeSphere(boundsCenter, boundsRadius);
                const auto& material = model->materials[primitive.materialIndex];
                const auto mvp = multiply(viewProjection, drawModelMatrix);
                const auto sortDepth = math::dot(boundsCenter - eye, forward);
                const auto lodIndex = entity.id == selectedEntityId_
                    ? 0U
                    : selectMeshLod(
                        primitive,
                        boundsRadius,
                        sortDepth,
                        cameraFrame.verticalFovRadians,
                        static_cast<float>(std::max(height(), 1)));
                const auto selectedTriangleCount = static_cast<std::uint64_t>(indexCountForLod(primitive, lodIndex) / 3U);
                if (lodIndex > 0U && selectedTriangleCount < sourceTriangleCount) {
                    ++frame.lodMeshDrawCount;
                    frame.lodTriangleReductionCount += sourceTriangleCount - selectedTriangleCount;
                }
                rendererMeshDraws_.push_back({
                    model->id,
                    static_cast<std::uint32_t>(primitiveIndex),
                    lodIndex,
                    &primitive,
                    &material,
                    modelTexture(material.baseColorTexture),
                    modelTexture(material.normalTexture),
                    modelTexture(material.metallicRoughnessTexture),
                    modelTexture(material.occlusionTexture),
                    modelTexture(material.emissiveTexture),
                    sortDepth,
                    {boundsCenter.x, boundsCenter.y, boundsCenter.z},
                    boundsRadius,
                    drawModelMatrix,
                    mvp,
                    flipsWinding,
                });
            };
            if (!model->primitiveInstances.empty()) {
                for (const auto& instance : model->primitiveInstances) {
                    submitPrimitive(
                        instance.primitiveIndex,
                        multiply(entityModelMatrix, renderMatrix(instance.transform)),
                        instance.flipsWinding);
                }
            } else {
                for (std::size_t primitiveIndex = 0; primitiveIndex < model->primitives.size(); ++primitiveIndex) {
                    submitPrimitive(primitiveIndex, entityModelMatrix, false);
                }
            }
        }
    }
    if (rendererLights_.empty()) {
        rendererLights_.push_back({});
    }
    if (visibleBounds.valid) {
        const auto center = visibleBounds.center();
        frame.visibleBoundsCenter = {center.x, center.y, center.z};
        frame.visibleBoundsRadius = visibleBounds.radius();
    } else {
        frame.visibleBoundsCenter = {camera_.target.x, camera_.target.y, camera_.target.z};
        frame.visibleBoundsRadius = std::max(camera_.distance, 1.0F);
    }
    frame.lights = std::span<const renderer::RenderLight>(rendererLights_);
    const auto shadowSelection = renderer::chooseShadowMap(
        frame.lights,
        frame.visibleBoundsCenter,
        frame.visibleBoundsRadius);
    if (shadowSelection.enabled) {
        frame.shadowViewProjection = shadowSelection.viewProjection;
        frame.shadowLightIndex = shadowSelection.lightIndex;
        frame.shadowsEnabled = true;
    }
    frame.meshDraws = std::span<const renderer::RenderMeshDraw>(rendererMeshDraws_);
    rendererGizmoVertices_.clear();
    rendererGizmoIndices_.clear();
    rendererColorMeshDraws_.clear();
    if (mode_ == ViewportMode::Scene) {
        appendGrid(rendererGizmoVertices_, rendererGizmoIndices_, forward, right, camera_.distance);
        appendAxes(rendererGizmoVertices_, rendererGizmoIndices_, forward, right, camera_.distance);
        if (scene_ != nullptr) {
            appendHierarchyLinks(
                rendererGizmoVertices_,
                rendererGizmoIndices_,
                *scene_,
                [this](scene::EntityId id) {
                    return entityWorldPosition(id);
                },
                forward,
                right,
                camera_.distance);
            const ViewportLabelCamera labelCamera {
                eye,
                right,
                up,
                forward,
                cameraFrame.verticalFovRadians,
                static_cast<float>(std::max(height(), 1)),
            };
            std::size_t labelsSubmitted = 0;
            for (const auto& entity : scene_->entities()) {
                if (labelsSubmitted >= 128U) {
                    break;
                }
                const auto position = entityWorldPosition(entity.id);
                if (!position.has_value()) {
                    continue;
                }
                if (!entity.meshRenderer.has_value()) {
                    appendEntityMarker(
                        rendererGizmoVertices_,
                        rendererGizmoIndices_,
                        *position,
                        entityPickRadius(entity),
                        entity.id == selectedEntityId_,
                        forward,
                        right,
                        up,
                        camera_.distance);
                }
                appendViewportLabel(
                    rendererGizmoVertices_,
                    rendererGizmoIndices_,
                    labelCamera,
                    *position,
                    entity.name,
                    entity.id == selectedEntityId_);
                ++labelsSubmitted;
            }
        }
        const auto vertexCountBeforeGizmo = rendererGizmoVertices_.size();
        const auto indexCountBeforeGizmo = rendererGizmoIndices_.size();
        const auto gizmoVertexOffset = static_cast<std::uint32_t>(rendererGizmoVertices_.size());
        (void)updateGizmoFrame();
        const auto& gizmo = gizmoBackend_->mesh();
        rendererGizmoVertices_.reserve(rendererGizmoVertices_.size() + gizmo.vertices.size());
        rendererGizmoIndices_.reserve(rendererGizmoIndices_.size() + gizmo.triangles.size() * 3U);
        for (const auto& vertex : gizmo.vertices) {
            rendererGizmoVertices_.push_back({
                {vertex.position.x, vertex.position.y, vertex.position.z},
                vertex.color,
            });
        }
        for (const auto& triangle : gizmo.triangles) {
            if (triangle.x >= gizmo.vertices.size() || triangle.y >= gizmo.vertices.size() || triangle.z >= gizmo.vertices.size()) {
                continue;
            }
            rendererGizmoIndices_.insert(
                rendererGizmoIndices_.end(),
                {gizmoVertexOffset + triangle.x, gizmoVertexOffset + triangle.y, gizmoVertexOffset + triangle.z});
        }
        if (!rendererGizmoVertices_.empty() && !rendererGizmoIndices_.empty()) {
            rendererColorMeshDraws_.push_back({
                std::span<const renderer::RenderColorVertex>(rendererGizmoVertices_),
                std::span<const std::uint32_t>(rendererGizmoIndices_),
                viewProjection,
            });
        }
        if (selectedEntityId_.isValid()
            && rendererGizmoVertices_.size() == vertexCountBeforeGizmo
            && rendererGizmoIndices_.size() == indexCountBeforeGizmo) {
            core::logWarning(
                core::LogCategory::Renderer,
                QStringLiteral("Selected viewport gizmo produced no Vulkan color geometry: vertices=%1 indices=%2")
                    .arg(static_cast<qulonglong>(gizmo.vertices.size()))
                    .arg(static_cast<qulonglong>(gizmo.triangles.size() * 3U))
                    .toStdString());
        }
    }
    frame.colorMeshDraws = std::span<const renderer::RenderColorMeshDraw>(rendererColorMeshDraws_);
    if (rendererMeshDraws_.empty() && rendererColorMeshDraws_.empty() && !hasMeshSceneContent) {
        return false;
    }
    std::string error;
    if (renderer_->renderSurfaceFrame(desc, frame, &error)) {
        gpuMeshFrameRendered_ = !rendererMeshDraws_.empty();
        return true;
    }
    gpuMeshFrameRendered_ = false;
    rendererSurfaceAttempted_ = false;
    rendererSurfaceReady_ = false;
    core::logWarning(
        core::LogCategory::Renderer,
        QStringLiteral("Viewport Vulkan frame unavailable: %1")
            .arg(QString::fromStdString(error.empty() ? "unknown error" : error))
            .toStdString());
    return false;
}
} // namespace projectunity::editor
