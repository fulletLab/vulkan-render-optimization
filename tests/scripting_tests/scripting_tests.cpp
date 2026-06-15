#include <projectunity/scripting/ScriptRuntime.hpp>

#include <cstdlib>
#include <iostream>
#include <memory>

namespace {

int fail(const char* message)
{
    std::cerr << message << '\n';
    return EXIT_FAILURE;
}

struct LifecycleCounts {
    int attach {0};
    int create {0};
    int enable {0};
    int start {0};
    int update {0};
    int fixedUpdate {0};
    int disable {0};
    int destroy {0};
    int detach {0};
    bool componentApiAvailable {false};
};

class ProbeScript final : public projectunity::scripting::ScriptInstance {
public:
    explicit ProbeScript(LifecycleCounts& counts)
        : counts_(counts)
    {
    }

    void onAttach(projectunity::scripting::ScriptContext&) override { ++counts_.attach; }
    void onCreate(projectunity::scripting::ScriptContext& context) override
    {
        ++counts_.create;
        counts_.componentApiAvailable = context.getComponent<projectunity::scene::TransformComponent>() != nullptr
            && context.getComponent<projectunity::scene::CameraComponent>() == nullptr;
    }
    void onEnable(projectunity::scripting::ScriptContext&) override { ++counts_.enable; }
    void onStart(projectunity::scripting::ScriptContext&) override { ++counts_.start; }
    void onUpdate(projectunity::scripting::ScriptContext&, float) override { ++counts_.update; }
    void onFixedUpdate(projectunity::scripting::ScriptContext&, float) override { ++counts_.fixedUpdate; }
    void onDisable(projectunity::scripting::ScriptContext&) override { ++counts_.disable; }
    void onDestroy(projectunity::scripting::ScriptContext&) override { ++counts_.destroy; }
    void onDetach(projectunity::scripting::ScriptContext&) override { ++counts_.detach; }
private:
    LifecycleCounts& counts_;
};

} // namespace

int main()
{
    using namespace projectunity;

    scripting::ScriptRegistry registry;
    scripting::registerBuiltInScripts(registry);
    std::string resolutionError;
    const auto health = registry.createComponentFromAsset("Assets/Scripts/Health.cpp", &resolutionError);
    if (!health.has_value()
        || health->scriptName != "Health"
        || health->scriptAsset != "Assets/Scripts/Health.cpp"
        || scene::scriptFieldValue(*health, "maxHealth", 0.0F) != 100.0F
        || scene::scriptFieldValue(*health, "regenerationPerSecond", -1.0F) != 0.0F
        || scene::findScriptField(*health, "speed") != nullptr
        || !resolutionError.empty()) {
        return fail("health metadata did not produce distinct script fields");
    }
    const auto missing = registry.createComponentFromAsset("Assets/Scripts/NotCompiled.cpp", &resolutionError);
    if (missing.has_value() || resolutionError != "Script asset found but class is not registered.") {
        return fail("unregistered script asset did not return the expected error");
    }

    auto replaceable = registry.createComponentFromAsset("Assets/Scripts/FlyPlayerController.cpp", &resolutionError);
    if (!replaceable.has_value()) {
        return fail("failed to create replaceable script component");
    }
    replaceable->enabled = false;
    scene::setScriptFieldValue(*replaceable, "speed", 42.0F);
    if (!registry.replaceComponentAsset(*replaceable, "Assets/Scripts/Health.cpp", &resolutionError)
        || replaceable->scriptName != "Health"
        || replaceable->enabled
        || scene::findScriptField(*replaceable, "speed") != nullptr
        || scene::scriptFieldValue(*replaceable, "maxHealth", 0.0F) != 100.0F) {
        return fail("changing script asset did not replace metadata cleanly");
    }

    LifecycleCounts counts;
    scripting::ScriptDescriptor probe;
    probe.className = "LifecycleProbe";
    probe.assetPath = "Assets/Scripts/LifecycleProbe.cpp";
    probe.create = [&counts] { return std::make_unique<ProbeScript>(counts); };
    if (!registry.registerScript(std::move(probe))) {
        return fail("failed to register lifecycle probe");
    }

    scene::Scene emptyRuntimeScene;
    auto& emptyEntity = emptyRuntimeScene.createEntity("Empty");
    const auto emptyId = emptyEntity.id;
    scripting::ScriptRuntime emptyRuntime;
    emptyRuntime.setRegistry(&registry);
    if (!emptyRuntime.start(emptyRuntimeScene)) {
        return fail("empty runtime failed to start");
    }
    emptyRuntime.update(0.5F, {});
    if (emptyRuntimeScene.findEntity(emptyId)->transform.position.z != 0.0F
        || emptyRuntime.stats().scriptComponentsFound != 0
        || emptyRuntime.stats().scriptsUpdated != 0) {
        return fail("scene without scripts should not execute gameplay");
    }
    emptyRuntime.stop();

    if (scripting::findRuntimeCameraEntity(emptyRuntimeScene).isValid()) {
        return fail("runtime camera found in scene without CameraComponent");
    }

    scene::Scene noCameraScene;
    auto& noCameraEntity = noCameraScene.createEntity("Fly Without Camera");
    const auto noCameraId = noCameraEntity.id;
    auto noCameraFly = registry.createComponentFromAsset("Assets/Scripts/FlyPlayerController.cpp", &resolutionError);
    if (!noCameraFly.has_value() || !noCameraScene.addScript(noCameraId, *noCameraFly).has_value()) {
        return fail("failed to add fly script without camera");
    }
    scripting::ScriptRuntime noCameraRuntime;
    noCameraRuntime.setRegistry(&registry);
    (void)noCameraRuntime.start(noCameraScene);
    scripting::InputState noCameraInput;
    noCameraInput.setKeyDown(scripting::KeyCode::W, true);
    noCameraRuntime.update(0.5F, noCameraInput);
    if (noCameraScene.findEntity(noCameraId)->transform.position.z != 0.0F
        || noCameraRuntime.stats().transformsChanged != 0) {
        return fail("FlyPlayerController moved without CameraComponent");
    }
    noCameraRuntime.stop();

    scene::Scene editorScene;
    auto& player = editorScene.createEntity("Player");
    const auto playerId = player.id;
    if (!editorScene.setCamera(playerId, scene::CameraComponent {})) {
        return fail("failed to attach camera");
    }
    auto fly = registry.createComponentFromAsset("Assets/Scripts/FlyPlayerController.cpp", &resolutionError);
    if (!fly.has_value()) {
        return fail("failed to create fly script component");
    }
    scene::setScriptFieldValue(*fly, "speed", 10.0F);
    const auto flyScriptId = editorScene.addScript(playerId, *fly);
    if (!flyScriptId.has_value()) {
        return fail("failed to attach fly script");
    }
    if (!editorScene.addScript(playerId, *health).has_value()) {
        return fail("failed to attach second script to player");
    }

    auto& probeEntity = editorScene.createEntity("Probe");
    const auto probeId = probeEntity.id;
    scene::ScriptComponent probeComponent;
    probeComponent.scriptName = "LifecycleProbe";
    probeComponent.scriptAsset = "Assets/Scripts/LifecycleProbe.cpp";
    const auto probeScriptId = editorScene.addScript(probeId, probeComponent);
    if (!probeScriptId.has_value()) {
        return fail("failed to attach lifecycle probe");
    }

    scene::Scene runtimeScene;
    std::string error;
    if (!runtimeScene.deserialize(editorScene.serialize(&error), &error)) {
        std::cerr << error << '\n';
        return fail("failed to create runtime scene snapshot");
    }

    scripting::ScriptRuntime runtime;
    runtime.setRegistry(&registry);
    if (!runtime.start(runtimeScene)) {
        return fail("runtime failed to start");
    }
    if (scripting::findRuntimeCameraEntity(runtimeScene) != playerId) {
        return fail("runtime camera lookup did not return Player CameraComponent");
    }
    if (counts.attach != 1 || counts.create != 1 || counts.enable != 1 || counts.start != 1
        || !counts.componentApiAvailable) {
        return fail("lifecycle startup callbacks mismatch");
    }

    scripting::InputState input;
    input.setKeyDown(scripting::KeyCode::W, true);
    runtime.update(0.5F, input);
    runtime.fixedUpdate(1.0F / 60.0F, input);

    const auto* editorPlayer = editorScene.findEntity(playerId);
    auto* runtimePlayer = runtimeScene.findEntity(playerId);
    if (editorPlayer == nullptr || runtimePlayer == nullptr) {
        return fail("player missing after runtime update");
    }
    if (editorPlayer->transform.position.z != 0.0F || runtimePlayer->transform.position.z != 5.0F
        || runtime.stats().transformsChanged != 1) {
        return fail("runtime script did not stay isolated from editor scene");
    }
    if (counts.update != 1 || counts.fixedUpdate != 1 || runtime.stats().scriptComponentsFound != 3) {
        return fail("runtime update callbacks mismatch");
    }

    auto* runtimeFly = scene::findScript(*runtimePlayer, runtimePlayer->scripts.front().instanceId);
    if (runtimeFly == nullptr || runtimeFly->scriptName != "FlyPlayerController") {
        return fail("runtime fly script missing");
    }
    scene::setScriptFieldValue(*runtimeFly, "speed", 2.0F);
    runtime.update(0.5F, input);
    if (runtimePlayer->transform.position.z != 6.0F || runtime.stats().transformsChanged != 2) {
        return fail("runtime field edit did not affect movement");
    }

    runtimeFly->enabled = false;
    runtime.update(0.5F, input);
    if (runtimePlayer->transform.position.z != 6.0F) {
        return fail("disabled script continued executing");
    }

    auto* runtimeProbeEntity = runtimeScene.findEntity(probeId);
    if (runtimeProbeEntity == nullptr || runtimeProbeEntity->scripts.empty()
        || !runtimeScene.removeScript(probeId, runtimeProbeEntity->scripts.front().instanceId)) {
        return fail("failed to remove lifecycle probe");
    }
    runtime.update(0.016F, {});
    if (counts.disable != 1 || counts.destroy != 1 || counts.detach != 1) {
        return fail("script removal lifecycle callbacks mismatch");
    }

    runtime.stop();
    if (runtime.running() || runtime.stats().scriptsUpdated < 3 || runtime.stats().scriptErrors != 0) {
        return fail("runtime final stats mismatch");
    }
    return EXIT_SUCCESS;
}
