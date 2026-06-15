#include <projectunity/editor/MainWindow.hpp>

#include <projectunity/editor/ProjectBrowserWidget.hpp>
#include <projectunity/editor/ViewportWidget.hpp>
#include <projectunity/renderer/IRenderer.hpp>

#include <DockManager.h>
#include <DockWidget.h>

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QImage>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPointF>
#include <QScreen>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <thread>

namespace projectunity::editor {
namespace {

constexpr int kLayoutVersion = 1;

[[nodiscard]] QString pathToQString(const std::filesystem::path& path)
{
    return QString::fromStdWString(path.wstring());
}

void printFrameCounters(const char* label, const renderer::RendererStats& stats)
{
    std::ostringstream line;
    line << label
        << ": drawCalls=" << stats.lastFrameMeshDrawCount
        << " objectsConsidered=" << stats.objectsConsidered
        << " passedFrustum=" << stats.passedFrustum
        << " visibleBatches=" << stats.visibleBatches
        << " occlusionTestedChunks=" << stats.lastFrameOcclusionTestedChunkCount
        << " occlusionRejectedChunks=" << stats.lastFrameOcclusionRejectedChunkCount
        << " occlusionOccluderChunks=" << stats.lastFrameOcclusionOccluderChunkCount
        << " occlusionRejectedInstances=" << stats.lastFrameOcclusionRejectedInstanceCount
        << " occlusionRejectedTriangles=" << stats.lastFrameOcclusionRejectedTriangleCount
        << " resourcePrepared=" << stats.resourcePrepared
        << " resourcePrepareMs=" << stats.resourcePrepareMs
        << " shadowCastersSubmitted=" << stats.shadowCastersSubmitted
        << " shadowCandidates=" << stats.shadowCandidates
        << " shadowSubmitted=" << stats.shadowSubmitted
        << " shadowTriangles=" << stats.shadowTriangles
        << " shadowCpuMs=" << stats.shadowCpuMs
        << " shadowGpuMs=" << stats.shadowGpuMs
        << " shadowRejectedByPolicy=" << stats.shadowRejectedByPolicy
        << " shadowRejectedByCasterCull=" << stats.shadowRejectedByCasterCull
        << " shadowCandidateInstances=" << stats.shadowCandidateInstances
        << " shadowPolicyRejectedInstances=" << stats.shadowPolicyRejectedInstances
        << " shadowBatchesSubmitted=" << stats.shadowBatchesSubmitted
        << " shadowInstancesSubmitted=" << stats.shadowInstancesSubmitted
        << " shadowTrianglesSubmitted=" << stats.shadowTrianglesSubmitted
        << " shadowGpuUs=" << stats.lastFrameShadowGpuTimeUs
        << " vkBindVertex=" << stats.vkBindVertex
        << " vkBindIndex=" << stats.vkBindIndex
        << " vkBindDescriptors=" << stats.vkBindDescriptors
        << " vkDrawIndexed=" << stats.vkDrawIndexed
        << " trianglesSubmitted=" << stats.trianglesSubmitted
        << " commandRecordingMs=" << stats.commandRecordingMs
        << " FPS=" << stats.FPS
        << '\n';
    std::cerr << line.str();
    const auto logPath = qEnvironmentVariable("PROJECTUNITY_COUNTER_LOG");
    if (!logPath.isEmpty()) {
        std::ofstream log(logPath.toStdWString(), std::ios::app);
        log << line.str();
    }
}

} // namespace

bool MainWindow::runSmokeChecks(QString* errorMessage)
{
    auto fail = [errorMessage](const QString& message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };

    if (dockManager_ == nullptr) {
        return fail(QStringLiteral("Dock manager was not created"));
    }

    const auto dockWidgets = dockManager_->dockWidgetsMap();
    if (dockWidgets.size() < 12) {
        return fail(QStringLiteral("Expected at least 12 editor dock widgets"));
    }

    const auto dockState = dockManager_->saveState(kLayoutVersion);
    if (dockState.isEmpty()) {
        return fail(QStringLiteral("Dock layout save returned an empty state"));
    }

    if (windowMenu_ == nullptr || defaultDockState_.isEmpty()) {
        return fail(QStringLiteral("Window menu or default dock layout state was not created"));
    }

    bool resetLayoutActionFound = false;
    bool hierarchyActionFound = false;
    for (const auto* action : windowMenu_->actions()) {
        resetLayoutActionFound = resetLayoutActionFound || action->text() == QStringLiteral("Reset Layout");
        hierarchyActionFound = hierarchyActionFound || action->text() == QStringLiteral("Hierarchy");
    }
    if (!resetLayoutActionFound || !hierarchyActionFound) {
        return fail(QStringLiteral("Window menu does not expose layout recovery actions"));
    }

    if (!dockManager_->restoreState(dockState, kLayoutVersion)) {
        return fail(QStringLiteral("Dock layout restore failed"));
    }

    if (consoleView_ == nullptr) {
        return fail(QStringLiteral("Console panel was not created"));
    }

    if (sceneViewport_ == nullptr || gameViewport_ == nullptr) {
        return fail(QStringLiteral("3D viewport panels were not created"));
    }

    if (renderer_ == nullptr || !renderer_->isReady()) {
        return fail(QStringLiteral("Editor Vulkan renderer was not initialized"));
    }

    const auto marker = QStringLiteral("Editor smoke log marker");
    core::logInfo(core::LogCategory::Editor, marker.toStdString());
    appendPendingLogs();
    if (!consoleView_->toPlainText().contains(marker)) {
        return fail(QStringLiteral("Console panel did not receive log entries"));
    }

    newScene();
    const auto offscreenPlatform = QApplication::platformName() == QStringLiteral("offscreen");
    const auto emptySceneFramesBefore = renderer_->stats().viewportFramesPresented;
    sceneViewport_->repaint();
    QApplication::processEvents();
    if (!offscreenPlatform && renderer_->stats().viewportFramesPresented == emptySceneFramesBefore) {
        return fail(QStringLiteral("Empty Scene View editor aids did not reach the Vulkan color path"));
    }

    const auto parentId = createEmptyEntity(QStringLiteral("Parent"));
    if (!parentId.isValid() || scene_.entityCount() != 1) {
        return fail(QStringLiteral("Scene entity creation failed"));
    }

    selectEntity(parentId);
    const auto childId = createEmptyEntity(QStringLiteral("Child"));
    if (!childId.isValid() || scene_.entityCount() != 2) {
        return fail(QStringLiteral("Scene child creation failed"));
    }

    const auto* parent = scene_.findEntity(parentId);
    if (parent == nullptr || parent->children.size() != 1 || parent->children.front() != childId) {
        return fail(QStringLiteral("Scene hierarchy was not reflected in engine data"));
    }

    sceneViewport_->resize(640, 480);
    gameViewport_->resize(640, 360);
    sceneViewport_->setSelectedEntity(parentId);
    QString viewportError;
    if (!sceneViewport_->runSelfTest(&viewportError)) {
        return fail(QStringLiteral("Scene View self-test failed: %1").arg(viewportError));
    }
    if (!gameViewport_->runSelfTest(&viewportError)) {
        return fail(QStringLiteral("Game View self-test failed: %1").arg(viewportError));
    }

    const auto projectedPick = sceneViewport_->pickEntityAt(QPointF(
        sceneViewport_->width() * 0.5,
        sceneViewport_->height() * 0.5));
    if (!projectedPick.has_value()) {
        return fail(QStringLiteral("Viewport center ray did not produce a valid 3D pick test result"));
    }

    selectEntity(childId);
    positionX_->setValue(7.0);
    positionY_->setValue(8.0);
    positionZ_->setValue(9.0);
    QApplication::processEvents();

    const auto* child = scene_.findEntity(childId);
    if (child == nullptr
        || child->transform.position.x != 7.0F
        || child->transform.position.y != 8.0F
        || child->transform.position.z != 9.0F) {
        return fail(QStringLiteral("Inspector transform edit did not update the scene"));
    }

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        return fail(QStringLiteral("Unable to create temporary directory for scene smoke test"));
    }

    const auto scenePath = tempDir.filePath(QStringLiteral("phase2_smoke.scene.json"));
    if (!saveSceneToPath(scenePath)) {
        return fail(QStringLiteral("Scene smoke save failed"));
    }

    newScene();
    if (!loadSceneFromPath(scenePath)) {
        return fail(QStringLiteral("Scene smoke load failed"));
    }

    if (scene_.entityCount() != 2 || scene_.rootEntities().size() != 1) {
        return fail(QStringLiteral("Loaded scene lost entity hierarchy"));
    }

    newScene();
    const auto playCameraId = createPlayerEntity();
    const auto editorEntityCountBeforePlay = scene_.entityCount();
    const auto* editorPlayerBeforePlay = scene_.findEntity(playCameraId);
    if (editorPlayerBeforePlay == nullptr || !editorPlayerBeforePlay->camera.has_value()) {
        return fail(QStringLiteral("Play smoke could not create an editor Player camera"));
    }
    const auto editorPlayerPositionBeforePlay = editorPlayerBeforePlay->transform.position;
    startPlayMode();
    QApplication::processEvents();
    if (!playModeActive_
        || playRuntimeViewport_ == nullptr
        || !playRuntimeViewport_->isWindow()
        || playRuntimeViewport_ == gameViewport_
        || !playRuntimeCameraEntityId_.isValid()
        || playRuntimeScene_.entityCount() != editorEntityCountBeforePlay) {
        return fail(QStringLiteral("Play did not open an independent runtime window snapshot"));
    }
    auto* runtimeCamera = playRuntimeScene_.findEntity(playRuntimeCameraEntityId_);
    if (runtimeCamera == nullptr) {
        return fail(QStringLiteral("Play runtime snapshot did not contain the camera entity"));
    }
    auto runtimeTransform = runtimeCamera->transform;
    runtimeTransform.position.x += 12.0F;
    (void)playRuntimeScene_.setTransform(playRuntimeCameraEntityId_, runtimeTransform);
    const auto* editorPlayerAfterRuntimeEdit = scene_.findEntity(playCameraId);
    if (editorPlayerAfterRuntimeEdit == nullptr
        || !math::nearlyEqual(editorPlayerAfterRuntimeEdit->transform.position, editorPlayerPositionBeforePlay)) {
        return fail(QStringLiteral("Runtime snapshot mutation leaked into the editor scene"));
    }
    stopPlayMode();
    QApplication::processEvents();
    if (playModeActive_
        || playRuntimeViewport_ != nullptr
        || playRuntimeCameraEntityId_.isValid()
        || playRuntimeScene_.entityCount() != 0U) {
        return fail(QStringLiteral("Play stop did not discard the runtime snapshot and window"));
    }

    const auto assetExamplePath = pathToQString(
        std::filesystem::path(PROJECTUNITY_SOURCE_DIR) / "examples" / "basic_assets" / "TexturedTriangle.gltf");
    const auto imported = importAssetFromPath(assetExamplePath, true);
    if (!imported.success || projectBrowser_ == nullptr || projectBrowser_->rootAssetCount() == 0U) {
        return fail(QStringLiteral("Project Browser model import smoke failed: success=%1 rootAssets=%2 error=%3")
            .arg(imported.success)
            .arg(projectBrowser_ == nullptr ? -1 : static_cast<int>(projectBrowser_->rootAssetCount()))
            .arg(QString::fromStdString(imported.error)));
    }

    const auto* importedEntity = scene_.findEntity(selectedEntityId_);
    if (importedEntity == nullptr || !importedEntity->meshRenderer.has_value()) {
        return fail(QStringLiteral("Imported model did not create a mesh renderer entity"));
    }
    if (auto* sceneDock = dockManager_->dockWidgetsMap().value(QStringLiteral("Scene View"), nullptr)) {
        sceneDock->toggleView(true);
        sceneDock->setAsCurrentTab();
        sceneDock->raise();
        QApplication::processEvents();
    }

    sceneViewport_->setSelectedEntity(selectedEntityId_);
    if (!sceneViewport_->runSelfTest(&viewportError)) {
        return fail(QStringLiteral("Imported mesh viewport self-test failed: %1").arg(viewportError));
    }
    sceneViewport_->repaint();
    QApplication::processEvents();

    const auto& stats = renderer_->stats();
    if (!offscreenPlatform && (stats.meshDrawsPresented == 0
            || stats.texturedMeshDrawsPresented == 0
            || stats.colorMeshDrawsPresented == 0
            || stats.lastFrameCandidateMeshDrawCount == 0
            || stats.lastFrameCandidateTriangleCount == 0
            || stats.lastFrameRenderCpuTimeUs == 0
            || stats.residentMeshCount == 0
            || stats.residentTextureCount == 0
            || stats.totalMeshUploadCount == 0
            || stats.totalTextureUploadCount == 0
            || stats.totalStaticUploadBytes == 0
            || stats.totalColorUploadBytes == 0)) {
        appendPendingLogs();
        return fail(QStringLiteral(
            "Imported textured mesh, gizmo, and profiling stats did not reach Vulkan: frames=%1 draws=%2 textured=%3 gizmos=%4 candidates=%5 triangles=%6 cpuUs=%7 meshes=%8 textures=%9 uploadBytes=%10 colorBytes=%11 shadows=%12 casters=%13 logs=%14")
            .arg(static_cast<qulonglong>(stats.viewportFramesPresented))
            .arg(static_cast<qulonglong>(stats.meshDrawsPresented))
            .arg(static_cast<qulonglong>(stats.texturedMeshDrawsPresented))
            .arg(static_cast<qulonglong>(stats.colorMeshDrawsPresented))
            .arg(static_cast<qulonglong>(stats.lastFrameCandidateMeshDrawCount))
            .arg(static_cast<qulonglong>(stats.lastFrameCandidateTriangleCount))
            .arg(static_cast<qulonglong>(stats.lastFrameRenderCpuTimeUs))
            .arg(static_cast<qulonglong>(stats.residentMeshCount))
            .arg(static_cast<qulonglong>(stats.residentTextureCount))
            .arg(static_cast<qulonglong>(stats.totalStaticUploadBytes))
            .arg(static_cast<qulonglong>(stats.totalColorUploadBytes))
            .arg(static_cast<qulonglong>(stats.shadowFramesPresented))
            .arg(static_cast<qulonglong>(stats.shadowCasterDrawsPresented))
            .arg(consoleView_->toPlainText().right(1200)));
    }
    const auto splitImportPath = std::filesystem::path(PROJECTUNITY_SOURCE_DIR)
        / "Project" / "Assets" / "VisualVerification" / "OrientationTest.glb";
    if (std::filesystem::exists(splitImportPath)) {
        newScene();
        const auto splitImport = importAssetFromPath(pathToQString(splitImportPath), true);
        const auto* splitRoot = scene_.findEntity(selectedEntityId_);
        const auto splitHasPartChild = splitRoot != nullptr && std::any_of(
            splitRoot->children.begin(),
            splitRoot->children.end(),
            [this](const scene::EntityId childId) {
                const auto* child = scene_.findEntity(childId);
                return child != nullptr
                    && child->meshRenderer.has_value()
                    && child->meshRenderer->primitiveInstanceIndex.has_value()
                    && !child->meshRenderer->renderable;
            });
        if (!splitImport.success
            || splitRoot == nullptr
            || !splitRoot->meshRenderer.has_value()
            || !splitRoot->meshRenderer->renderable
            || splitHasPartChild) {
            return fail(QStringLiteral("Imported multi-instance model polluted the main Hierarchy with internal mesh part children"));
        }
    }
    const auto cameraImportPath = std::filesystem::path(PROJECTUNITY_SOURCE_DIR)
        / "Project" / "Assets" / "VisualVerification" / "NodePerformanceTest.glb";
    if (std::filesystem::exists(cameraImportPath)) {
        newScene();
        const auto cameraImport = importAssetFromPath(pathToQString(cameraImportPath), true);
        const auto importedRootId = selectedEntityId_;
        const auto* importedRoot = scene_.findEntity(importedRootId);
        const auto hasImportedCamera = std::any_of(scene_.entities().begin(), scene_.entities().end(), [importedRootId](const scene::Entity& entity) {
            return entity.camera.has_value()
                && entity.parent == std::optional<scene::EntityId>(importedRootId);
        });
        if (!cameraImport.success || importedRoot == nullptr || !importedRoot->meshRenderer.has_value() || !hasImportedCamera) {
            return fail(QStringLiteral("Imported NodePerformanceTest did not expose its camera as an editable child entity"));
        }
    }
    if (skyColorR_ == nullptr
        || groundColorB_ == nullptr
        || environmentIntensity_ == nullptr
        || sunAzimuth_ == nullptr
        || sunElevation_ == nullptr
        || sunColorR_ == nullptr
        || sunColorG_ == nullptr
        || sunColorB_ == nullptr
        || sunIntensity_ == nullptr
        || shadowModeCombo_ == nullptr
        || shadowModeCombo_->count() != 3
        || resetLightingButton_ == nullptr) {
        return fail(QStringLiteral("Lighting panel environment controls were not created"));
    }
    shadowModeCombo_->setCurrentIndex(shadowModeCombo_->findData(static_cast<int>(renderer::RenderShadowUpdateMode::Frozen)));
    if (sceneViewport_->shadowUpdateMode() != renderer::RenderShadowUpdateMode::Frozen
        || gameViewport_->shadowUpdateMode() != renderer::RenderShadowUpdateMode::Frozen) {
        return fail(QStringLiteral("Lighting shadow mode did not propagate to both viewports"));
    }
    QImage environmentImage(8, 4, QImage::Format_RGBA8888);
    for (int y = 0; y < environmentImage.height(); ++y) {
        for (int x = 0; x < environmentImage.width(); ++x) {
            environmentImage.setPixelColor(x, y, QColor(24 + x * 20, 32 + y * 32, 128 + x * 8, 255));
        }
    }
    const auto environmentPath = tempDir.filePath(QStringLiteral("smoke_environment.png"));
    if (!environmentImage.save(environmentPath)) {
        return fail(QStringLiteral("Unable to write temporary environment texture"));
    }
    const auto environmentImport = importAssetFromPath(environmentPath, false);
    if (!environmentImport.success || environmentImport.record.type != assets::AssetType::Texture2D) {
        return fail(QStringLiteral("Environment texture import failed: %1").arg(QString::fromStdString(environmentImport.error)));
    }
    if (projectBrowser_ == nullptr || !projectBrowser_->selectAsset(environmentImport.record.id)) {
        return fail(QStringLiteral("Project Browser did not select the imported environment texture"));
    }
    useSelectedTextureAsEnvironment();
    if (environmentTexture_ == nullptr || environmentTextureId_ != environmentImport.record.id) {
        return fail(QStringLiteral("Lighting panel did not assign the selected texture environment"));
    }
    if (!offscreenPlatform) {
        const auto textureUploadsBeforeLighting = renderer_->stats().totalTextureUploadCount;
        skyColorR_->setValue(0.65);
        groundColorB_->setValue(0.10);
        environmentIntensity_->setValue(1.15);
        applyLightingSettings();
        sceneViewport_->repaint();
        QApplication::processEvents();
        if (renderer_->stats().totalTextureUploadCount <= textureUploadsBeforeLighting) {
            return fail(QStringLiteral("Lighting environment controls did not refresh Vulkan IBL textures"));
        }
    }
    if (!offscreenPlatform) {
        std::uint64_t previousStaticBytes = renderer_->stats().totalStaticUploadBytes;
        bool uploadsIdle = false;
        for (int frame = 0; frame < 24 && !uploadsIdle; ++frame) {
            sceneViewport_->repaint();
            QApplication::processEvents();
            const auto& cachedStats = renderer_->stats();
            uploadsIdle = cachedStats.lastFrameStaticUploadBytes == 0
                && cachedStats.totalStaticUploadBytes == previousStaticBytes;
            previousStaticBytes = cachedStats.totalStaticUploadBytes;
        }
        if (!uploadsIdle) {
            const auto& cachedStats = renderer_->stats();
            return fail(QStringLiteral(
                "Vulkan static asset streaming did not become idle: meshUploads=%1 textureUploads=%2 staticBytes=%3 lastBytes=%4")
                .arg(static_cast<qulonglong>(cachedStats.totalMeshUploadCount))
                .arg(static_cast<qulonglong>(cachedStats.totalTextureUploadCount))
                .arg(static_cast<qulonglong>(cachedStats.totalStaticUploadBytes))
                .arg(static_cast<qulonglong>(cachedStats.lastFrameStaticUploadBytes)));
        }
    }
    resetLightingDefaults();
    const renderer::RenderEnvironmentSettings defaultEnvironment;
    if (environmentTexture_ != nullptr
        || environmentTextureId_.isValid()
        || environmentSettings_.skyColor != defaultEnvironment.skyColor
        || environmentSettings_.groundColor != defaultEnvironment.groundColor
        || environmentSettings_.intensity != defaultEnvironment.intensity
        || shadowUpdateMode_ != renderer::RenderShadowUpdateMode::Off
        || sceneViewport_->shadowUpdateMode() != renderer::RenderShadowUpdateMode::Off
        || gameViewport_->shadowUpdateMode() != renderer::RenderShadowUpdateMode::Off) {
        return fail(QStringLiteral("Lighting reset defaults did not restore procedural environment state"));
    }

    return true;
}

bool MainWindow::runPhase6VisualChecks(QString* errorMessage)
{
    auto fail = [errorMessage](const QString& message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };
    if (QApplication::platformName() == QStringLiteral("offscreen")) {
        return fail(QStringLiteral("Phase 6 visual smoke requires a visible Qt platform"));
    }
    if (renderer_ == nullptr || !renderer_->isReady() || sceneViewport_ == nullptr) {
        return fail(QStringLiteral("Phase 6 visual smoke requires a ready Vulkan renderer and Scene View"));
    }
    const auto assetRoot = std::filesystem::path(PROJECTUNITY_SOURCE_DIR)
        / "Project" / "Assets" / "VisualVerification";
    if (!std::filesystem::exists(assetRoot)) {
        return fail(QStringLiteral("Missing Phase 6 visual asset pack at %1").arg(pathToQString(assetRoot)));
    }
    if (auto* sceneDock = dockManager_->dockWidgetsMap().value(QStringLiteral("Scene View"), nullptr)) {
        sceneDock->toggleView(true);
        sceneDock->setAsCurrentTab();
        sceneDock->raise();
    }
    sceneViewport_->resize(960, 540);
    QApplication::processEvents();
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    QApplication::processEvents();
    auto waitForStaticUploadsIdle = [&]() {
        std::uint64_t previousStaticBytes = renderer_->stats().totalStaticUploadBytes;
        for (int frame = 0; frame < 24; ++frame) {
            sceneViewport_->repaint();
            QApplication::processEvents();
            const auto& currentStats = renderer_->stats();
            if (currentStats.lastFrameStaticUploadBytes == 0
                && currentStats.totalStaticUploadBytes == previousStaticBytes) {
                return true;
            }
            previousStaticBytes = currentStats.totalStaticUploadBytes;
        }
        return false;
    };

    const std::array<const char*, 13> assetNames {{
        "OrientationTest.glb",
        "NegativeScaleTest.glb",
        "TextureCoordinateTest.glb",
        "NormalTangentTest.glb",
        "NormalTangentMirrorTest.glb",
        "Avocado.glb",
        "BoomBox.glb",
        "Lantern.glb",
        "MetalRoughSpheres.glb",
        "AlphaBlendModeTest.glb",
        "DirectionalLight.glb",
        "LightsPunctualLamp.glb",
        "NodePerformanceTest.glb",
    }};

    bool sawDirectionalCascades = false;
    bool sawPointCubemap = false;
    bool sawLargeScene = false;
    bool sawEditableImportedLight = false;
    bool sawEditableImportedCamera = false;
    bool sawRenderWorldLookAwayCull = false;
    for (const auto* assetName : assetNames) {
        const auto assetPath = assetRoot / assetName;
        if (!std::filesystem::exists(assetPath)) {
            return fail(QStringLiteral("Missing Phase 6 visual asset %1").arg(QString::fromUtf8(assetName)));
        }
        newScene();
        const auto imported = importAssetFromPath(pathToQString(assetPath), true);
        if (!imported.success) {
            return fail(QStringLiteral("Phase 6 visual import failed for %1: %2")
                .arg(QString::fromUtf8(assetName))
                .arg(QString::fromStdString(imported.error)));
        }
        sceneViewport_->setSelectedEntity(selectedEntityId_);
        sceneViewport_->repaint();
        QApplication::processEvents();
        if (!waitForStaticUploadsIdle()) {
            return fail(QStringLiteral("Phase 6 visual static asset streaming did not become idle for %1").arg(QString::fromUtf8(assetName)));
        }
        const auto stats = renderer_->stats();
        if (stats.lastFrameMeshDrawCount == 0
            || stats.lastFrameCandidateMeshDrawCount == 0
            || stats.lastFrameCandidateTriangleCount == 0
            || stats.lastFrameRenderCpuTimeUs == 0) {
            return fail(QStringLiteral(
                "Phase 6 visual render produced no measurable mesh frame for %1: draws=%2 candidates=%3 triangles=%4 cpuUs=%5")
                .arg(QString::fromUtf8(assetName))
                .arg(static_cast<qulonglong>(stats.lastFrameMeshDrawCount))
                .arg(static_cast<qulonglong>(stats.lastFrameCandidateMeshDrawCount))
                .arg(static_cast<qulonglong>(stats.lastFrameCandidateTriangleCount))
                .arg(static_cast<qulonglong>(stats.lastFrameRenderCpuTimeUs)));
        }
        sawDirectionalCascades = sawDirectionalCascades
            || stats.lastFrameShadowViewCount == static_cast<std::uint64_t>(renderer::kMaxShadowCascades);
        sawPointCubemap = sawPointCubemap || stats.lastFrameShadowViewCount == 6U;
        if (QString::fromUtf8(assetName) == QStringLiteral("NodePerformanceTest.glb")) {
            printFrameCounters("NodePerformanceTest facing map", stats);
            sawLargeScene = stats.lastFrameCandidateTriangleCount > 10000U
                && stats.lastFrameMeshBatchCount > 0U
                && stats.lastFrameRenderChunkCount > 0U
                && stats.lastFrameVisibleRenderChunkCount > 0U
                && stats.lastFrameRenderInstanceCount > 0U
                && stats.lastFrameVisibleRenderInstanceCount > 0U;
            sawEditableImportedLight = std::any_of(scene_.entities().begin(), scene_.entities().end(), [](const scene::Entity& entity) {
                return entity.light.has_value()
                    && entity.name == "Sun"
                    && entity.light->type == scene::LightComponentType::Directional
                    && entity.light->intensity <= 8.0F;
            });
            sawEditableImportedCamera = std::any_of(scene_.entities().begin(), scene_.entities().end(), [](const scene::Entity& entity) {
                return entity.camera.has_value()
                    && entity.camera->projection == scene::CameraComponentProjection::Perspective
                    && entity.parent.has_value();
            });
            sceneViewport_->setCameraForTesting({5000.0F, 5000.0F, 5000.0F}, 500.0F, 0.65F, -0.38F);
            for (int frame = 0; frame < 3; ++frame) {
                sceneViewport_->repaint();
                QApplication::processEvents();
            }
            const auto awayStats = renderer_->stats();
            printFrameCounters("NodePerformanceTest facing empty space", awayStats);
            sawRenderWorldLookAwayCull = awayStats.lastFrameVisibleRenderChunkCount < stats.lastFrameVisibleRenderChunkCount
                && awayStats.lastFrameVisibleRenderInstanceCount < stats.lastFrameVisibleRenderInstanceCount
                && awayStats.lastFrameMeshDrawCount < stats.lastFrameMeshDrawCount
                && awayStats.lastFrameVisibleTriangleCount < stats.lastFrameVisibleTriangleCount
                && awayStats.lastFrameResourcePrepareCpuTimeUs <= stats.lastFrameResourcePrepareCpuTimeUs;
            if (!sawRenderWorldLookAwayCull) {
                return fail(QStringLiteral(
                    "Phase 6 RenderWorld look-away culling did not reduce NodePerformanceTest workload: chunks %1/%2 -> %3/%4, instances %5/%6 -> %7/%8, draws %9 -> %10, triangles %11 -> %12, prepareUs %13 -> %14")
                    .arg(static_cast<qulonglong>(stats.lastFrameVisibleRenderChunkCount))
                    .arg(static_cast<qulonglong>(stats.lastFrameRenderChunkCount))
                    .arg(static_cast<qulonglong>(awayStats.lastFrameVisibleRenderChunkCount))
                    .arg(static_cast<qulonglong>(awayStats.lastFrameRenderChunkCount))
                    .arg(static_cast<qulonglong>(stats.lastFrameVisibleRenderInstanceCount))
                    .arg(static_cast<qulonglong>(stats.lastFrameRenderInstanceCount))
                    .arg(static_cast<qulonglong>(awayStats.lastFrameVisibleRenderInstanceCount))
                    .arg(static_cast<qulonglong>(awayStats.lastFrameRenderInstanceCount))
                    .arg(static_cast<qulonglong>(stats.lastFrameMeshDrawCount))
                    .arg(static_cast<qulonglong>(awayStats.lastFrameMeshDrawCount))
                    .arg(static_cast<qulonglong>(stats.lastFrameVisibleTriangleCount))
                    .arg(static_cast<qulonglong>(awayStats.lastFrameVisibleTriangleCount))
                    .arg(static_cast<qulonglong>(stats.lastFrameResourcePrepareCpuTimeUs))
                    .arg(static_cast<qulonglong>(awayStats.lastFrameResourcePrepareCpuTimeUs)));
            }
        }
    }

    if (!sawDirectionalCascades) {
        return fail(QStringLiteral("Phase 6 visual smoke did not exercise cascaded directional shadows"));
    }
    if (!sawPointCubemap) {
        return fail(QStringLiteral("Phase 6 visual smoke did not exercise point-light cubemap shadows"));
    }
    if (!sawLargeScene) {
        return fail(QStringLiteral("Phase 6 visual smoke did not exercise NodePerformanceTest large-scene counters"));
    }
    if (!sawEditableImportedLight) {
        return fail(QStringLiteral("Phase 6 visual smoke did not create an editable imported light for NodePerformanceTest"));
    }
    if (!sawEditableImportedCamera) {
        return fail(QStringLiteral("Phase 6 visual smoke did not create an editable imported camera for NodePerformanceTest"));
    }
    if (!sawRenderWorldLookAwayCull) {
        return fail(QStringLiteral("Phase 6 visual smoke did not exercise RenderWorld look-away culling"));
    }

    const auto externalAssetPath = qEnvironmentVariable("PROJECTUNITY_PHASE6_EXTERNAL_ASSET");
    if (!externalAssetPath.isEmpty()) {
        const auto path = std::filesystem::path(externalAssetPath.toStdWString());
        if (!std::filesystem::exists(path)) {
            return fail(QStringLiteral("External Phase 6 asset does not exist: %1").arg(externalAssetPath));
        }
        newScene();
        const auto imported = importAssetFromPath(externalAssetPath, true);
        if (!imported.success) {
            return fail(QStringLiteral("External Phase 6 asset import failed: %1")
                .arg(QString::fromStdString(imported.error)));
        }
        sceneViewport_->setSelectedEntity(selectedEntityId_);
        if (const auto model = assetManager_.model(imported.record.id);
            model != nullptr && !model->primitiveInstances.empty()) {
            auto minimum = model->primitiveInstances.front().bounds.minimum;
            auto maximum = model->primitiveInstances.front().bounds.maximum;
            for (const auto& instance : model->primitiveInstances) {
                minimum.x = std::min(minimum.x, instance.bounds.minimum.x);
                minimum.y = std::min(minimum.y, instance.bounds.minimum.y);
                minimum.z = std::min(minimum.z, instance.bounds.minimum.z);
                maximum.x = std::max(maximum.x, instance.bounds.maximum.x);
                maximum.y = std::max(maximum.y, instance.bounds.maximum.y);
                maximum.z = std::max(maximum.z, instance.bounds.maximum.z);
            }
            const auto center = (minimum + maximum) * 0.5F;
            const auto extent = maximum - minimum;
            const auto radius = std::sqrt(extent.x * extent.x + extent.y * extent.y + extent.z * extent.z) * 0.5F;
            sceneViewport_->setCameraForTesting(
                center,
                std::clamp(radius * 2.4F, 10.0F, 480.0F),
                0.65F,
                -0.55F);
        }
        sceneViewport_->repaint();
        QApplication::processEvents();
        if (!waitForStaticUploadsIdle()) {
            return fail(QStringLiteral("External Phase 6 asset uploads did not become idle"));
        }
        const auto selectedStats = renderer_->stats();
        sceneViewport_->setSelectedEntity({});
        sceneViewport_->repaint();
        QApplication::processEvents();
        const auto unselectedStats = renderer_->stats();
        sceneViewport_->setSelectedEntity(selectedEntityId_);

        auto* selected = scene_.findEntity(selectedEntityId_);
        if (selected == nullptr) {
            return fail(QStringLiteral("External Phase 6 asset selected entity disappeared"));
        }
        const auto originalTransform = selected->transform;
        auto movedTransform = originalTransform;
        movedTransform.position.x += 0.25F;
        const auto moveStart = std::chrono::steady_clock::now();
        if (!scene_.setTransform(selectedEntityId_, movedTransform)) {
            return fail(QStringLiteral("External Phase 6 asset transform update failed"));
        }
        sceneViewport_->repaint();
        QApplication::processEvents();
        const auto moveWallTimeUs = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - moveStart).count());
        const auto movedStats = renderer_->stats();
        printFrameCounters("External asset facing map", movedStats);
        (void)scene_.setTransform(selectedEntityId_, originalTransform);

        std::cerr
            << "External Phase 6 interaction profile: selectedVisibleTriangles=" << selectedStats.lastFrameVisibleTriangleCount
            << " unselectedVisibleTriangles=" << unselectedStats.lastFrameVisibleTriangleCount
            << " movedVisibleTriangles=" << movedStats.lastFrameVisibleTriangleCount
            << " moveWallUs=" << moveWallTimeUs
            << " editorBuildUs=" << movedStats.lastFrameEditorBuildCpuTimeUs
            << " renderWorldBuildUs=" << movedStats.lastFrameRenderWorldBuildCpuTimeUs
            << " renderCpuUs=" << movedStats.lastFrameRenderCpuTimeUs
            << " gpuUs=" << movedStats.lastFrameGpuTimeUs
            << " rebuiltRecords=" << movedStats.lastFrameRenderWorldRebuiltRecordCount
            << " reusedRecords=" << movedStats.lastFrameRenderWorldReusedRecordCount
            << " meshBatches=" << movedStats.lastFrameMeshBatchCount
            << " staticUploadBytes=" << movedStats.lastFrameStaticUploadBytes
            << '\n';

        const auto screenshotPath = qEnvironmentVariable("PROJECTUNITY_PHASE6_EXTERNAL_SCREENSHOT");
        if (!screenshotPath.isEmpty()) {
            const auto screenshot = sceneViewport_->screen()->grabWindow(sceneViewport_->winId());
            if (screenshot.isNull() || !screenshot.save(screenshotPath)) {
                return fail(QStringLiteral("Unable to save external Phase 6 viewport screenshot"));
            }
        }

        sceneViewport_->setCameraForTesting({5000.0F, 5000.0F, 5000.0F}, 500.0F, 0.65F, -0.38F);
        for (int frame = 0; frame < 3; ++frame) {
            sceneViewport_->repaint();
            QApplication::processEvents();
        }
        const auto awayStats = renderer_->stats();
        printFrameCounters("External asset facing empty space", awayStats);
        std::cerr
            << "External Phase 6 look-away profile: visibleChunks=" << movedStats.lastFrameVisibleRenderChunkCount
            << "->" << awayStats.lastFrameVisibleRenderChunkCount
            << " visibleInstances=" << movedStats.lastFrameVisibleRenderInstanceCount
            << "->" << awayStats.lastFrameVisibleRenderInstanceCount
            << " meshDraws=" << movedStats.lastFrameMeshDrawCount
            << "->" << awayStats.lastFrameMeshDrawCount
            << " visibleTriangles=" << movedStats.lastFrameVisibleTriangleCount
            << "->" << awayStats.lastFrameVisibleTriangleCount
            << '\n';

        if (movedStats.lastFrameCandidateTriangleCount < 64'000U
            || movedStats.viewportFramesPresented <= selectedStats.viewportFramesPresented) {
            return fail(QStringLiteral("External Phase 6 interaction profile did not present the moved large asset"));
        }
        if (awayStats.lastFrameVisibleRenderChunkCount >= movedStats.lastFrameVisibleRenderChunkCount
            || awayStats.lastFrameVisibleRenderInstanceCount >= movedStats.lastFrameVisibleRenderInstanceCount
            || awayStats.lastFrameMeshDrawCount >= movedStats.lastFrameMeshDrawCount
            || awayStats.lastFrameVisibleTriangleCount >= movedStats.lastFrameVisibleTriangleCount) {
            return fail(QStringLiteral(
                "External Phase 6 look-away culling did not reduce workload: chunks %1 -> %2, instances %3 -> %4, draws %5 -> %6, triangles %7 -> %8")
                .arg(static_cast<qulonglong>(movedStats.lastFrameVisibleRenderChunkCount))
                .arg(static_cast<qulonglong>(awayStats.lastFrameVisibleRenderChunkCount))
                .arg(static_cast<qulonglong>(movedStats.lastFrameVisibleRenderInstanceCount))
                .arg(static_cast<qulonglong>(awayStats.lastFrameVisibleRenderInstanceCount))
                .arg(static_cast<qulonglong>(movedStats.lastFrameMeshDrawCount))
                .arg(static_cast<qulonglong>(awayStats.lastFrameMeshDrawCount))
                .arg(static_cast<qulonglong>(movedStats.lastFrameVisibleTriangleCount))
                .arg(static_cast<qulonglong>(awayStats.lastFrameVisibleTriangleCount)));
        }
    }
    return true;
}

} // namespace projectunity::editor
