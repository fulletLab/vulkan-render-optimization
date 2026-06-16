#include <projectunity/physics/PhysicsWorld.hpp>

#include <projectunity/core/Log.hpp>
#include <projectunity/terrain/TerrainGenerator.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace projectunity::physics {
namespace {

constexpr float kGravity = 9.81F;
constexpr float kEpsilon = 0.0001F;

[[nodiscard]] bool hasTerrainCollider(const scene::Entity& entity) noexcept
{
    if (!entity.terrain.has_value()) {
        return false;
    }
    return entity.terrain->settings.generateCollider
        || (entity.collider.has_value() && entity.collider->shape == scene::ColliderShape::Terrain);
}

[[nodiscard]] bool supportedTerrainTransform(const scene::TransformComponent& transform) noexcept
{
    return std::fabs(transform.rotationEuler.x) <= kEpsilon
        && std::fabs(transform.rotationEuler.y) <= kEpsilon
        && std::fabs(transform.rotationEuler.z) <= kEpsilon
        && transform.scale.x > 0.0F
        && transform.scale.y > 0.0F
        && transform.scale.z > 0.0F;
}

[[nodiscard]] math::Vec3 toTerrainLocal(math::Vec3 world, const scene::TransformComponent& transform) noexcept
{
    return {
        (world.x - transform.position.x) / transform.scale.x,
        (world.y - transform.position.y) / transform.scale.y,
        (world.z - transform.position.z) / transform.scale.z,
    };
}

[[nodiscard]] math::Vec3 terrainLocalDirection(math::Vec3 direction, const scene::TransformComponent& transform) noexcept
{
    return {
        direction.x / transform.scale.x,
        direction.y / transform.scale.y,
        direction.z / transform.scale.z,
    };
}

[[nodiscard]] math::Vec3 fromTerrainLocal(math::Vec3 local, const scene::TransformComponent& transform) noexcept
{
    return {
        transform.position.x + local.x * transform.scale.x,
        transform.position.y + local.y * transform.scale.y,
        transform.position.z + local.z * transform.scale.z,
    };
}

[[nodiscard]] math::Vec3 normalToWorld(math::Vec3 localNormal, const scene::TransformComponent& transform) noexcept
{
    return math::Vec3 {
        localNormal.x / transform.scale.x,
        localNormal.y / transform.scale.y,
        localNormal.z / transform.scale.z,
    }.normalized();
}

[[nodiscard]] float colliderBottomOffset(const scene::ColliderComponent& collider, math::Vec3 scale) noexcept
{
    const auto maxHorizontalScale = std::max(std::fabs(scale.x), std::fabs(scale.z));
    switch (collider.shape) {
    case scene::ColliderShape::Sphere:
        return collider.radius * maxHorizontalScale;
    case scene::ColliderShape::Capsule:
        return std::max(collider.radius * maxHorizontalScale, collider.height * 0.5F * std::fabs(scale.y));
    case scene::ColliderShape::Box:
    case scene::ColliderShape::Mesh:
        return collider.size.y * 0.5F * std::fabs(scale.y);
    case scene::ColliderShape::Terrain:
        return 0.0F;
    }
    return 0.0F;
}

[[nodiscard]] std::optional<PhysicsRaycastHit> sampleTerrainEntity(
    const scene::Entity& entity,
    math::Vec3 worldPosition)
{
    if (!hasTerrainCollider(entity) || !supportedTerrainTransform(entity.transform)) {
        return std::nullopt;
    }
    const auto local = toTerrainLocal(worldPosition, entity.transform);
    const auto height = terrain::TerrainGenerator::sampleHeight(
        entity.terrain->heightmap,
        entity.terrain->settings,
        local.x,
        local.z);
    if (!height.has_value()) {
        return std::nullopt;
    }
    const auto localNormal = terrain::TerrainGenerator::surfaceNormal(
        entity.terrain->heightmap,
        entity.terrain->settings,
        local.x,
        local.z);
    const auto worldPoint = fromTerrainLocal({local.x, *height, local.z}, entity.transform);
    PhysicsRaycastHit hit;
    hit.entityId = entity.id;
    hit.colliderShape = scene::ColliderShape::Terrain;
    hit.point = worldPoint;
    hit.normal = normalToWorld(localNormal, entity.transform);
    hit.distance = std::fabs(worldPosition.y - worldPoint.y);
    return hit;
}

} // namespace

std::optional<PhysicsRaycastHit> sampleTerrain(const scene::Scene& scene, math::Vec3 worldPosition)
{
    std::optional<PhysicsRaycastHit> best;
    for (const auto& entity : scene.entities()) {
        const auto hit = sampleTerrainEntity(entity, worldPosition);
        if (!hit.has_value()) {
            continue;
        }
        if (!best.has_value() || hit->point.y > best->point.y) {
            best = hit;
        }
    }
    return best;
}

std::optional<PhysicsRaycastHit> raycast(
    const scene::Scene& scene,
    math::Vec3 origin,
    math::Vec3 direction,
    float maxDistance)
{
    const auto directionLength = direction.length();
    if (directionLength <= kEpsilon || !(maxDistance > 0.0F)) {
        return std::nullopt;
    }
    direction = direction / directionLength;

    std::optional<PhysicsRaycastHit> best;
    for (const auto& entity : scene.entities()) {
        if (!hasTerrainCollider(entity) || !supportedTerrainTransform(entity.transform)) {
            continue;
        }
        const auto localOrigin = toTerrainLocal(origin, entity.transform);
        const auto localDirection = terrainLocalDirection(direction, entity.transform).normalized();
        const auto localHit = terrain::TerrainGenerator::raycast(
            entity.terrain->heightmap,
            entity.terrain->settings,
            localOrigin,
            localDirection);
        if (!localHit.has_value()) {
            continue;
        }
        const auto worldPoint = fromTerrainLocal(*localHit, entity.transform);
        const auto distance = (worldPoint - origin).length();
        if (distance > maxDistance || (best.has_value() && distance >= best->distance)) {
            continue;
        }
        const auto localNormal = terrain::TerrainGenerator::surfaceNormal(
            entity.terrain->heightmap,
            entity.terrain->settings,
            localHit->x,
            localHit->z);
        PhysicsRaycastHit hit;
        hit.entityId = entity.id;
        hit.colliderShape = scene::ColliderShape::Terrain;
        hit.point = worldPoint;
        hit.normal = normalToWorld(localNormal, entity.transform);
        hit.distance = distance;
        best = hit;
    }
    return best;
}

PhysicsStepStats stepBasic(scene::Scene& scene, float deltaTimeSeconds)
{
    PhysicsStepStats stats;
    std::vector<scene::EntityId> entityIds;
    entityIds.reserve(scene.entities().size());
    for (const auto& entity : scene.entities()) {
        entityIds.push_back(entity.id);
    }

    for (const auto id : entityIds) {
        auto* entity = scene.findEntity(id);
        if (entity == nullptr) {
            continue;
        }
        if (hasTerrainCollider(*entity)) {
            ++stats.terrainColliders;
            if (entity->terrain.has_value()) {
                entity->terrain->colliderDirty = false;
            }
        }
    }

    const auto clampedDeltaTime = std::clamp(deltaTimeSeconds, 0.0F, 0.1F);
    for (const auto id : entityIds) {
        auto* body = scene.findEntity(id);
        if (body == nullptr || !body->rigidbody.has_value() || !body->collider.has_value()
            || !body->rigidbody->enabled || !body->collider->enabled || body->collider->trigger
            || body->collider->shape == scene::ColliderShape::Terrain) {
            continue;
        }
        ++stats.dynamicBodies;
        if (!body->rigidbody->kinematic && body->rigidbody->useGravity) {
            body->transform.position.y -= kGravity * clampedDeltaTime;
        }

        const auto colliderCenter = body->transform.position + body->collider->offset;
        const auto terrainHit = sampleTerrain(scene, colliderCenter);
        if (!terrainHit.has_value()) {
            continue;
        }
        const auto bottom = colliderCenter.y - colliderBottomOffset(*body->collider, body->transform.scale);
        const auto penetration = terrainHit->point.y - bottom;
        if (penetration < 0.0F) {
            continue;
        }
        ++stats.contacts;
        body->transform.position.y += penetration;
        ++stats.resolvedContacts;
        core::logInfo(
            core::LogCategory::Physics,
            "Basic physics resolved terrain contact for body=" + std::to_string(body->id.value())
                + " terrain=" + std::to_string(terrainHit->entityId.value())
                + " penetration=" + std::to_string(penetration));
    }
    return stats;
}

} // namespace projectunity::physics
