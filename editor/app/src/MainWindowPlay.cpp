#include <projectunity/editor/MainWindow.hpp>

#include <projectunity/editor/ViewportWidget.hpp>

#include <QStatusBar>
#include <QString>

#include <filesystem>
#include <fstream>
#include <optional>

namespace projectunity::editor {

scene::EntityId MainWindow::createPlayerEntity()
{
    ensureFlyPlayerScriptAsset();
    auto& player = scene_.createEntity("Player");
    scene::TransformComponent transform;
    transform.position = {0.0F, 3.0F, -10.0F};
    (void)scene_.setTransform(player.id, transform);
    scene::CameraComponent camera;
    camera.direction = {0.0F, 0.0F, 1.0F};
    camera.right = {1.0F, 0.0F, 0.0F};
    camera.up = {0.0F, 1.0F, 0.0F};
    camera.verticalFovRadians = 1.04719755F;
    camera.nearPlane = 0.05F;
    camera.farPlane = 4000.0F;
    (void)scene_.setCamera(player.id, camera);
    const auto id = player.id;
    rebuildHierarchy();
    selectEntity(id);
    statusBar()->showMessage(QStringLiteral("Player created"));
    core::logInfo(core::LogCategory::Editor, "Player GameObject created");
    return id;
}

void MainWindow::ensureFlyPlayerScriptAsset()
{
    const auto scriptsDir = std::filesystem::path(PROJECTUNITY_SOURCE_DIR) / "Project" / "Assets" / "Scripts";
    std::error_code errorCode;
    std::filesystem::create_directories(scriptsDir, errorCode);
    const auto scriptPath = scriptsDir / "FlyPlayerController.cpp";
    if (std::filesystem::exists(scriptPath)) {
        return;
    }
    std::ofstream script(scriptPath, std::ios::binary | std::ios::trunc);
    script
        << "// ProjectUnity C++ gameplay script template.\n"
        << "// Runtime binding: attach ScriptComponent name \"FlyPlayerController\" to an entity with a Camera.\n"
        << "// Controls in Game View: right mouse look, WASD move, Q/E down/up, Shift fast.\n\n"
        << "struct FlyPlayerController {\n"
        << "    float moveSpeed = 7.5f;\n"
        << "    float fastMultiplier = 3.0f;\n"
        << "    float lookSensitivity = 0.0035f;\n"
        << "};\n";
}

void MainWindow::startPlayMode()
{
    ensureFlyPlayerScriptAsset();
    const auto playerId = findPlayableCameraEntity();
    if (!playerId.isValid()) {
        statusBar()->showMessage(QStringLiteral("Play needs a Player or Camera"));
        core::logWarning(core::LogCategory::Editor, "Play ignored: no scene camera/player exists");
        return;
    }
    playModeActive_ = true;
    if (gameViewport_ != nullptr) {
        gameViewport_->setGameCameraEntity(playerId);
        gameViewport_->setGameInputEnabled(true);
        gameViewport_->setGameRuntimeSnapshotEnabled(true);
        gameViewport_->setFocus(Qt::OtherFocusReason);
    }
    statusBar()->showMessage(QStringLiteral("Play runtime active"));
    core::logInfo(core::LogCategory::Editor, "Play mode started with runtime scene snapshot");
}

void MainWindow::stopPlayMode()
{
    playModeActive_ = false;
    if (gameViewport_ != nullptr) {
        gameViewport_->setGameInputEnabled(false);
        gameViewport_->setGameRuntimeSnapshotEnabled(false);
        gameViewport_->setGameCameraEntity({});
    }
    statusBar()->showMessage(QStringLiteral("Play stopped"));
}

scene::EntityId MainWindow::findPlayableCameraEntity() const
{
    for (const auto& entity : scene_.entities()) {
        if (entity.camera.has_value()
            && entity.script.has_value()
            && entity.script->enabled
            && entity.script->scriptName == "FlyPlayerController") {
            return entity.id;
        }
    }
    for (const auto& entity : scene_.entities()) {
        if (entity.camera.has_value()) {
            return entity.id;
        }
    }
    return {};
}

void MainWindow::attachFlyPlayerControllerToSelection()
{
    auto* entity = selectedEntityId_.isValid() ? scene_.findEntity(selectedEntityId_) : nullptr;
    if (entity == nullptr) {
        return;
    }
    ensureFlyPlayerScriptAsset();
    scene::ScriptComponent script;
    script.scriptName = "FlyPlayerController";
    (void)scene_.setScript(selectedEntityId_, script);
    updateInspector();
    refreshViewports();
    statusBar()->showMessage(QStringLiteral("FlyPlayerController attached"));
}

void MainWindow::removeScriptFromSelection()
{
    auto* entity = selectedEntityId_.isValid() ? scene_.findEntity(selectedEntityId_) : nullptr;
    if (entity == nullptr) {
        return;
    }
    (void)scene_.setScript(selectedEntityId_, std::nullopt);
    updateInspector();
    refreshViewports();
    statusBar()->showMessage(QStringLiteral("Script removed"));
}

} // namespace projectunity::editor
