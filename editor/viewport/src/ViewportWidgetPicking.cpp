#include <projectunity/editor/ViewportWidget.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

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

[[nodiscard]] std::optional<math::Vec3> inverseTransformPoint(
    const renderer::RenderMatrix4& matrix,
    math::Vec3 point)
{
    const auto a00 = at(matrix, 0, 0);
    const auto a01 = at(matrix, 0, 1);
    const auto a02 = at(matrix, 0, 2);
    const auto a10 = at(matrix, 1, 0);
    const auto a11 = at(matrix, 1, 1);
    const auto a12 = at(matrix, 1, 2);
    const auto a20 = at(matrix, 2, 0);
    const auto a21 = at(matrix, 2, 1);
    const auto a22 = at(matrix, 2, 2);
    const auto c00 = a11 * a22 - a12 * a21;
    const auto c01 = a02 * a21 - a01 * a22;
    const auto c02 = a01 * a12 - a02 * a11;
    const auto determinant = a00 * c00 + a10 * c01 + a20 * c02;
    if (std::fabs(determinant) <= 0.0000001F || !std::isfinite(determinant)) {
        return std::nullopt;
    }
    const auto invDet = 1.0F / determinant;
    const auto value = point - math::Vec3 {at(matrix, 0, 3), at(matrix, 1, 3), at(matrix, 2, 3)};
    return math::Vec3 {
        (c00 * value.x + (a12 * a20 - a10 * a22) * value.y + (a10 * a21 - a11 * a20) * value.z) * invDet,
        (c01 * value.x + (a00 * a22 - a02 * a20) * value.y + (a01 * a20 - a00 * a21) * value.z) * invDet,
        (c02 * value.x + (a02 * a10 - a00 * a12) * value.y + (a00 * a11 - a01 * a10) * value.z) * invDet,
    };
}

[[nodiscard]] std::optional<ViewportRay> inverseTransformRay(
    const renderer::RenderMatrix4& matrix,
    const ViewportRay& ray)
{
    const auto localOrigin = inverseTransformPoint(matrix, ray.origin);
    const auto localEnd = inverseTransformPoint(matrix, ray.origin + ray.direction);
    if (!localOrigin.has_value() || !localEnd.has_value()) {
        return std::nullopt;
    }
    return ViewportRay {*localOrigin, *localEnd - *localOrigin};
}

struct PickCandidate {
    scene::EntityId id;
    float priority {0.0F};
    float distance {0.0F};
};

[[nodiscard]] bool betterPick(const PickCandidate& candidate, const PickCandidate& current) noexcept
{
    if (candidate.priority != current.priority) {
        return candidate.priority < current.priority;
    }
    return candidate.distance < current.distance;
}

[[nodiscard]] bool isAggregateMesh(const scene::Entity& entity)
{
    return entity.meshRenderer.has_value()
        && entity.meshRenderer->renderable
        && !entity.meshRenderer->primitiveInstanceIndex.has_value()
        && !entity.children.empty();
}

[[nodiscard]] bool isPrimitiveProxy(const scene::Entity& entity)
{
    return entity.meshRenderer.has_value()
        && !entity.meshRenderer->renderable
        && entity.meshRenderer->primitiveInstanceIndex.has_value();
}

[[nodiscard]] bool hasPrimitiveProxyChildren(const scene::Scene& scene, const scene::Entity& entity)
{
    return std::any_of(entity.children.begin(), entity.children.end(), [&scene](scene::EntityId id) {
        const auto* child = scene.findEntity(id);
        return child != nullptr && isPrimitiveProxy(*child);
    });
}

[[nodiscard]] bool rayIntersectsSphere(
    const ViewportRay& ray,
    math::Vec3 center,
    float radius) noexcept
{
    const auto oc = ray.origin - center;
    const auto b = math::dot(oc, ray.direction);
    const auto c = math::dot(oc, oc) - radius * radius;
    return b * b - c >= 0.0F;
}

[[nodiscard]] std::optional<float> rayTriangleDistance(
    const ViewportRay& ray,
    math::Vec3 a,
    math::Vec3 b,
    math::Vec3 c) noexcept
{
    constexpr float epsilon = 0.0000001F;
    const auto edge1 = b - a;
    const auto edge2 = c - a;
    const auto h = math::cross(ray.direction, edge2);
    const auto determinant = math::dot(edge1, h);
    if (std::fabs(determinant) <= epsilon) {
        return std::nullopt;
    }
    const auto invDeterminant = 1.0F / determinant;
    const auto s = ray.origin - a;
    const auto u = invDeterminant * math::dot(s, h);
    if (u < 0.0F || u > 1.0F) {
        return std::nullopt;
    }
    const auto q = math::cross(s, edge1);
    const auto v = invDeterminant * math::dot(ray.direction, q);
    if (v < 0.0F || u + v > 1.0F) {
        return std::nullopt;
    }
    const auto distance = invDeterminant * math::dot(edge2, q);
    if (distance < 0.0F || !std::isfinite(distance)) {
        return std::nullopt;
    }
    return distance;
}

[[nodiscard]] std::optional<float> rayPrimitiveDistance(
    const ViewportRay& worldRay,
    const assets::MeshPrimitive& primitive,
    const renderer::RenderMatrix4& matrix)
{
    const auto boundsCenter = transformPoint(matrix, primitive.bounds.center);
    const auto boundsRadius = primitive.bounds.radius * maxScale(matrix);
    if (!rayIntersectsSphere(worldRay, boundsCenter, boundsRadius)) {
        return std::nullopt;
    }
    const auto localRay = inverseTransformRay(matrix, worldRay);
    if (!localRay.has_value()) {
        return std::nullopt;
    }

    std::optional<float> closest;
    for (std::size_t index = 2; index < primitive.indices.size(); index += 3U) {
        const auto i0 = primitive.indices[index - 2U];
        const auto i1 = primitive.indices[index - 1U];
        const auto i2 = primitive.indices[index];
        if (i0 >= primitive.vertices.size() || i1 >= primitive.vertices.size() || i2 >= primitive.vertices.size()) {
            continue;
        }
        const auto distance = rayTriangleDistance(
            *localRay,
            primitive.vertices[i0].position,
            primitive.vertices[i1].position,
            primitive.vertices[i2].position);
        if (distance.has_value() && (!closest.has_value() || *distance < *closest)) {
            closest = distance;
        }
    }
    return closest;
}

[[nodiscard]] std::optional<float> rayInstanceDistance(
    const ViewportRay& ray,
    const assets::ModelAsset& model,
    const assets::MeshPrimitiveInstance& instance,
    const renderer::RenderMatrix4& matrix)
{
    if (instance.primitiveIndex >= model.primitives.size()) {
        return std::nullopt;
    }
    return rayPrimitiveDistance(ray, model.primitives[instance.primitiveIndex], matrix);
}

} // namespace

std::optional<scene::EntityId> ViewportWidget::pickEntityAt(QPointF point) const
{
    if (scene_ == nullptr) {
        return std::nullopt;
    }

    const auto ray = screenPointToRay(point);
    std::optional<PickCandidate> meshPick;
    if (assetManager_ != nullptr) {
        for (const auto& entity : scene_->entities()) {
            if (!entity.meshRenderer.has_value()) {
                continue;
            }
            const auto worldPosition = entityWorldPosition(entity.id);
            const auto model = assetManager_->model(entity.meshRenderer->modelAssetId);
            if (!worldPosition.has_value() || model == nullptr) {
                continue;
            }

            const auto entityMatrix = modelMatrix(entity, *worldPosition);
            const auto tryCandidate = [&](scene::EntityId id, std::optional<float> distance) {
                if (!distance.has_value()) {
                    return;
                }
                const PickCandidate candidate {id, 0.0F, *distance};
                if (!meshPick.has_value() || betterPick(candidate, *meshPick)) {
                    meshPick = candidate;
                }
            };

            if (entity.meshRenderer->primitiveInstanceIndex.has_value()) {
                const auto index = *entity.meshRenderer->primitiveInstanceIndex;
                if (index < model->primitiveInstances.size()) {
                    const auto& instance = model->primitiveInstances[index];
                    const auto instanceMatrix = multiply(
                        multiply(entityMatrix, translationMatrix(instance.bounds.center * -1.0F)),
                        renderMatrix(instance.transform));
                    tryCandidate(entity.id, rayInstanceDistance(ray, *model, instance, instanceMatrix));
                }
                continue;
            }
            if (isAggregateMesh(entity) && hasPrimitiveProxyChildren(*scene_, entity)) {
                continue;
            }
            if (!model->primitiveInstances.empty()) {
                for (const auto& instance : model->primitiveInstances) {
                    tryCandidate(
                        entity.id,
                        rayInstanceDistance(ray, *model, instance, multiply(entityMatrix, renderMatrix(instance.transform))));
                }
            } else {
                for (const auto& primitive : model->primitives) {
                    tryCandidate(entity.id, rayPrimitiveDistance(ray, primitive, entityMatrix));
                }
            }
        }
    }
    if (meshPick.has_value()) {
        return meshPick->id;
    }

    std::optional<PickCandidate> markerPick;
    for (const auto& entity : scene_->entities()) {
        const auto position = entityWorldPosition(entity.id);
        if (!position.has_value()) {
            continue;
        }
        const auto projected = projectPoint(*position);
        if (!projected.visible) {
            continue;
        }

        float hitRadius = 14.0F;
        float priority = 2.0F;
        if (entity.camera.has_value() || entity.light.has_value()) {
            priority = 1.0F;
            hitRadius = 22.0F;
        } else if (isPrimitiveProxy(entity)) {
            priority = 1.2F;
            hitRadius = 18.0F;
        } else if (isAggregateMesh(entity)) {
            priority = 3.0F;
            hitRadius = 16.0F;
        } else if (!entity.meshRenderer.has_value()) {
            priority = 1.1F;
            hitRadius = 18.0F;
        }
        if (entity.id == selectedEntityId_) {
            hitRadius = std::max(hitRadius, 24.0F);
        }

        const auto delta = projected.point - point;
        const auto distance = static_cast<float>(std::hypot(delta.x(), delta.y()));
        if (distance > hitRadius) {
            continue;
        }

        const PickCandidate candidate {entity.id, priority, distance};
        if (!markerPick.has_value() || betterPick(candidate, *markerPick)) {
            markerPick = candidate;
        }
    }
    if (markerPick.has_value()) {
        return markerPick->id;
    }

    float closestShapeDistance = std::numeric_limits<float>::max();
    std::optional<scene::EntityId> closestShapeEntity;

    for (const auto& entity : scene_->entities()) {
        if (isAggregateMesh(entity)) {
            continue;
        }
        const auto position = entityWorldPosition(entity.id);
        if (!position.has_value()) {
            continue;
        }

        const auto radius = entityPickRadius(entity);
        const auto oc = ray.origin - *position;
        const auto b = math::dot(oc, ray.direction);
        const auto c = math::dot(oc, oc) - radius * radius;
        const auto discriminant = b * b - c;
        if (discriminant < 0.0F) {
            continue;
        }

        const auto root = std::sqrt(discriminant);
        auto distance = -b - root;
        if (distance < 0.0F) {
            distance = -b + root;
        }

        if (distance >= 0.0F && distance < closestShapeDistance) {
            closestShapeDistance = distance;
            closestShapeEntity = entity.id;
        }
    }

    return closestShapeEntity;
}

float ViewportWidget::entityPickRadius(const scene::Entity& entity) const
{
    const auto maxScale = std::max({
        std::fabs(entity.transform.scale.x),
        std::fabs(entity.transform.scale.y),
        std::fabs(entity.transform.scale.z),
    });
    if (assetManager_ != nullptr
        && entity.meshRenderer.has_value()
        && entity.meshRenderer->primitiveInstanceIndex.has_value()) {
        const auto model = assetManager_->model(entity.meshRenderer->modelAssetId);
        const auto index = *entity.meshRenderer->primitiveInstanceIndex;
        if (model != nullptr && index < model->primitiveInstances.size()) {
            return std::clamp(model->primitiveInstances[index].bounds.radius * maxScale, 0.35F, 80.0F);
        }
    }
    if (assetManager_ != nullptr
        && entity.meshRenderer.has_value()
        && !entity.meshRenderer->primitiveInstanceIndex.has_value()) {
        const auto model = assetManager_->model(entity.meshRenderer->modelAssetId);
        if (model != nullptr) {
            float radius = 0.0F;
            if (!model->primitiveInstances.empty()) {
                for (const auto& instance : model->primitiveInstances) {
                    radius = std::max(radius, instance.bounds.center.length() + instance.bounds.radius);
                }
            } else {
                for (const auto& primitive : model->primitives) {
                    radius = std::max(radius, primitive.bounds.center.length() + primitive.bounds.radius);
                }
            }
            if (radius > 0.0F && std::isfinite(radius)) {
                return std::clamp(radius * maxScale, 0.35F, 160.0F);
            }
        }
    }
    if (entity.camera.has_value()) {
        return std::clamp(maxScale * 0.85F, 0.45F, 6.0F);
    }
    return std::clamp(maxScale * 0.55F, 0.35F, 5.0F);
}

} // namespace projectunity::editor
