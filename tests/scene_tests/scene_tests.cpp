#include <projectunity/scene/Scene.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace {

int fail(const char* message)
{
    std::cerr << message << '\n';
    return EXIT_FAILURE;
}

bool closeEnough(float lhs, float rhs)
{
    return std::fabs(lhs - rhs) <= 0.0001F;
}

} // namespace

int main()
{
    using namespace projectunity::scene;

    Scene scene;
    scene.setName("Scene Test");
    auto& parent = scene.createEntity("Parent");
    const auto parentId = parent.id;
    auto& child = scene.createEntity("Child", parentId);
    const auto childId = child.id;

    TransformComponent transform;
    transform.position = {1.0F, 2.0F, 3.0F};
    transform.rotationEuler = {10.0F, 20.0F, 30.0F};
    transform.scale = {2.0F, 2.0F, 2.0F};
    if (!scene.setTransform(childId, transform)) {
        return fail("failed to set transform");
    }
    MeshRendererComponent renderer {projectunity::core::StableId(77), 3U};
    renderer.editorInstanceIndex = 4U;
    renderer.renderable = false;
    renderer.runtimeCook.staticBatchable = false;
    renderer.runtimeCook.mutableRuntime = true;
    renderer.runtimeCook.physics = RuntimePhysicsMode::RigidBody;
    renderer.runtimeCook.grabbable = true;
    if (!scene.setMeshRenderer(childId, renderer)) {
        return fail("failed to set mesh renderer");
    }
    LightComponent light;
    light.type = LightComponentType::Spot;
    light.direction = {0.0F, -1.0F, 0.0F};
    light.color = {0.4F, 0.6F, 1.0F};
    light.intensity = 5.0F;
    light.range = 12.0F;
    light.innerConeAngle = 0.25F;
    light.outerConeAngle = 0.65F;
    if (!scene.setLight(childId, light)) {
        return fail("failed to set light");
    }
    CameraComponent camera;
    camera.projection = CameraComponentProjection::Perspective;
    camera.direction = {0.0F, 0.0F, 1.0F};
    camera.verticalFovRadians = 0.9F;
    camera.nearPlane = 0.1F;
    camera.farPlane = 500.0F;
    if (!scene.setCamera(childId, camera)) {
        return fail("failed to set camera");
    }
    ScriptComponent script;
    script.scriptName = "FlyPlayerController";
    script.scriptAsset = "Assets/Scripts/FlyPlayerController.cpp";
    setScriptFieldValue(script, "speed", 12.0F);
    setScriptFieldValue(script, "sprintSpeed", 48.0F);
    setScriptFieldValue(script, "mouseSensitivity", 0.25F);
    if (!scene.setScript(childId, script)) {
        return fail("failed to set script");
    }
    TerrainComponent terrain;
    terrain.settings.width = 32.0F;
    terrain.settings.length = 24.0F;
    terrain.settings.heightScale = 6.0F;
    terrain.settings.resolution = 17U;
    terrain.settings.chunkSize = 8U;
    terrain.settings.seed = 99U;
    terrain.settings.noiseType = projectunity::terrain::TerrainNoiseType::Ridged;
    terrain.settings.frequency = 0.025F;
    terrain.settings.octaves = 3U;
    terrain.settings.persistence = 0.45F;
    terrain.settings.lacunarity = 2.1F;
    terrain.settings.lodLevels = 2U;
    terrain.settings.generateCollider = true;
    terrain.generatedModelAssetId = projectunity::core::StableId(555);
    terrain.heightmap.assign(static_cast<std::size_t>(terrain.settings.resolution) * terrain.settings.resolution, 0.0F);
    terrain.heightmap[terrain.heightmap.size() / 2U] = 3.25F;
    terrain.materialLayers.clear();
    terrain.materialLayers.push_back({"Grass"});
    terrain.materialLayers.push_back({"Rock"});
    terrain.materialLayers.back().heightRange = {0.45F, 1.0F};
    terrain.materialLayers.back().slopeRange = {0.35F, 1.0F};
    if (!scene.setTerrain(childId, terrain)) {
        return fail("failed to set terrain");
    }
    RigidbodyComponent rigidbody;
    rigidbody.mass = 3.0F;
    rigidbody.linearDrag = 0.1F;
    rigidbody.angularDrag = 0.2F;
    rigidbody.useGravity = false;
    if (!scene.setRigidbody(childId, rigidbody)) {
        return fail("failed to set rigidbody");
    }
    ColliderComponent collider;
    collider.shape = ColliderShape::Terrain;
    collider.size = {32.0F, 6.0F, 24.0F};
    collider.radius = 2.0F;
    collider.trigger = true;
    if (!scene.setCollider(childId, collider)) {
        return fail("failed to set collider");
    }
    if (!scene.setCollider(childId, std::nullopt) || scene.findEntity(childId)->collider.has_value()) {
        return fail("failed to remove collider component");
    }
    if (!scene.setCollider(childId, collider)) {
        return fail("failed to restore collider component");
    }

    const auto* parentRead = scene.findEntity(parentId);
    if (parentRead == nullptr || parentRead->children.size() != 1 || parentRead->children.front() != childId) {
        return fail("parent/child hierarchy was not created");
    }

    if (scene.setParent(parentId, childId)) {
        return fail("cycle parent assignment was accepted");
    }

    auto serialized = scene.serialize();
    if (serialized.empty()) {
        return fail("scene serialization returned empty data");
    }

    Scene loaded;
    std::string error;
    if (!loaded.deserialize(serialized, &error)) {
        std::cerr << error << '\n';
        return fail("scene deserialization failed");
    }

    if (loaded.entityCount() != 2) {
        return fail("loaded scene entity count mismatch");
    }

    const auto roots = loaded.rootEntities();
    if (roots.size() != 1) {
        return fail("loaded scene root count mismatch");
    }

    const auto* loadedParent = loaded.findEntity(roots.front());
    if (loadedParent == nullptr || loadedParent->children.size() != 1) {
        return fail("loaded hierarchy was lost");
    }

    const auto* loadedChild = loaded.findEntity(loadedParent->children.front());
    if (loadedChild == nullptr || loadedChild->transform.position.x != 1.0F || loadedChild->transform.scale.x != 2.0F) {
        return fail("loaded transform mismatch");
    }
    if (!loadedChild->meshRenderer.has_value()
        || loadedChild->meshRenderer->modelAssetId.value() != 77
        || loadedChild->meshRenderer->primitiveInstanceIndex.value_or(0U) != 3U
        || loadedChild->meshRenderer->editorInstanceIndex.value_or(0U) != 4U
        || loadedChild->meshRenderer->renderable
        || loadedChild->meshRenderer->runtimeCook.staticBatchable
        || !loadedChild->meshRenderer->runtimeCook.mutableRuntime
        || loadedChild->meshRenderer->runtimeCook.physics != RuntimePhysicsMode::RigidBody
        || !loadedChild->meshRenderer->runtimeCook.grabbable) {
        return fail("loaded mesh renderer mismatch");
    }
    if (!loadedChild->light.has_value()
        || loadedChild->light->type != LightComponentType::Spot
        || loadedChild->light->color[2] != 1.0F
        || loadedChild->light->range != 12.0F) {
        return fail("loaded light component mismatch");
    }
    if (!loadedChild->camera.has_value()
        || loadedChild->camera->projection != CameraComponentProjection::Perspective
        || loadedChild->camera->direction.z != 1.0F
        || loadedChild->camera->farPlane != 500.0F) {
        return fail("loaded camera component mismatch");
    }
    if (loadedChild->scripts.empty()
        || loadedChild->scripts[0].scriptName != "FlyPlayerController"
        || loadedChild->scripts[0].scriptAsset != "Assets/Scripts/FlyPlayerController.cpp"
        || !loadedChild->scripts[0].enabled
        || scriptFieldValue(loadedChild->scripts[0], "speed", 0.0F) != 12.0F
        || scriptFieldValue(loadedChild->scripts[0], "sprintSpeed", 0.0F) != 48.0F
        || scriptFieldValue(loadedChild->scripts[0], "mouseSensitivity", 0.0F) != 0.25F) {
        return fail("loaded script component mismatch");
    }
    if (!loadedChild->terrain.has_value()) {
        return fail("loaded terrain component was missing");
    }
    if (loadedChild->terrain->settings.width != 32.0F
        || loadedChild->terrain->settings.noiseType != projectunity::terrain::TerrainNoiseType::Ridged) {
        return fail("loaded terrain settings mismatch");
    }
    if (loadedChild->terrain->generatedModelAssetId.value() != 555U) {
        return fail("loaded terrain generated asset id mismatch");
    }
    if (loadedChild->terrain->heightmap.size() != terrain.heightmap.size()
        || loadedChild->terrain->heightmap[terrain.heightmap.size() / 2U] != 3.25F) {
        return fail("loaded terrain heightmap mismatch");
    }
    if (loadedChild->terrain->materialLayers.size() != 2U) {
        return fail("loaded terrain material layer count mismatch");
    }
    if (loadedChild->terrain->materialLayers.back().name != "Rock"
        || !closeEnough(loadedChild->terrain->materialLayers.back().heightRange[0], 0.45F)) {
        return fail("loaded terrain material layer data mismatch");
    }
    if (!loadedChild->rigidbody.has_value()
        || loadedChild->rigidbody->mass != 3.0F
        || loadedChild->rigidbody->useGravity) {
        return fail("loaded rigidbody component mismatch");
    }
    if (!loadedChild->collider.has_value()
        || loadedChild->collider->shape != ColliderShape::Terrain
        || loadedChild->collider->size.x != 32.0F
        || !loadedChild->collider->trigger) {
        return fail("loaded collider component mismatch");
    }

    const auto path = std::filesystem::temp_directory_path() / "projectunity_scene_test.scene.json";
    if (!loaded.saveToFile(path, &error)) {
        std::cerr << error << '\n';
        return fail("scene saveToFile failed");
    }

    Scene fromFile;
    if (!fromFile.loadFromFile(path, &error)) {
        std::cerr << error << '\n';
        return fail("scene loadFromFile failed");
    }

    std::filesystem::remove(path);

    if (fromFile.entityCount() != 2 || fromFile.rootEntities().size() != 1) {
        return fail("file roundtrip lost hierarchy");
    }

    const auto examplePath = std::filesystem::path(PROJECTUNITY_SOURCE_DIR)
        / "examples" / "basic_scene" / "BasicScene.scene.json";
    Scene exampleScene;
    if (!exampleScene.loadFromFile(examplePath, &error)) {
        std::cerr << error << '\n';
        return fail("basic scene example failed to load");
    }

    if (exampleScene.entityCount() != 2 || exampleScene.rootEntities().size() != 1) {
        return fail("basic scene example hierarchy is invalid");
    }

    return EXIT_SUCCESS;
}
