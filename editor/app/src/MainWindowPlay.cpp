#include <projectunity/editor/MainWindow.hpp>

#include <projectunity/editor/ViewportWidget.hpp>

#include <QApplication>
#include <QMessageBox>
#include <QObject>
#include <QProgressDialog>
#include <QStatusBar>
#include <QString>

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <unordered_map>

namespace projectunity::editor {
namespace {

struct PlayRuntimeSnapshotStats {
    std::uint64_t editorEntities {0};
    std::uint64_t runtimeEntities {0};
    std::uint64_t editableProxiesSkipped {0};
    std::uint64_t runtimeProxyOverrides {0};
    std::uint64_t runtimeAssetInstances {0};
    std::uint64_t runtimePrimitiveInstances {0};
    std::uint64_t runtimeRenderChunks {0};
    std::uint64_t runtimeCameras {0};
    std::uint64_t runtimeLights {0};
    std::uint64_t runtimeScripts {0};
    bool injectedRuntimeFlyPlayer {false};
};

[[nodiscard]] bool isEditablePrimitiveProxy(const scene::Entity& entity) noexcept
{
    return entity.meshRenderer.has_value() && !entity.meshRenderer->renderable;
}

[[nodiscard]] bool nearVec3(math::Vec3 lhs, math::Vec3 rhs, float epsilon = 0.0005F) noexcept
{
    return std::fabs(lhs.x - rhs.x) <= epsilon
        && std::fabs(lhs.y - rhs.y) <= epsilon
        && std::fabs(lhs.z - rhs.z) <= epsilon;
}

[[nodiscard]] bool defaultEditableProxyTransform(
    const scene::TransformComponent& transform,
    math::Vec3 expectedPosition) noexcept
{
    return nearVec3(transform.position, expectedPosition)
        && nearVec3(transform.rotationEuler, {0.0F, 0.0F, 0.0F})
        && nearVec3(transform.scale, {1.0F, 1.0F, 1.0F});
}

[[nodiscard]] std::optional<std::uint32_t> primitiveInstanceIndexForSnapshotProxy(
    const assets::ModelAsset& model,
    const scene::MeshRendererComponent& renderer) noexcept
{
    if (renderer.primitiveInstanceIndex.has_value()
        && *renderer.primitiveInstanceIndex < model.primitiveInstances.size()) {
        return *renderer.primitiveInstanceIndex;
    }
    if (!renderer.editorInstanceIndex.has_value()
        || *renderer.editorInstanceIndex >= model.editorInstances.size()) {
        return std::nullopt;
    }

    const auto editorIndex = *renderer.editorInstanceIndex;
    const auto& editorInstance = model.editorInstances[editorIndex];
    if (model.editorInstances.size() == model.primitiveInstances.size()
        && editorIndex < model.primitiveInstances.size()) {
        return editorIndex;
    }
    for (std::uint32_t index = 0; index < model.primitiveInstances.size(); ++index) {
        const auto& instance = model.primitiveInstances[index];
        if (instance.primitiveIndex == editorInstance.sourcePrimitiveIndex
            && nearVec3(instance.bounds.center, editorInstance.bounds.center, 0.001F)) {
            return index;
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool editableProxyHasRuntimeOverride(
    const scene::Entity& entity,
    const assets::IAssetManager& assetManager)
{
    if (!isEditablePrimitiveProxy(entity)) {
        return false;
    }
    const auto model = assetManager.model(entity.meshRenderer->modelAssetId);
    if (model == nullptr) {
        return false;
    }
    const auto primitiveInstanceIndex = primitiveInstanceIndexForSnapshotProxy(*model, *entity.meshRenderer);
    if (!primitiveInstanceIndex.has_value()) {
        return false;
    }
    const auto& instance = model->primitiveInstances[*primitiveInstanceIndex];
    return !defaultEditableProxyTransform(entity.transform, instance.bounds.center);
}

[[nodiscard]] std::string flyPlayerScriptTemplate()
{
    return
        "// ProjectUnity gameplay script asset.\n"
        "// This script is bound by name through Script: FlyPlayerController.\n"
        "// Play mode runs it on a runtime scene snapshot, not on editor proxy entities.\n"
        "// Current runtime: the engine executes the Script component values natively.\n"
        "// This C++ file is the project asset placeholder until native script hot-reload lands.\n\n"
        "struct FlyPlayerController {\n"
        "    float moveSpeed = 7.5f;\n"
        "    float fastMultiplier = 3.0f;\n"
        "    float lookSensitivity = 0.0035f;\n\n"
        "    // Runtime API draft:\n"
        "    // - Right mouse: look\n"
        "    // - W/S: forward/back\n"
        "    // - A/D: strafe\n"
        "    // - Q/E: down/up\n"
        "    // - Shift: fast move\n"
        "    void onUpdate(auto& ctx) {\n"
        "        const float speed = moveSpeed * (ctx.keyDown(\"Shift\") ? fastMultiplier : 1.0f);\n"
        "        ctx.lookWithMouse(lookSensitivity);\n"
        "        ctx.moveLocal({\n"
        "            (ctx.keyDown(\"D\") ? 1.0f : 0.0f) - (ctx.keyDown(\"A\") ? 1.0f : 0.0f),\n"
        "            (ctx.keyDown(\"E\") ? 1.0f : 0.0f) - (ctx.keyDown(\"Q\") ? 1.0f : 0.0f),\n"
        "            (ctx.keyDown(\"W\") ? 1.0f : 0.0f) - (ctx.keyDown(\"S\") ? 1.0f : 0.0f),\n"
        "        }, speed);\n"
        "    }\n"
        "};\n";
}

} // namespace

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
    scene::ScriptComponent script;
    script.scriptName = "FlyPlayerController";
    (void)scene_.setScript(player.id, script);
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
    script << flyPlayerScriptTemplate();
}

bool MainWindow::buildPlayRuntimeSnapshot(scene::EntityId sourceCameraEntityId)
{
    playRuntimeScene_.clear();
    playRuntimeScene_.setName(std::string(scene_.name()) + " Runtime");
    playRuntimeCameraEntityId_ = {};

    PlayRuntimeSnapshotStats stats;
    std::unordered_map<std::uint64_t, scene::EntityId> runtimeIdBySource;
    runtimeIdBySource.reserve(scene_.entityCount());

    for (const auto& source : scene_.entities()) {
        ++stats.editorEntities;
        if (isEditablePrimitiveProxy(source)) {
            if (!editableProxyHasRuntimeOverride(source, assetManager_)) {
                ++stats.editableProxiesSkipped;
                continue;
            }
            ++stats.runtimeProxyOverrides;
        }

        auto& runtime = playRuntimeScene_.createEntity(source.name);
        runtimeIdBySource[source.id.value()] = runtime.id;
        ++stats.runtimeEntities;

        (void)playRuntimeScene_.setTransform(runtime.id, source.transform);
        if (source.meshRenderer.has_value()) {
            (void)playRuntimeScene_.setMeshRenderer(runtime.id, source.meshRenderer);
            if (source.meshRenderer->renderable) {
                ++stats.runtimeAssetInstances;
                if (const auto model = assetManager_.model(source.meshRenderer->modelAssetId)) {
                    stats.runtimePrimitiveInstances += model->primitiveInstances.empty()
                        ? model->primitives.size()
                        : model->primitiveInstances.size();
                    stats.runtimeRenderChunks += model->primitiveClusters.empty()
                        ? std::max<std::size_t>(model->primitiveInstances.size(), model->primitives.size())
                        : model->primitiveClusters.size();
                }
            }
        }
        if (source.light.has_value()) {
            (void)playRuntimeScene_.setLight(runtime.id, source.light);
            ++stats.runtimeLights;
        }
        if (source.camera.has_value()) {
            (void)playRuntimeScene_.setCamera(runtime.id, source.camera);
            ++stats.runtimeCameras;
        }
        if (source.script.has_value()) {
            (void)playRuntimeScene_.setScript(runtime.id, source.script);
            ++stats.runtimeScripts;
        }
    }

    for (const auto& source : scene_.entities()) {
        const auto runtimeIt = runtimeIdBySource.find(source.id.value());
        if (runtimeIt == runtimeIdBySource.end() || !source.parent.has_value()) {
            continue;
        }
        const auto parentIt = runtimeIdBySource.find(source.parent->value());
        if (parentIt != runtimeIdBySource.end()) {
            (void)playRuntimeScene_.setParent(runtimeIt->second, parentIt->second);
        }
    }

    if (const auto cameraIt = runtimeIdBySource.find(sourceCameraEntityId.value()); cameraIt != runtimeIdBySource.end()) {
        playRuntimeCameraEntityId_ = cameraIt->second;
    }
    if (!playRuntimeCameraEntityId_.isValid()) {
        for (const auto& entity : playRuntimeScene_.entities()) {
            if (entity.camera.has_value()) {
                playRuntimeCameraEntityId_ = entity.id;
                break;
            }
        }
    }
    if (playRuntimeCameraEntityId_.isValid()) {
        auto* runtimeCamera = playRuntimeScene_.findEntity(playRuntimeCameraEntityId_);
        if (runtimeCamera != nullptr && !runtimeCamera->script.has_value()) {
            scene::ScriptComponent script;
            script.scriptName = "FlyPlayerController";
            (void)playRuntimeScene_.setScript(playRuntimeCameraEntityId_, script);
            ++stats.runtimeScripts;
            stats.injectedRuntimeFlyPlayer = true;
        }
    }

    std::ostringstream message;
    message << "Play runtime snapshot cooked"
            << " editorEntities=" << stats.editorEntities
            << " runtimeEntities=" << stats.runtimeEntities
            << " editableProxiesSkipped=" << stats.editableProxiesSkipped
            << " runtimeProxyOverrides=" << stats.runtimeProxyOverrides
            << " runtimeAssetInstances=" << stats.runtimeAssetInstances
            << " runtimePrimitiveInstances=" << stats.runtimePrimitiveInstances
            << " runtimeRenderChunks=" << stats.runtimeRenderChunks
            << " runtimeCameras=" << stats.runtimeCameras
            << " runtimeLights=" << stats.runtimeLights
            << " runtimeScripts=" << stats.runtimeScripts
            << " injectedRuntimeFlyPlayer=" << (stats.injectedRuntimeFlyPlayer ? 1 : 0)
            << " runtimeCameraId=" << playRuntimeCameraEntityId_.value();
    core::logInfo(core::LogCategory::Editor, message.str());

    return playRuntimeCameraEntityId_.isValid();
}

void MainWindow::startPlayMode()
{
    if (playModeActive_) {
        stopPlayMode();
    }

    ensureFlyPlayerScriptAsset();
    const auto playerId = findPlayableCameraEntity();
    if (!playerId.isValid()) {
        statusBar()->showMessage(QStringLiteral("Play needs a Player or Camera"));
        core::logWarning(core::LogCategory::Editor, "Play ignored: no scene camera/player exists");
        QMessageBox::warning(
            this,
            QStringLiteral("Play"),
            QStringLiteral("No Player or Camera found. Use GameObject > Player, then press Play again."));
        return;
    }

    QProgressDialog progress(
        QStringLiteral("Cooking runtime scene..."),
        QString(),
        0,
        100,
        this);
    progress.setWindowTitle(QStringLiteral("Play"));
    progress.setCancelButton(nullptr);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);
    progress.setValue(10);
    progress.show();
    qApp->processEvents();

    if (!buildPlayRuntimeSnapshot(playerId)) {
        progress.close();
        statusBar()->showMessage(QStringLiteral("Play runtime build failed"));
        core::logWarning(core::LogCategory::Editor, "Play ignored: runtime snapshot has no playable camera");
        QMessageBox::warning(
            this,
            QStringLiteral("Play"),
            QStringLiteral("Runtime scene has no playable camera after cooking."));
        return;
    }
    progress.setValue(70);
    qApp->processEvents();

    playModeActive_ = true;
    showPlayRuntimeWindow();
    refreshViewports();
    progress.setValue(100);
    statusBar()->showMessage(QStringLiteral("Play runtime window active"));
    core::logInfo(core::LogCategory::Editor, "Play mode started in an independent runtime window");
    appendPendingLogs();
}

void MainWindow::stopPlayMode()
{
    playModeActive_ = false;
    if (playRuntimeViewport_ != nullptr) {
        auto* runtimeViewport = playRuntimeViewport_;
        playRuntimeViewport_ = nullptr;
        runtimeViewport->setGameInputEnabled(false);
        runtimeViewport->setGameRuntimeSnapshotEnabled(false);
        runtimeViewport->setGameCameraEntity({});
        runtimeViewport->setRenderer(nullptr);
        runtimeViewport->close();
    }
    if (gameViewport_ != nullptr) {
        gameViewport_->setGameInputEnabled(false);
        gameViewport_->setGameRuntimeSnapshotEnabled(false);
        gameViewport_->setGameCameraEntity({});
        gameViewport_->setScene(&scene_);
        gameViewport_->setSelectedEntity(selectedEntityId_);
    }
    playRuntimeScene_.clear();
    playRuntimeCameraEntityId_ = {};
    refreshViewports();
    statusBar()->showMessage(QStringLiteral("Play stopped"));
    appendPendingLogs();
}

void MainWindow::showPlayRuntimeWindow()
{
    if (!playModeActive_ || !playRuntimeCameraEntityId_.isValid()) {
        return;
    }

    if (playRuntimeViewport_ != nullptr) {
        playRuntimeViewport_->raise();
        playRuntimeViewport_->activateWindow();
        playRuntimeViewport_->setFocus(Qt::OtherFocusReason);
        return;
    }

    auto* runtimeViewport = new ViewportWidget(ViewportMode::Game);
    playRuntimeViewport_ = runtimeViewport;
    runtimeViewport->setObjectName(QStringLiteral("PlayRuntimeViewport"));
    runtimeViewport->setWindowTitle(QStringLiteral("ProjectUnity Runtime - %1").arg(QString::fromStdString(std::string(scene_.name()))));
    runtimeViewport->setAttribute(Qt::WA_DeleteOnClose, true);
    runtimeViewport->resize(1280, 720);
    runtimeViewport->setScene(&playRuntimeScene_);
    runtimeViewport->setAssetManager(&assetManager_);
    runtimeViewport->setRenderer(renderer_.get());
    runtimeViewport->setEnvironmentSettings(environmentSettings_);
    runtimeViewport->setEditorSunLight(editorSunLight_);
    runtimeViewport->setShadowUpdateMode(shadowUpdateMode_);
    runtimeViewport->setSelectedEntity({});
    runtimeViewport->setGameCameraEntity(playRuntimeCameraEntityId_);
    runtimeViewport->setGameRuntimeSnapshotEnabled(true);
    runtimeViewport->setGameInputEnabled(true);
    connect(runtimeViewport, &QObject::destroyed, this, [this, runtimeViewport] {
        if (playRuntimeViewport_ != runtimeViewport) {
            return;
        }
        playRuntimeViewport_ = nullptr;
        if (playModeActive_) {
            stopPlayMode();
        }
    });
    runtimeViewport->show();
    runtimeViewport->raise();
    runtimeViewport->activateWindow();
    runtimeViewport->setFocus(Qt::OtherFocusReason);
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
