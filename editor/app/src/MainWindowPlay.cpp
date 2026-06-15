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
#include <optional>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace projectunity::editor {
namespace {

struct PlayRuntimeCookStats {
    std::uint64_t editorEntities {0};
    std::uint64_t editableProxies {0};
    std::uint64_t editableChildren {0};
    std::uint64_t runtimeEntities {0};
    std::uint64_t runtimeAssetInstances {0};
    std::uint64_t runtimeChunks {0};
    std::uint64_t runtimeDrawPackets {0};
    std::uint64_t mergedOrInstancedParts {0};
    std::uint64_t skippedEditableProxies {0};
    std::uint64_t overrideInstances {0};
    std::uint64_t mutableRuntimeEntities {0};
    std::uint64_t dynamicBatchMasks {0};
    std::uint64_t drawsBeforeCompile {0};
    std::uint64_t drawsAfterCompile {0};
    std::uint64_t runtimeCameras {0};
    std::uint64_t runtimeLights {0};
    std::uint64_t runtimeScripts {0};
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

[[nodiscard]] bool hasRuntimeGameplayState(const scene::Entity& entity) noexcept
{
    return entity.light.has_value() || entity.camera.has_value() || !entity.scripts.empty();
}

[[nodiscard]] bool requiresRuntimeEntity(const scene::Entity& entity) noexcept
{
    if (hasRuntimeGameplayState(entity)) {
        return true;
    }
    if (!entity.meshRenderer.has_value()) {
        return false;
    }
    const auto& cook = entity.meshRenderer->runtimeCook;
    return cook.mutableRuntime
        || !cook.staticBatchable
        || cook.grabbable
        || cook.physics != scene::RuntimePhysicsMode::None;
}

[[nodiscard]] std::uint64_t runtimePrimitivePartCount(const assets::ModelAsset& model) noexcept
{
    if (!model.primitiveInstances.empty()) {
        return model.primitiveInstances.size();
    }
    return model.primitives.size();
}

[[nodiscard]] std::uint64_t runtimeChunkCount(const assets::ModelAsset& model) noexcept
{
    if (!model.primitiveClusters.empty()) {
        return model.primitiveClusters.size();
    }
    return runtimePrimitivePartCount(model);
}

[[nodiscard]] std::uint64_t runtimeDrawPacketCount(const assets::ModelAsset& model) noexcept
{
    if (!model.primitiveClusters.empty()) {
        return model.primitiveClusters.size();
    }
    return model.primitives.empty() ? 0U : model.primitives.size();
}

void copyRuntimeComponents(
    const scene::Entity& source,
    scene::Scene& runtimeScene,
    scene::EntityId runtimeId,
    const std::optional<scene::MeshRendererComponent>& meshRendererOverride = std::nullopt)
{
    (void)runtimeScene.setTransform(runtimeId, source.transform);
    if (meshRendererOverride.has_value()) {
        (void)runtimeScene.setMeshRenderer(runtimeId, meshRendererOverride);
    } else if (source.meshRenderer.has_value()) {
        (void)runtimeScene.setMeshRenderer(runtimeId, source.meshRenderer);
    }
    if (source.light.has_value()) {
        (void)runtimeScene.setLight(runtimeId, source.light);
    }
    if (source.camera.has_value()) {
        (void)runtimeScene.setCamera(runtimeId, source.camera);
    }
    for (const auto& script : source.scripts) {
        (void)runtimeScene.addScript(runtimeId, script);
    }
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
    script.scriptAsset = "Assets/Scripts/FlyPlayerController.cpp";
    scriptRegistry_.applyDefaults(script);
    (void)scene_.addScript(player.id, script);
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
    const auto scriptPath = scriptsDir / "FlyPlayerController.cpp";
    if (!std::filesystem::exists(scriptPath)) {
        core::logWarning(
            core::LogCategory::Editor,
            "FlyPlayerController script asset is missing from Project/Assets/Scripts");
    }
}

bool MainWindow::cookPlayRuntimeScene(scene::EntityId sourceCameraEntityId)
{
    playRuntimeScene_.clear();
    playRuntimeScene_.setName(std::string(scene_.name()) + " Runtime");
    playRuntimeCameraEntityId_ = {};

    PlayRuntimeCookStats stats;
    std::unordered_map<std::uint64_t, scene::EntityId> runtimeIdBySource;
    runtimeIdBySource.reserve(scene_.entityCount());
    std::vector<std::pair<scene::EntityId, scene::EntityId>> runtimeParentLinks;
    runtimeParentLinks.reserve(scene_.entityCount());

    for (const auto& source : scene_.entities()) {
        ++stats.editorEntities;
        if (isEditablePrimitiveProxy(source)) {
            ++stats.editableProxies;
            if (source.parent.has_value()) {
                ++stats.editableChildren;
            }
            continue;
        }

        if (source.meshRenderer.has_value() && source.meshRenderer->renderable) {
            auto& runtime = playRuntimeScene_.createEntity("RuntimeAssetInstance: " + source.name);
            runtimeIdBySource[source.id.value()] = runtime.id;
            ++stats.runtimeEntities;

            auto renderer = *source.meshRenderer;
            if (!requiresRuntimeEntity(source)) {
                renderer.runtimeCook.staticBatchable = true;
                renderer.runtimeCook.mutableRuntime = false;
                renderer.runtimeCook.physics = scene::RuntimePhysicsMode::None;
                renderer.runtimeCook.grabbable = false;
                ++stats.runtimeAssetInstances;
            } else {
                ++stats.mutableRuntimeEntities;
            }
            copyRuntimeComponents(source, playRuntimeScene_, runtime.id, renderer);

            if (const auto model = assetManager_.model(source.meshRenderer->modelAssetId)) {
                const auto sourceParts = runtimePrimitivePartCount(*model);
                const auto chunks = runtimeChunkCount(*model);
                const auto drawPackets = runtimeDrawPacketCount(*model);
                stats.drawsBeforeCompile += sourceParts;
                stats.runtimeChunks += chunks;
                stats.runtimeDrawPackets += drawPackets;
                stats.mergedOrInstancedParts += sourceParts;
            }
            if (source.light.has_value()) {
                ++stats.runtimeLights;
            }
            if (source.camera.has_value()) {
                ++stats.runtimeCameras;
            }
            stats.runtimeScripts += source.scripts.size();
            continue;
        }

        auto& runtime = playRuntimeScene_.createEntity(source.name);
        runtimeIdBySource[source.id.value()] = runtime.id;
        ++stats.runtimeEntities;
        copyRuntimeComponents(source, playRuntimeScene_, runtime.id);
        if (source.light.has_value()) {
            ++stats.runtimeLights;
        }
        if (source.camera.has_value()) {
            ++stats.runtimeCameras;
        }
        stats.runtimeScripts += source.scripts.size();
    }

    for (const auto& source : scene_.entities()) {
        if (!isEditablePrimitiveProxy(source) || !source.meshRenderer.has_value()) {
            continue;
        }

        const auto model = assetManager_.model(source.meshRenderer->modelAssetId);
        const auto primitiveInstanceIndex = model == nullptr
            ? std::optional<std::uint32_t> {}
            : primitiveInstanceIndexForSnapshotProxy(*model, *source.meshRenderer);
        if (!primitiveInstanceIndex.has_value()) {
            ++stats.skippedEditableProxies;
            continue;
        }

        if (requiresRuntimeEntity(source)) {
            auto maskRenderer = *source.meshRenderer;
            maskRenderer.renderable = false;
            maskRenderer.runtimeCook.staticBatchable = false;
            maskRenderer.runtimeCook.mutableRuntime = true;
            auto& mask = playRuntimeScene_.createEntity("RuntimeBatchMask: " + source.name);
            copyRuntimeComponents(source, playRuntimeScene_, mask.id, maskRenderer);
            ++stats.runtimeEntities;
            ++stats.dynamicBatchMasks;
            if (source.parent.has_value()) {
                runtimeParentLinks.push_back({mask.id, *source.parent});
            }

            auto dynamicRenderer = *source.meshRenderer;
            dynamicRenderer.renderable = true;
            dynamicRenderer.runtimeCook.staticBatchable = false;
            dynamicRenderer.runtimeCook.mutableRuntime = true;
            auto& dynamic = playRuntimeScene_.createEntity("RuntimeEntity: " + source.name);
            runtimeIdBySource[source.id.value()] = dynamic.id;
            copyRuntimeComponents(source, playRuntimeScene_, dynamic.id, dynamicRenderer);
            ++stats.runtimeEntities;
            ++stats.mutableRuntimeEntities;
            ++stats.runtimeDrawPackets;
            if (stats.mergedOrInstancedParts > 0U) {
                --stats.mergedOrInstancedParts;
            }
            if (source.parent.has_value()) {
                runtimeParentLinks.push_back({dynamic.id, *source.parent});
            }
            if (source.light.has_value()) {
                ++stats.runtimeLights;
            }
            if (source.camera.has_value()) {
                ++stats.runtimeCameras;
            }
            stats.runtimeScripts += source.scripts.size();
            continue;
        }

        if (!editableProxyHasRuntimeOverride(source, assetManager_)) {
            ++stats.skippedEditableProxies;
            continue;
        }

        auto renderer = *source.meshRenderer;
        renderer.renderable = false;
        renderer.runtimeCook.staticBatchable = true;
        renderer.runtimeCook.mutableRuntime = false;
        auto& runtimeProxy = playRuntimeScene_.createEntity("RuntimeOverride: " + source.name);
        runtimeIdBySource[source.id.value()] = runtimeProxy.id;
        copyRuntimeComponents(source, playRuntimeScene_, runtimeProxy.id, renderer);
        ++stats.runtimeEntities;
        ++stats.overrideInstances;
        if (source.parent.has_value()) {
            runtimeParentLinks.push_back({runtimeProxy.id, *source.parent});
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
    for (const auto& [runtimeChildId, sourceParentId] : runtimeParentLinks) {
        const auto parentIt = runtimeIdBySource.find(sourceParentId.value());
        if (parentIt != runtimeIdBySource.end()) {
            (void)playRuntimeScene_.setParent(runtimeChildId, parentIt->second);
        }
    }

    stats.drawsAfterCompile += stats.runtimeDrawPackets;

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
    std::ostringstream message;
    message << "CookRuntimeScene"
            << " editorEntities=" << stats.editorEntities
            << " editableProxies=" << stats.editableProxies
            << " editableChildren=" << stats.editableChildren
            << " runtimeEntities=" << stats.runtimeEntities
            << " runtimeAssetInstances=" << stats.runtimeAssetInstances
            << " runtimeChunks=" << stats.runtimeChunks
            << " runtimeDrawPackets=" << stats.runtimeDrawPackets
            << " mergedOrInstancedParts=" << stats.mergedOrInstancedParts
            << " skippedEditableProxies=" << stats.skippedEditableProxies
            << " overrideInstances=" << stats.overrideInstances
            << " mutableRuntimeEntities=" << stats.mutableRuntimeEntities
            << " dynamicBatchMasks=" << stats.dynamicBatchMasks
            << " drawsBeforeCompile=" << stats.drawsBeforeCompile
            << " drawsAfterCompile=" << stats.drawsAfterCompile
            << " runtimeCameras=" << stats.runtimeCameras
            << " runtimeLights=" << stats.runtimeLights
            << " runtimeScripts=" << stats.runtimeScripts
            << " runtimeCameraFound=" << (playRuntimeCameraEntityId_.isValid() ? 1 : 0)
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
        statusBar()->showMessage(QStringLiteral("No runtime Camera found."));
        core::logWarning(core::LogCategory::Editor, "Play ignored: no scene camera/player exists");
        QMessageBox::warning(
            this,
            QStringLiteral("Play"),
            QStringLiteral("No runtime Camera found."));
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

    if (!cookPlayRuntimeScene(playerId)) {
        progress.close();
        statusBar()->showMessage(QStringLiteral("Play runtime build failed"));
        core::logWarning(core::LogCategory::Editor, "Play ignored: cooked runtime scene has no playable camera");
        QMessageBox::warning(
            this,
            QStringLiteral("Play"),
            QStringLiteral("No runtime Camera found."));
        return;
    }
    progress.setValue(70);
    qApp->processEvents();

    playModeActive_ = true;
    (void)scriptRuntime_.start(playRuntimeScene_);
    {
        const auto& scriptStats = scriptRuntime_.stats();
        std::ostringstream message;
        message << "PlayRuntimeScripts"
                << " scriptsRegistered=" << scriptStats.scriptsRegistered
                << " scriptComponentsFound=" << scriptStats.scriptComponentsFound
                << " scriptInstancesCreated=" << scriptStats.scriptInstancesCreated
                << " runtimeCameraFound=" << (playRuntimeCameraEntityId_.isValid() ? 1 : 0)
                << " scriptsUpdated=" << scriptStats.scriptsUpdated
                << " scriptErrors=" << scriptStats.scriptErrors;
        core::logInfo(core::LogCategory::Editor, message.str());
    }
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
        runtimeViewport->setGameScriptRuntime(nullptr);
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
    scriptRuntime_.stop();
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
    runtimeViewport->setGameScriptRuntime(&scriptRuntime_);
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
    return scripting::findRuntimeCameraEntity(scene_);
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
    script.scriptAsset = "Assets/Scripts/FlyPlayerController.cpp";
    scriptRegistry_.applyDefaults(script);
    (void)scene_.addScript(selectedEntityId_, script);
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
    const auto scriptId = inspectedScriptInstanceId_.isValid()
        ? inspectedScriptInstanceId_
        : (entity->scripts.empty() ? scene::ScriptInstanceId {} : entity->scripts.back().instanceId);
    if (scriptId.isValid()) {
        (void)scene_.removeScript(selectedEntityId_, scriptId);
    }
    inspectedScriptInstanceId_ = {};
    updateInspector();
    refreshViewports();
    statusBar()->showMessage(QStringLiteral("Script removed"));
}

} // namespace projectunity::editor
