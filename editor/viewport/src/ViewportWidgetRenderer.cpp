#include <projectunity/editor/ViewportWidget.hpp>

#include <projectunity/core/Log.hpp>
#include <projectunity/renderer/IRenderer.hpp>

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

[[nodiscard]] math::Vec3 safeNormalized(math::Vec3 value, math::Vec3 fallback)
{
    const auto length = value.length();
    if (length <= 0.00001F || !std::isfinite(length)) {
        return fallback;
    }
    return value / length;
}

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
    rendererMeshDraws_.clear();
    bool hasMeshSceneContent = false;

    renderer::RenderMatrix4 view;
    view.values.fill(0.0F);
    const auto right = cameraRight();
    const auto up = cameraUp();
    const auto forward = cameraForward();
    const auto eye = cameraPosition();
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
    renderer::RenderMatrix4 projection;
    projection.values.fill(0.0F);
    constexpr float nearPlane = 0.05F;
    constexpr float farPlane = 4000.0F;
    const auto focal = 1.0F / std::tan(camera_.verticalFovRadians * 0.5F);
    at(projection, 0, 0) = focal / std::max(aspectRatio(), 0.001F);
    at(projection, 1, 1) = -focal;
    at(projection, 2, 2) = farPlane / (farPlane - nearPlane);
    at(projection, 2, 3) = -(nearPlane * farPlane) / (farPlane - nearPlane);
    at(projection, 3, 2) = 1.0F;
    const auto viewProjection = multiply(projection, view);

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
            hasMeshSceneContent = hasMeshSceneContent || !model->primitives.empty();
            const auto mvp = multiply(viewProjection, modelMatrix(entity, *worldPosition));
            for (std::size_t primitiveIndex = 0; primitiveIndex < model->primitives.size(); ++primitiveIndex) {
                const auto& primitive = model->primitives[primitiveIndex];
                if (primitive.materialIndex >= model->materials.size()) {
                    continue;
                }
                const auto boundsCenter = transformPoint(entity, *worldPosition, primitive.bounds.center);
                const auto boundsRadius = primitive.bounds.radius * maxAbsScale(entity.transform.scale);
                if (!sphereVisible(boundsCenter, boundsRadius, eye, right, up, forward, camera_.verticalFovRadians, aspectRatio())) {
                    continue;
                }
                const auto& material = model->materials[primitive.materialIndex];
                const assets::TextureAsset* texture = nullptr;
                if (material.baseColorTexture.has_value() && *material.baseColorTexture < model->textures.size()) {
                    texture = &model->textures[*material.baseColorTexture];
                }
                rendererMeshDraws_.push_back({
                    model->id,
                    static_cast<std::uint32_t>(primitiveIndex),
                    &primitive,
                    &material,
                    texture,
                    mvp,
                });
            }
        }
    }
    frame.meshDraws = std::span<const renderer::RenderMeshDraw>(rendererMeshDraws_);

    if (rendererMeshDraws_.empty() && !hasMeshSceneContent) {
        return false;
    }

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
