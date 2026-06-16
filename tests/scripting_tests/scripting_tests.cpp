#include <projectunity/scripting/ScriptRuntime.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

namespace {

int fail(const char* message)
{
    std::cerr << message << '\n';
    return EXIT_FAILURE;
}

[[nodiscard]] std::string environmentValue(const char* name)
{
#if defined(_MSC_VER)
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
        return {};
    }
    std::string result(value);
    std::free(value);
    return result;
#else
    const auto* value = std::getenv(name);
    return value == nullptr ? std::string {} : std::string(value);
#endif
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

class CaptureService final : public projectunity::scripting::InputService {
public:
    void setMouseCaptured(bool captured) override
    {
        mouseCaptured = captured;
    }

    [[nodiscard]] bool isMouseCaptured() const noexcept override
    {
        return mouseCaptured;
    }

    bool mouseCaptured {false};
};

} // namespace

int main()
{
    using namespace projectunity;

    const auto modulePath = environmentValue("PROJECTUNITY_TEST_SCRIPTS_DLL");
    if (modulePath.empty()) {
        return fail("PROJECTUNITY_TEST_SCRIPTS_DLL is not set");
    }

    scripting::ScriptModuleLoader moduleLoader;
    scripting::ScriptRegistry registry;
    std::string resolutionError;
    if (!moduleLoader.load(std::filesystem::path(modulePath), registry, &resolutionError)) {
        std::cerr << resolutionError << '\n';
        return fail("failed to load ProjectUnityGameScripts module");
    }

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
    if (missing.has_value()
        || resolutionError != "Script asset exists but native class is not loaded. Build/Reload Project Scripts.") {
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

    scripting::InputState edgeInput;
    edgeInput.setKeyDown(scripting::KeyCode::Tab, true);
    if (!edgeInput.keyDown(scripting::KeyCode::Tab) || !edgeInput.keyPressed(scripting::KeyCode::Tab)) {
        return fail("input did not report a key press edge");
    }
    edgeInput.clearFrameDeltas();
    if (!edgeInput.keyDown(scripting::KeyCode::Tab) || edgeInput.keyPressed(scripting::KeyCode::Tab)) {
        return fail("input did not clear key press edges per frame");
    }
    edgeInput.setKeyDown(scripting::KeyCode::Tab, true);
    if (edgeInput.keyPressed(scripting::KeyCode::Tab)) {
        return fail("held key generated a repeated key press edge");
    }
    edgeInput.setKeyDown(scripting::KeyCode::Tab, false);
    edgeInput.setKeyDown(scripting::KeyCode::Tab, true);
    if (!edgeInput.keyPressed(scripting::KeyCode::Tab)) {
        return fail("released key did not generate a new key press edge");
    }

    scene::Scene emptyRuntimeScene;
    auto& emptyEntity = emptyRuntimeScene.createEntity("Empty");
    const auto emptyId = emptyEntity.id;
    CaptureService emptyCaptureService;
    scripting::ScriptRuntime emptyRuntime;
    emptyRuntime.setRegistry(&registry);
    emptyRuntime.setInputService(&emptyCaptureService);
    if (!emptyRuntime.start(emptyRuntimeScene)) {
        return fail("empty runtime failed to start");
    }
    scripting::InputState emptyTabInput;
    emptyTabInput.setKeyDown(scripting::KeyCode::Tab, true);
    emptyRuntime.update(0.016F, emptyTabInput);
    if (emptyCaptureService.mouseCaptured) {
        return fail("scene without scripts captured mouse");
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

    scene::Scene disabledCaptureScene;
    auto& disabledCapturePlayer = disabledCaptureScene.createEntity("Disabled Capture Player");
    const auto disabledCapturePlayerId = disabledCapturePlayer.id;
    if (!disabledCaptureScene.setCamera(disabledCapturePlayerId, scene::CameraComponent {})) {
        return fail("failed to attach disabled capture camera");
    }
    auto disabledCaptureFly = registry.createComponentFromAsset("Assets/Scripts/FlyPlayerController.cpp", &resolutionError);
    if (!disabledCaptureFly.has_value()) {
        return fail("failed to create disabled capture fly script");
    }
    disabledCaptureFly->enabled = false;
    if (!disabledCaptureScene.addScript(disabledCapturePlayerId, *disabledCaptureFly).has_value()) {
        return fail("failed to add disabled capture fly script");
    }
    CaptureService disabledCaptureService;
    scripting::ScriptRuntime disabledCaptureRuntime;
    disabledCaptureRuntime.setRegistry(&registry);
    disabledCaptureRuntime.setInputService(&disabledCaptureService);
    (void)disabledCaptureRuntime.start(disabledCaptureScene);
    scripting::InputState disabledTabInput;
    disabledTabInput.setKeyDown(scripting::KeyCode::Tab, true);
    disabledCaptureRuntime.update(0.016F, disabledTabInput);
    if (disabledCaptureService.mouseCaptured) {
        return fail("disabled FlyPlayerController captured mouse");
    }
    disabledCaptureRuntime.stop();

    scene::Scene captureScene;
    auto& capturePlayer = captureScene.createEntity("Capture Player");
    const auto capturePlayerId = capturePlayer.id;
    if (!captureScene.setCamera(capturePlayerId, scene::CameraComponent {})) {
        return fail("failed to attach capture camera");
    }
    auto captureFly = registry.createComponentFromAsset("Assets/Scripts/FlyPlayerController.cpp", &resolutionError);
    if (!captureFly.has_value() || !captureScene.addScript(capturePlayerId, *captureFly).has_value()) {
        return fail("failed to add capture fly script");
    }
    CaptureService captureService;
    scripting::ScriptRuntime captureRuntime;
    captureRuntime.setRegistry(&registry);
    captureRuntime.setInputService(&captureService);
    (void)captureRuntime.start(captureScene);
    auto* captureRuntimePlayer = captureScene.findEntity(capturePlayerId);
    if (captureRuntimePlayer == nullptr || !captureRuntimePlayer->camera.has_value()) {
        return fail("capture player missing camera");
    }
    const auto captureInitialDirection = captureRuntimePlayer->camera->direction;
    scripting::InputState ignoredMouseDelta;
    ignoredMouseDelta.mouseDeltaX = 45.0F;
    captureRuntime.update(0.016F, ignoredMouseDelta);
    if (!math::nearlyEqual(captureRuntimePlayer->camera->direction, captureInitialDirection)) {
        return fail("FlyPlayerController rotated while mouse was not captured");
    }
    scripting::InputState captureTabInput;
    captureTabInput.setKeyDown(scripting::KeyCode::Tab, true);
    captureRuntime.update(0.016F, captureTabInput);
    if (!captureService.mouseCaptured) {
        return fail("FlyPlayerController did not request mouse capture on Tab");
    }
    captureTabInput.clearFrameDeltas();
    captureRuntime.update(0.016F, captureTabInput);
    if (!captureService.mouseCaptured) {
        return fail("held Tab toggled mouse capture repeatedly");
    }
    const auto capturedDirectionBeforeMouse = captureRuntimePlayer->camera->direction;
    scripting::InputState capturedMouseDelta;
    capturedMouseDelta.mouseDeltaX = 45.0F;
    captureRuntime.update(0.016F, capturedMouseDelta);
    if (math::nearlyEqual(captureRuntimePlayer->camera->direction, capturedDirectionBeforeMouse)) {
        return fail("FlyPlayerController did not rotate while mouse was captured");
    }
    scripting::InputState escapeInput;
    escapeInput.setKeyDown(scripting::KeyCode::Escape, true);
    captureRuntime.update(0.016F, escapeInput);
    if (captureService.mouseCaptured) {
        return fail("FlyPlayerController did not release mouse capture on Escape");
    }
    const auto releasedDirectionBeforeMouse = captureRuntimePlayer->camera->direction;
    scripting::InputState releasedMouseDelta;
    releasedMouseDelta.mouseDeltaX = 45.0F;
    captureRuntime.update(0.016F, releasedMouseDelta);
    if (!math::nearlyEqual(captureRuntimePlayer->camera->direction, releasedDirectionBeforeMouse)) {
        return fail("FlyPlayerController rotated after mouse capture was released");
    }
    captureRuntime.stop();

    scene::Scene childCameraScene;
    auto& childCameraPlayer = childCameraScene.createEntity("Child Camera Player");
    const auto childCameraPlayerId = childCameraPlayer.id;
    auto& childCamera = childCameraScene.createEntity("Camera", childCameraPlayerId);
    const auto childCameraId = childCamera.id;
    if (!childCameraScene.setCamera(childCameraId, scene::CameraComponent {})) {
        return fail("failed to attach child camera");
    }
    auto childCameraFly = registry.createComponentFromAsset("Assets/Scripts/FlyPlayerController.cpp", &resolutionError);
    if (!childCameraFly.has_value() || !childCameraScene.addScript(childCameraPlayerId, *childCameraFly).has_value()) {
        return fail("failed to add fly script with child camera");
    }
    scripting::ScriptRuntime childCameraRuntime;
    childCameraRuntime.setRegistry(&registry);
    (void)childCameraRuntime.start(childCameraScene);
    if (scripting::findRuntimeCameraEntity(childCameraScene) != childCameraId) {
        return fail("runtime camera lookup did not return child CameraComponent");
    }
    scripting::InputState childCameraJump;
    childCameraJump.setKeyDown(scripting::KeyCode::Space, true);
    childCameraRuntime.update(0.1F, childCameraJump);
    if (childCameraScene.findEntity(childCameraPlayerId)->transform.position.y <= 0.0F
        || childCameraRuntime.stats().transformsChanged != 1) {
        return fail("FlyPlayerController did not jump with a child CameraComponent");
    }
    childCameraRuntime.stop();

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

    scripting::InputState jumpInput;
    jumpInput.setKeyDown(scripting::KeyCode::Space, true);
    const auto beforeJumpY = runtimePlayer->transform.position.y;
    runtime.update(0.1F, jumpInput);
    if (runtimePlayer->transform.position.y <= beforeJumpY) {
        return fail("FlyPlayerController space jump did not move through scripting");
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
