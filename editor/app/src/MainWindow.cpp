#include <projectunity/editor/MainWindow.hpp>

#include <projectunity/editor/ViewportWidget.hpp>
#include <projectunity/renderer/VulkanRenderer.hpp>

#include <DockAreaWidget.h>
#include <DockManager.h>
#include <DockWidget.h>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QAbstractItemView>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <utility>

namespace projectunity::editor {
namespace {

constexpr int kLayoutVersion = 1;
constexpr int kEntityIdRole = Qt::UserRole + 1;

[[nodiscard]] QString formatLogEntry(const core::LogEntry& entry)
{
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        entry.timestamp.time_since_epoch()).count();

    return QStringLiteral("%1 [%2] [%3] %4")
        .arg(QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(millis)).toString(QStringLiteral("HH:mm:ss.zzz")))
        .arg(QString::fromUtf8(core::toString(entry.level).data(), static_cast<int>(core::toString(entry.level).size())))
        .arg(QString::fromUtf8(core::toString(entry.category).data(), static_cast<int>(core::toString(entry.category).size())))
        .arg(QString::fromStdString(entry.message));
}

[[nodiscard]] std::filesystem::path pathFromQString(const QString& path)
{
    return std::filesystem::path(path.toStdWString());
}

[[nodiscard]] QString pathToQString(const std::filesystem::path& path)
{
    return QString::fromStdWString(path.wstring());
}

[[nodiscard]] quint64 entityIdValue(scene::EntityId id)
{
    return static_cast<quint64>(id.value());
}

[[nodiscard]] scene::EntityId entityIdFromItem(const QTreeWidgetItem* item)
{
    if (item == nullptr) {
        return {};
    }
    return scene::EntityId(item->data(0, kEntityIdRole).toULongLong());
}

[[nodiscard]] std::filesystem::path editorAssetCacheRoot()
{
    std::error_code error;
    const auto current = std::filesystem::current_path(error);
    if (!error) {
        const auto candidate = current / "Cache" / "Assets";
        std::filesystem::create_directories(candidate, error);
        if (!error) {
            return candidate;
        }
    }

    error.clear();
    const auto fallback = std::filesystem::temp_directory_path() / "projectunity_editor_cache" / "Assets";
    std::filesystem::create_directories(fallback, error);
    return fallback;
}

void setSpinBoxesEnabled(const std::array<QDoubleSpinBox*, 9>& spinBoxes, bool enabled)
{
    for (auto* spinBox : spinBoxes) {
        if (spinBox != nullptr) {
            spinBox->setEnabled(enabled);
        }
    }
}

void setTableValue(QTableWidget* table, int row, const QString& value)
{
    if (table == nullptr || row < 0 || row >= table->rowCount()) {
        return;
    }
    auto* item = table->item(row, 1);
    if (item == nullptr) {
        item = new QTableWidgetItem;
        table->setItem(row, 1, item);
    }
    item->setText(value);
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , logSink_(std::make_shared<core::MemoryLogSink>())
    , assetManager_(editorAssetCacheRoot())
{
    core::Logger::instance().addSink(logSink_);

    std::string rendererError;
    renderer::RendererConfig rendererConfig;
    rendererConfig.applicationName = "ProjectUnity Editor";
    renderer_ = renderer::createVulkanRenderer(rendererConfig, &rendererError);
    if (renderer_ != nullptr && renderer_->isReady()) {
        const auto& stats = renderer_->stats();
        core::logInfo(
            core::LogCategory::Renderer,
            QStringLiteral("Editor Vulkan renderer ready: %1 Vulkan %2.%3.%4 VMA=%5")
                .arg(QString::fromStdString(stats.gpuName))
                .arg(stats.apiVersionMajor)
                .arg(stats.apiVersionMinor)
                .arg(stats.apiVersionPatch)
                .arg(stats.vmaAllocatorReady ? QStringLiteral("yes") : QStringLiteral("no"))
                .toStdString());
    } else {
        core::logError(
            core::LogCategory::Renderer,
            QStringLiteral("Editor Vulkan renderer unavailable: %1")
                .arg(QString::fromStdString(rendererError.empty() ? "unknown error" : rendererError))
                .toStdString());
    }

    setWindowTitle(QStringLiteral("ProjectUnity Editor"));
    setDockNestingEnabled(true);

    createMenus();
    createToolbar();
    createDockLayout();
    newScene();
    lightingApplyTimer_ = new QTimer(this);
    lightingApplyTimer_->setSingleShot(true);
    connect(lightingApplyTimer_, &QTimer::timeout, this, [this]() {
        applyLightingSettings();
    });
    restoreEditorLayout();

    logFlushTimer_ = new QTimer(this);
    connect(logFlushTimer_, &QTimer::timeout, this, [this]() {
        appendPendingLogs();
        updateProfilerPanel();
    });
    logFlushTimer_->start(100);

    statusBar()->showMessage(QStringLiteral("Ready"));
    core::logInfo(core::LogCategory::Editor, "Main editor window initialized");
}

void MainWindow::updateProfilerPanel()
{
    if (profilerTable_ == nullptr) {
        return;
    }
    if (renderer_ == nullptr || !renderer_->isReady()) {
        setTableValue(profilerTable_, 0, QStringLiteral("Unavailable"));
        for (int row = 1; row < profilerTable_->rowCount(); ++row) {
            setTableValue(profilerTable_, row, QStringLiteral("-"));
        }
        return;
    }

    const auto& stats = renderer_->stats();
    setTableValue(profilerTable_, 0, QString::fromStdString(stats.gpuName));
    setTableValue(
        profilerTable_,
        1,
        QStringLiteral("%1.%2.%3")
            .arg(stats.apiVersionMajor)
            .arg(stats.apiVersionMinor)
            .arg(stats.apiVersionPatch));
    setTableValue(profilerTable_, 2, QStringLiteral("%1 us").arg(static_cast<qulonglong>(stats.lastFrameRenderCpuTimeUs)));
    setTableValue(profilerTable_, 3, QStringLiteral("%1 us").arg(static_cast<qulonglong>(stats.averageRenderCpuTimeUs)));
    setTableValue(profilerTable_, 4, QStringLiteral("%1 us").arg(static_cast<qulonglong>(stats.lastFrameResourcePrepareCpuTimeUs)));
    setTableValue(profilerTable_, 5, QStringLiteral("%1 us").arg(static_cast<qulonglong>(stats.lastFrameCommandRecordCpuTimeUs)));
    setTableValue(profilerTable_, 6, QStringLiteral("%1 us").arg(static_cast<qulonglong>(stats.lastFrameShadowRecordCpuTimeUs)));
    setTableValue(profilerTable_, 7, QStringLiteral("%1 us").arg(static_cast<qulonglong>(stats.lastFrameMeshRecordCpuTimeUs)));
    setTableValue(profilerTable_, 8, QStringLiteral("%1 us").arg(static_cast<qulonglong>(stats.lastFrameColorRecordCpuTimeUs)));
    setTableValue(profilerTable_, 9, QString::number(static_cast<qulonglong>(stats.viewportFramesPresented)));
    setTableValue(profilerTable_, 10, QString::number(static_cast<qulonglong>(stats.lastFrameCandidateMeshDrawCount)));
    setTableValue(profilerTable_, 11, QString::number(static_cast<qulonglong>(stats.lastFrameCulledMeshDrawCount)));
    setTableValue(profilerTable_, 12, QString::number(static_cast<qulonglong>(stats.lastFrameMeshDrawCount)));
    setTableValue(profilerTable_, 13, QString::number(static_cast<qulonglong>(stats.lastFrameMeshBatchCount)));
    setTableValue(
        profilerTable_,
        14,
        QStringLiteral("%1 / %2")
            .arg(static_cast<qulonglong>(stats.lastFrameCandidateTriangleCount))
            .arg(static_cast<qulonglong>(stats.lastFrameVisibleTriangleCount)));
    setTableValue(profilerTable_, 15, QString::number(static_cast<qulonglong>(stats.lastFrameCulledTriangleCount)));
    setTableValue(profilerTable_, 16, QString::number(static_cast<qulonglong>(stats.lastFrameLodMeshDrawCount)));
    setTableValue(profilerTable_, 17, QString::number(static_cast<qulonglong>(stats.lastFrameLodTriangleReductionCount)));
    setTableValue(profilerTable_, 18, QString::number(static_cast<qulonglong>(stats.meshDrawsPresented)));
    setTableValue(profilerTable_, 19, QString::number(static_cast<qulonglong>(stats.texturedMeshDrawsPresented)));
    setTableValue(profilerTable_, 20, QString::number(static_cast<qulonglong>(stats.lastFrameColorMeshDrawCount)));
    setTableValue(profilerTable_, 21, QStringLiteral("%1 bytes").arg(static_cast<qulonglong>(stats.lastFrameColorUploadBytes)));
    setTableValue(profilerTable_, 22, QStringLiteral("%1 bytes").arg(static_cast<qulonglong>(stats.totalColorUploadBytes)));
    setTableValue(
        profilerTable_,
        23,
        QStringLiteral("%1 / %2")
            .arg(static_cast<qulonglong>(stats.lastFrameMeshUploadCount))
            .arg(static_cast<qulonglong>(stats.totalMeshUploadCount)));
    setTableValue(
        profilerTable_,
        24,
        QStringLiteral("%1 / %2")
            .arg(static_cast<qulonglong>(stats.lastFrameTextureUploadCount))
            .arg(static_cast<qulonglong>(stats.totalTextureUploadCount)));
    setTableValue(profilerTable_, 25, QStringLiteral("%1 bytes").arg(static_cast<qulonglong>(stats.lastFrameStaticUploadBytes)));
    setTableValue(profilerTable_, 26, QStringLiteral("%1 bytes").arg(static_cast<qulonglong>(stats.totalStaticUploadBytes)));
    setTableValue(
        profilerTable_,
        27,
        QStringLiteral("%1 / %2")
            .arg(static_cast<qulonglong>(stats.residentMeshCount))
            .arg(static_cast<qulonglong>(stats.residentTextureCount)));
    setTableValue(profilerTable_, 28, QString::number(static_cast<qulonglong>(stats.lastFrameLightCount)));
    setTableValue(profilerTable_, 29, QString::number(static_cast<qulonglong>(stats.shadowFramesPresented)));
    setTableValue(profilerTable_, 30, QString::number(static_cast<qulonglong>(stats.lastFrameShadowViewCount)));
    setTableValue(profilerTable_, 31, QString::number(static_cast<qulonglong>(stats.lastFrameShadowBatchCount)));
    setTableValue(profilerTable_, 32, QString::number(static_cast<qulonglong>(stats.lastFrameShadowCulledBatchCount)));
    setTableValue(
        profilerTable_,
        33,
        QStringLiteral("%1 / %2")
            .arg(static_cast<qulonglong>(stats.lastFrameShadowCasterCount))
            .arg(static_cast<qulonglong>(stats.shadowCasterDrawsPresented)));
    setTableValue(profilerTable_, 34, stats.gpuTimestampsSupported ? QStringLiteral("yes") : QStringLiteral("no"));
    setTableValue(
        profilerTable_,
        35,
        stats.lastFrameGpuTimestampsValid
            ? QStringLiteral("%1 us").arg(static_cast<qulonglong>(stats.lastFrameGpuTimeUs))
            : QStringLiteral("-"));
    setTableValue(
        profilerTable_,
        36,
        stats.lastFrameGpuTimestampsValid
            ? QStringLiteral("%1 us").arg(static_cast<qulonglong>(stats.lastFrameShadowGpuTimeUs))
            : QStringLiteral("-"));
    setTableValue(
        profilerTable_,
        37,
        stats.lastFrameGpuTimestampsValid
            ? QStringLiteral("%1 us").arg(static_cast<qulonglong>(stats.lastFrameMeshGpuTimeUs))
            : QStringLiteral("-"));
    setTableValue(
        profilerTable_,
        38,
        stats.lastFrameGpuTimestampsValid
            ? QStringLiteral("%1 us").arg(static_cast<qulonglong>(stats.lastFrameColorGpuTimeUs))
            : QStringLiteral("-"));
}

MainWindow::~MainWindow()
{
    saveEditorLayout();
    appendPendingLogs();
    if (sceneViewport_ != nullptr) {
        sceneViewport_->setRenderer(nullptr);
    }
    if (gameViewport_ != nullptr) {
        gameViewport_->setRenderer(nullptr);
    }
    renderer_.reset();
    core::Logger::instance().removeSink(logSink_.get());
}

void MainWindow::restoreEditorLayout()
{
    QSettings settings;
    const auto geometry = settings.value(QStringLiteral("editor/geometry")).toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }

    const auto dockState = settings.value(QStringLiteral("editor/dockState")).toByteArray();
    if (!dockState.isEmpty() && dockManager_ != nullptr) {
        if (!dockManager_->restoreState(dockState, kLayoutVersion)) {
            core::logWarning(core::LogCategory::Editor, "Failed to restore editor dock layout");
        }
    }
    restoreLightingSettings();
}

void MainWindow::resetEditorLayout()
{
    if (dockManager_ == nullptr || defaultDockState_.isEmpty()) {
        core::logWarning(core::LogCategory::Editor, "Editor default dock layout is unavailable");
        return;
    }

    if (!dockManager_->restoreState(defaultDockState_)) {
        core::logError(core::LogCategory::Editor, "Failed to restore editor default dock layout");
        return;
    }

    saveEditorLayout();
    statusBar()->showMessage(QStringLiteral("Editor layout reset"), 3000);
    core::logInfo(core::LogCategory::Editor, "Editor dock layout reset");
}

void MainWindow::saveEditorLayout()
{
    QSettings settings;
    settings.setValue(QStringLiteral("editor/geometry"), saveGeometry());
    if (dockManager_ != nullptr) {
        settings.setValue(QStringLiteral("editor/dockState"), dockManager_->saveState(kLayoutVersion));
    }
    saveLightingSettings();
}

void MainWindow::appendPendingLogs()
{
    if (consoleView_ == nullptr || !logSink_) {
        return;
    }

    const auto entries = logSink_->drain();
    for (const auto& entry : entries) {
        consoleView_->appendPlainText(formatLogEntry(entry));
    }
}

void MainWindow::newScene()
{
    scene_.clear();
    scene_.setName("Untitled Scene");
    currentScenePath_.clear();
    selectedEntityId_ = {};
    rebuildHierarchy();
    updateInspector();
    refreshViewports();
    statusBar()->showMessage(QStringLiteral("New scene"));
    core::logInfo(core::LogCategory::Editor, "New scene created");
}

void MainWindow::openScene()
{
    const auto path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Open Scene"),
        QString(),
        QStringLiteral("ProjectUnity Scene (*.scene.json);;JSON (*.json)"));

    if (!path.isEmpty()) {
        loadSceneFromPath(path);
    }
}

bool MainWindow::saveScene()
{
    if (currentScenePath_.empty()) {
        return saveSceneAs();
    }

    return saveSceneToPath(pathToQString(currentScenePath_));
}

bool MainWindow::saveSceneAs()
{
    const auto path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Save Scene"),
        QString(),
        QStringLiteral("ProjectUnity Scene (*.scene.json);;JSON (*.json)"));

    if (path.isEmpty()) {
        return false;
    }

    return saveSceneToPath(path);
}

bool MainWindow::saveSceneToPath(const QString& path)
{
    std::string error;
    if (!scene_.saveToFile(pathFromQString(path), &error)) {
        core::logError(core::LogCategory::Editor, error);
        statusBar()->showMessage(QStringLiteral("Scene save failed"));
        return false;
    }

    currentScenePath_ = pathFromQString(path);
    statusBar()->showMessage(QStringLiteral("Scene saved"));
    core::logInfo(core::LogCategory::Editor, "Scene saved from editor");
    return true;
}

bool MainWindow::loadSceneFromPath(const QString& path)
{
    std::string error;
    if (!scene_.loadFromFile(pathFromQString(path), &error)) {
        core::logError(core::LogCategory::Editor, error);
        statusBar()->showMessage(QStringLiteral("Scene load failed"));
        return false;
    }

    currentScenePath_ = pathFromQString(path);
    selectedEntityId_ = {};
    rebuildHierarchy();
    updateInspector();
    refreshViewports();
    statusBar()->showMessage(QStringLiteral("Scene loaded"));
    core::logInfo(core::LogCategory::Editor, "Scene loaded from editor");
    return true;
}

scene::EntityId MainWindow::createEmptyEntity(const QString& name)
{
    const std::optional<scene::EntityId> parent = selectedEntityId_.isValid()
        ? std::optional<scene::EntityId>(selectedEntityId_)
        : std::nullopt;

    auto& entity = scene_.createEntity(name.toStdString(), parent);
    const auto id = entity.id;
    rebuildHierarchy();
    selectEntity(id);
    statusBar()->showMessage(QStringLiteral("Entity created"));
    return id;
}

void MainWindow::deleteSelectedEntity()
{
    if (!selectedEntityId_.isValid()) {
        return;
    }

    if (scene_.destroyEntity(selectedEntityId_)) {
        selectedEntityId_ = {};
        rebuildHierarchy();
        updateInspector();
        refreshViewports();
        statusBar()->showMessage(QStringLiteral("Entity deleted"));
    }
}

void MainWindow::duplicateSelectedEntity()
{
    if (!selectedEntityId_.isValid()) {
        return;
    }

    auto* duplicate = scene_.duplicateEntity(selectedEntityId_);
    if (duplicate == nullptr) {
        core::logWarning(core::LogCategory::Editor, "Duplicate command ignored missing entity");
        return;
    }

    const auto duplicateId = duplicate->id;
    rebuildHierarchy();
    selectEntity(duplicateId);
    statusBar()->showMessage(QStringLiteral("Entity duplicated"));
}

void MainWindow::selectEntity(scene::EntityId id)
{
    selectedEntityId_ = id;

    if (hierarchyTree_ != nullptr) {
        const auto items = hierarchyTree_->findItems(QStringLiteral("*"), Qt::MatchWildcard | Qt::MatchRecursive);
        for (auto* item : items) {
            if (entityIdFromItem(item) == id) {
                hierarchyTree_->setCurrentItem(item);
                break;
            }
        }
    }

    updateInspector();
    refreshViewports();
}

void MainWindow::clearSelection()
{
    selectedEntityId_ = {};
    if (hierarchyTree_ != nullptr) {
        hierarchyTree_->clearSelection();
    }
    updateInspector();
    refreshViewports();
}

void MainWindow::rebuildHierarchy()
{
    if (hierarchyTree_ == nullptr) {
        return;
    }

    const QSignalBlocker blocker(hierarchyTree_);
    hierarchyTree_->clear();
    for (const auto rootId : scene_.rootEntities()) {
        addEntityToHierarchy(nullptr, rootId);
    }
    hierarchyTree_->expandAll();
}

void MainWindow::addEntityToHierarchy(QTreeWidgetItem* parentItem, scene::EntityId id)
{
    const auto* entity = scene_.findEntity(id);
    if (entity == nullptr || hierarchyTree_ == nullptr) {
        return;
    }

    auto* item = parentItem == nullptr
        ? new QTreeWidgetItem(hierarchyTree_)
        : new QTreeWidgetItem(parentItem);
    item->setText(0, QString::fromStdString(entity->name));
    item->setData(0, kEntityIdRole, QVariant::fromValue(entityIdValue(entity->id)));

    for (const auto childId : entity->children) {
        addEntityToHierarchy(item, childId);
    }
}

void MainWindow::updateInspector()
{
    inspectorUpdating_ = true;

    if (sceneNameEdit_ != nullptr) {
        const QSignalBlocker blocker(sceneNameEdit_);
        sceneNameEdit_->setText(QString::fromStdString(std::string(scene_.name())));
    }

    const auto* entity = selectedEntityId_.isValid() ? scene_.findEntity(selectedEntityId_) : nullptr;
    const bool hasSelection = entity != nullptr;

    if (entityNameEdit_ != nullptr) {
        const QSignalBlocker blocker(entityNameEdit_);
        entityNameEdit_->setEnabled(hasSelection);
        entityNameEdit_->setText(hasSelection ? QString::fromStdString(entity->name) : QString());
    }

    const std::array<QDoubleSpinBox*, 9> spinBoxes {
        positionX_, positionY_, positionZ_,
        rotationX_, rotationY_, rotationZ_,
        scaleX_, scaleY_, scaleZ_,
    };
    setSpinBoxesEnabled(spinBoxes, hasSelection);

    if (hasSelection) {
        const auto& transform = entity->transform;
        const std::array<float, 9> values {
            transform.position.x, transform.position.y, transform.position.z,
            transform.rotationEuler.x, transform.rotationEuler.y, transform.rotationEuler.z,
            transform.scale.x, transform.scale.y, transform.scale.z,
        };

        for (std::size_t index = 0; index < spinBoxes.size(); ++index) {
            if (spinBoxes[index] != nullptr) {
                const QSignalBlocker blocker(spinBoxes[index]);
                spinBoxes[index]->setValue(values[index]);
            }
        }
    } else {
        for (auto* spinBox : spinBoxes) {
            if (spinBox != nullptr) {
                const QSignalBlocker blocker(spinBox);
                spinBox->setValue(0.0);
            }
        }
        if (scaleX_ != nullptr) {
            const QSignalBlocker blocker(scaleX_);
            scaleX_->setValue(1.0);
        }
        if (scaleY_ != nullptr) {
            const QSignalBlocker blocker(scaleY_);
            scaleY_->setValue(1.0);
        }
        if (scaleZ_ != nullptr) {
            const QSignalBlocker blocker(scaleZ_);
            scaleZ_->setValue(1.0);
        }
    }
    if (componentSummary_ != nullptr) {
        QStringList components;
        if (hasSelection && entity->meshRenderer.has_value()) {
            components << (entity->meshRenderer->renderable
                ? QStringLiteral("Mesh Renderer")
                : QStringLiteral("Editable Mesh Part"));
        }
        if (hasSelection && entity->light.has_value()) {
            components << QStringLiteral("Light");
        }
        if (hasSelection && entity->camera.has_value()) {
            components << QStringLiteral("Camera");
        }
        componentSummary_->setText(components.isEmpty() ? QStringLiteral("-") : components.join(QStringLiteral(", ")));
    }

    if (deleteEntityButton_ != nullptr) {
        deleteEntityButton_->setEnabled(hasSelection);
    }
    if (duplicateEntityButton_ != nullptr) {
        duplicateEntityButton_->setEnabled(hasSelection);
    }

    inspectorUpdating_ = false;
}

void MainWindow::applyInspectorToSelection()
{
    if (inspectorUpdating_) {
        return;
    }

    scene_.setName(sceneNameEdit_ == nullptr ? "Untitled Scene" : sceneNameEdit_->text().toStdString());

    auto* entity = selectedEntityId_.isValid() ? scene_.findEntity(selectedEntityId_) : nullptr;
    if (entity == nullptr) {
        rebuildHierarchy();
        return;
    }

    if (entityNameEdit_ != nullptr) {
        (void)scene_.setName(selectedEntityId_, entityNameEdit_->text().toStdString());
    }

    scene::TransformComponent transform;
    transform.position = {
        static_cast<float>(positionX_->value()),
        static_cast<float>(positionY_->value()),
        static_cast<float>(positionZ_->value()),
    };
    transform.rotationEuler = {
        static_cast<float>(rotationX_->value()),
        static_cast<float>(rotationY_->value()),
        static_cast<float>(rotationZ_->value()),
    };
    transform.scale = {
        static_cast<float>(scaleX_->value()),
        static_cast<float>(scaleY_->value()),
        static_cast<float>(scaleZ_->value()),
    };
    (void)scene_.setTransform(selectedEntityId_, transform);
    rebuildHierarchy();
    selectEntity(selectedEntityId_);
}

void MainWindow::applyLightingSettings()
{
    if (skyColorR_ == nullptr
        || skyColorG_ == nullptr
        || skyColorB_ == nullptr
        || groundColorR_ == nullptr
        || groundColorG_ == nullptr
        || groundColorB_ == nullptr
        || environmentIntensity_ == nullptr) {
        return;
    }

    environmentSettings_.skyColor = {
        static_cast<float>(skyColorR_->value()),
        static_cast<float>(skyColorG_->value()),
        static_cast<float>(skyColorB_->value()),
    };
    environmentSettings_.groundColor = {
        static_cast<float>(groundColorR_->value()),
        static_cast<float>(groundColorG_->value()),
        static_cast<float>(groundColorB_->value()),
    };
    environmentSettings_.intensity = static_cast<float>(environmentIntensity_->value());
    pushLightingSettingsToViewports();
    saveLightingSettings();
}

void MainWindow::scheduleLightingSettingsApply()
{
    if (lightingApplyTimer_ == nullptr) {
        applyLightingSettings();
        return;
    }
    lightingApplyTimer_->start(180);
}

void MainWindow::pushLightingSettingsToViewports()
{
    environmentSettings_.sourceTexture = environmentTexture_.get();
    if (sceneViewport_ != nullptr) {
        sceneViewport_->setEnvironmentSettings(environmentSettings_);
    }
    if (gameViewport_ != nullptr) {
        gameViewport_->setEnvironmentSettings(environmentSettings_);
    }
}

void MainWindow::refreshViewports()
{
    pushLightingSettingsToViewports();
    if (sceneViewport_ != nullptr) {
        sceneViewport_->setScene(&scene_);
        sceneViewport_->setSelectedEntity(selectedEntityId_);
    }
    if (gameViewport_ != nullptr) {
        gameViewport_->setScene(&scene_);
        gameViewport_->setSelectedEntity(selectedEntityId_);
    }
}

} // namespace projectunity::editor
