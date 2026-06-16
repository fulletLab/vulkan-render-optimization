#pragma once

#include <projectunity/math/Vec3.hpp>
#include <projectunity/scene/Scene.hpp>

#include <cstddef>
#include <optional>

namespace projectunity::physics {

struct PhysicsRaycastHit {
    scene::EntityId entityId;
    scene::ColliderShape colliderShape {scene::ColliderShape::Terrain};
    math::Vec3 point;
    math::Vec3 normal {0.0F, 1.0F, 0.0F};
    float distance {0.0F};
};

struct PhysicsContact {
    scene::EntityId bodyEntityId;
    scene::EntityId colliderEntityId;
    math::Vec3 point;
    math::Vec3 normal {0.0F, 1.0F, 0.0F};
    float penetrationDepth {0.0F};
    bool grounded {false};
};

struct PhysicsStepStats {
    std::size_t terrainColliders {0};
    std::size_t dynamicBodies {0};
    std::size_t contacts {0};
    std::size_t resolvedContacts {0};
};

[[nodiscard]] std::optional<PhysicsRaycastHit> raycast(
    const scene::Scene& scene,
    math::Vec3 origin,
    math::Vec3 direction,
    float maxDistance = 100000.0F);

[[nodiscard]] std::optional<PhysicsRaycastHit> sampleTerrain(
    const scene::Scene& scene,
    math::Vec3 worldPosition);

[[nodiscard]] PhysicsStepStats stepBasic(scene::Scene& scene, float deltaTimeSeconds);

} // namespace projectunity::physics
