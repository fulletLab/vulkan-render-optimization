#include <projectunity/physics/PhysicsWorld.hpp>

#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message)
{
    std::cerr << message << '\n';
    return EXIT_FAILURE;
}

} // namespace

int main()
{
    using namespace projectunity;

    scene::Scene scene;
    auto& terrainEntity = scene.createEntity("Terrain");
    const auto terrainId = terrainEntity.id;
    scene::TerrainComponent terrain;
    terrain.settings.width = 16.0F;
    terrain.settings.length = 16.0F;
    terrain.settings.heightScale = 4.0F;
    terrain.settings.resolution = 9U;
    terrain.settings.chunkSize = 4U;
    terrain.settings.generateCollider = true;
    terrain.heightmap.assign(terrain.settings.resolution * terrain.settings.resolution, 0.0F);
    const auto center = static_cast<std::size_t>(terrain.settings.resolution / 2U) * terrain.settings.resolution
        + terrain.settings.resolution / 2U;
    terrain.heightmap[center] = 2.0F;
    if (!scene.setTerrain(terrainId, terrain)) {
        return fail("failed to attach terrain collider");
    }

    const auto hit = physics::raycast(scene, {0.0F, 20.0F, 0.0F}, {0.0F, -1.0F, 0.0F}, 50.0F);
    if (!hit.has_value() || hit->entityId != terrainId || hit->point.y < 1.9F || hit->normal.y <= 0.0F) {
        return fail("physics terrain raycast missed heightfield");
    }

    auto& body = scene.createEntity("Body");
    const auto bodyId = body.id;
    body.transform.position = {0.0F, 1.0F, 0.0F};
    scene::RigidbodyComponent rigidbody;
    rigidbody.useGravity = false;
    scene::ColliderComponent collider;
    collider.shape = scene::ColliderShape::Sphere;
    collider.radius = 0.5F;
    if (!scene.setRigidbody(bodyId, rigidbody) || !scene.setCollider(bodyId, collider)) {
        return fail("failed to attach rigidbody/collider");
    }

    auto stats = physics::stepBasic(scene, 1.0F / 60.0F);
    const auto* solvedBody = scene.findEntity(bodyId);
    if (stats.terrainColliders != 1U || stats.dynamicBodies != 1U || stats.resolvedContacts != 1U
        || solvedBody == nullptr || solvedBody->transform.position.y < 2.49F) {
        return fail("basic terrain contact was not resolved");
    }
    const auto* solvedTerrain = scene.findEntity(terrainId);
    if (solvedTerrain == nullptr || !solvedTerrain->terrain.has_value()
        || solvedTerrain->terrain->colliderDirty) {
        return fail("terrain collider dirty flag was not cleared by physics step");
    }
    const auto cleanRevision = solvedTerrain->terrain->colliderRevision;

    auto editedTerrain = *solvedTerrain->terrain;
    editedTerrain.heightmap[center] = 3.0F;
    if (!scene.setTerrain(terrainId, editedTerrain)) {
        return fail("failed to update edited terrain");
    }
    const auto* dirtyTerrain = scene.findEntity(terrainId);
    if (dirtyTerrain == nullptr || !dirtyTerrain->terrain.has_value()
        || !dirtyTerrain->terrain->colliderDirty
        || dirtyTerrain->terrain->colliderRevision <= cleanRevision) {
        return fail("edited heightmap did not dirty terrain collider");
    }

    return EXIT_SUCCESS;
}
