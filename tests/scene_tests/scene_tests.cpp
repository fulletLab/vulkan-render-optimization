#include <projectunity/scene/Scene.hpp>

#include <cstdlib>
#include <filesystem>
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
    if (!scene.setScript(childId, script)) {
        return fail("failed to set script");
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
        || loadedChild->meshRenderer->renderable) {
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
    if (!loadedChild->script.has_value()
        || loadedChild->script->scriptName != "FlyPlayerController"
        || !loadedChild->script->enabled) {
        return fail("loaded script component mismatch");
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
