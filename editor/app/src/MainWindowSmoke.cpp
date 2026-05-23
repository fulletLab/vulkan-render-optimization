#include <projectunity/editor/MainWindow.hpp>

#include <projectunity/editor/ViewportWidget.hpp>
#include <projectunity/renderer/IRenderer.hpp>

#include <DockManager.h>
#include <DockWidget.h>

#include <QAction>
#include <QApplication>
#include <QDoubleSpinBox>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPointF>
#include <QTableWidget>
#include <QTemporaryDir>

#include <filesystem>

namespace projectunity::editor {
namespace {

constexpr int kLayoutVersion = 1;

[[nodiscard]] QString pathToQString(const std::filesystem::path& path)
{
    return QString::fromStdWString(path.wstring());
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
    const auto emptySceneFramesBefore = renderer_->stats().viewportFramesPresented;
    sceneViewport_->repaint();
    QApplication::processEvents();
    if (renderer_->stats().viewportFramesPresented != emptySceneFramesBefore) {
        return fail(QStringLiteral("Empty Scene View presented a Vulkan clear frame over editor aids"));
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

    const auto assetExamplePath = pathToQString(
        std::filesystem::path(PROJECTUNITY_SOURCE_DIR) / "examples" / "basic_assets" / "TexturedTriangle.gltf");
    const auto imported = importAssetFromPath(assetExamplePath, true);
    if (!imported.success || assetTable_ == nullptr || assetTable_->rowCount() == 0) {
        return fail(QStringLiteral("Project Browser model import smoke failed: success=%1 rows=%2 error=%3")
            .arg(imported.success)
            .arg(assetTable_ == nullptr ? -1 : assetTable_->rowCount())
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

    const auto offscreenPlatform = QApplication::platformName() == QStringLiteral("offscreen");
    const auto& stats = renderer_->stats();
    if (!offscreenPlatform && (stats.meshDrawsPresented == 0
            || stats.texturedMeshDrawsPresented == 0
            || stats.colorMeshDrawsPresented == 0
            || stats.lastFrameCandidateMeshDrawCount == 0
            || stats.lastFrameRenderCpuTimeUs == 0
            || stats.shadowFramesPresented == 0
            || stats.shadowCasterDrawsPresented == 0)) {
        appendPendingLogs();
        return fail(QStringLiteral(
            "Imported textured mesh, gizmo, culling stats, and shadow pass did not reach Vulkan: frames=%1 draws=%2 textured=%3 gizmos=%4 candidates=%5 cpuUs=%6 shadows=%7 casters=%8 logs=%9")
            .arg(static_cast<qulonglong>(stats.viewportFramesPresented))
            .arg(static_cast<qulonglong>(stats.meshDrawsPresented))
            .arg(static_cast<qulonglong>(stats.texturedMeshDrawsPresented))
            .arg(static_cast<qulonglong>(stats.colorMeshDrawsPresented))
            .arg(static_cast<qulonglong>(stats.lastFrameCandidateMeshDrawCount))
            .arg(static_cast<qulonglong>(stats.lastFrameRenderCpuTimeUs))
            .arg(static_cast<qulonglong>(stats.shadowFramesPresented))
            .arg(static_cast<qulonglong>(stats.shadowCasterDrawsPresented))
            .arg(consoleView_->toPlainText().right(1200)));
    }

    return true;
}

} // namespace projectunity::editor
