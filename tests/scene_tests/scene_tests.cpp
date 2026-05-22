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
    if (!scene.setMeshRenderer(childId, MeshRendererComponent {projectunity::core::StableId(77)})) {
        return fail("failed to set mesh renderer");
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
    if (!loadedChild->meshRenderer.has_value() || loadedChild->meshRenderer->modelAssetId.value() != 77) {
        return fail("loaded mesh renderer mismatch");
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
