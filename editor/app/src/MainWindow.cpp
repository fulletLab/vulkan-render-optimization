#include <projectunity/editor/MainWindow.hpp>

#include <projectunity/editor/SceneHierarchyWidget.hpp>
#include <projectunity/editor/ViewportWidget.hpp>
#include <projectunity/renderer/VulkanRenderer.hpp>

#include <DockAreaWidget.h>
#include <DockManager.h>
#include <DockWidget.h>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
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
#include <cmath>
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

[[nodiscard]] int colliderShapeIndex(scene::ColliderShape shape) noexcept
{
    switch (shape) {
    case scene::ColliderShape::Box: return 0;
    case scene::ColliderShape::Sphere: return 1;
    case scene::ColliderShape::Capsule: return 2;
    case scene::ColliderShape::Mesh: return 3;
    case scene::ColliderShape::Terrain: return 4;
    }
    return 0;
}

[[nodiscard]] scene::ColliderShape colliderShapeFromIndex(int index) noexcept
{
    switch (index) {
    case 1: return scene::ColliderShape::Sphere;
    case 2: return scene::ColliderShape::Capsule;
    case 3: return scene::ColliderShape::Mesh;
    case 4: return scene::ColliderShape::Terrain;
    default: return scene::ColliderShape::Box;
    }
}

[[nodiscard]] renderer::RenderLight editorSunFromAngles(
    float azimuthDegrees,
    float elevationDegrees,
    std::array<float, 3> color,
    float intensity)
{
    constexpr float kDegreesToRadians = 0.01745329251994329577F;
    const auto azimuth = azimuthDegrees * kDegreesToRadians;
    const auto elevation = elevationDegrees * kDegreesToRadians;
    const auto horizontal = std::cos(elevation);
    renderer::RenderLight light;
    light.type = renderer::RenderLightType::Directional;
    light.direction = {
        std::sin(azimuth) * horizontal,
        -std::sin(elevation),
        std::cos(azimuth) * horizontal,
    };
    light.color = color;
    light.intensity = intensity;
    return light;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , logSink_(std::make_shared<core::MemoryLogSink>())
    , assetManager_(editorAssetCacheRoot())
{
    core::Logger::instance().addSink(logSink_);
    scripting::registerBuiltInScripts(scriptRegistry_);
    scriptRuntime_.setRegistry(&scriptRegistry_);

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
    ensureBuiltInGeneratedModels();
    rebuildAssetBrowser();
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
        if (profilerTable_ != nullptr) {
            updateProfilerPanel();
        }
    });
    logFlushTimer_->start(250);

    performanceStatus_ = new QLabel(QStringLiteral("FPS -"));
    performanceStatus_->setObjectName(QStringLiteral("PerformanceStatus"));
    performanceStatus_->setMinimumWidth(920);
    performanceStatus_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    statusBar()->addPermanentWidget(performanceStatus_, 1);
    statusBar()->showMessage(QStringLiteral("Ready"));
    core::logInfo(core::LogCategory::Editor, "Main editor window initialized");
}

MainWindow::~MainWindow()
{
    stopPlayMode();
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
    stopPlayMode();
    scene_.clear();
    scene_.setName("Untitled Scene");
    currentScenePath_.clear();
    selectedEntityId_ = {};
    rebuildHierarchy();
    updateInspector();
    syncTerrainPanelFromSelection();
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
    stopPlayMode();
    std::string error;
    if (!scene_.loadFromFile(pathFromQString(path), &error)) {
        core::logError(core::LogCategory::Editor, error);
        statusBar()->showMessage(QStringLiteral("Scene load failed"));
        return false;
    }

    currentScenePath_ = pathFromQString(path);
    selectedEntityId_ = {};
    rebuildGeneratedSceneAssets();
    rebuildHierarchy();
    updateInspector();
    syncTerrainPanelFromSelection();
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

    if (auto* hierarchy = dynamic_cast<SceneHierarchyWidget*>(hierarchyTree_)) {
        (void)hierarchy->selectSceneEntity(id);
    } else if (hierarchyTree_ != nullptr) {
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

    if (auto* hierarchy = dynamic_cast<SceneHierarchyWidget*>(hierarchyTree_)) {
        hierarchy->setScene(&scene_);
        hierarchy->setAssetManager(&assetManager_);
        hierarchy->rebuild(selectedEntityId_);
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
        if (hasSelection) {
            for (const auto& script : entity->scripts) {
                components << QStringLiteral("Script: %1").arg(QString::fromStdString(script.scriptName));
            }
        }
        if (hasSelection && entity->terrain.has_value()) {
            components << QStringLiteral("Terrain");
        }
        if (hasSelection && entity->rigidbody.has_value()) {
            components << QStringLiteral("Rigidbody (basic terrain contact)");
        }
        if (hasSelection && entity->collider.has_value()) {
            components << QStringLiteral("Collider (basic terrain contact)");
        }
        componentSummary_->setText(components.isEmpty() ? QStringLiteral("-") : components.join(QStringLiteral(", ")));
    }
    const scene::ScriptComponent* inspectedScript = nullptr;
    if (hasSelection && !entity->scripts.empty()) {
        inspectedScript = scene::findScript(*entity, inspectedScriptInstanceId_);
        if (inspectedScript == nullptr) {
            inspectedScriptInstanceId_ = entity->scripts.front().instanceId;
            inspectedScript = &entity->scripts.front();
        }
    } else {
        inspectedScriptInstanceId_ = {};
    }
    if (scriptComponentCombo_ != nullptr) {
        const QSignalBlocker blocker(scriptComponentCombo_);
        scriptComponentCombo_->clear();
        if (hasSelection) {
            for (const auto& script : entity->scripts) {
                scriptComponentCombo_->addItem(
                    QString::fromStdString(script.scriptName),
                    QVariant::fromValue(script.instanceId.value()));
            }
        }
        const auto index = inspectedScript == nullptr ? -1 : scriptComponentCombo_->findData(
            QVariant::fromValue(inspectedScript->instanceId.value()));
        scriptComponentCombo_->setCurrentIndex(index);
        scriptComponentCombo_->setEnabled(inspectedScript != nullptr);
    }
    if (scriptAssetCombo_ != nullptr) {
        const QSignalBlocker blocker(scriptAssetCombo_);
        scriptAssetCombo_->clear();
        for (const auto& className : scriptRegistry_.classNames()) {
            const auto* descriptor = scriptRegistry_.find(className);
            if (descriptor != nullptr) {
                scriptAssetCombo_->addItem(
                    QString::fromStdString(descriptor->className),
                    QString::fromStdString(descriptor->assetPath));
            }
        }
        const auto index = inspectedScript == nullptr ? -1 : scriptAssetCombo_->findData(
            QString::fromStdString(inspectedScript->scriptAsset));
        scriptAssetCombo_->setCurrentIndex(index);
        scriptAssetCombo_->setEnabled(inspectedScript != nullptr);
    }
    if (scriptEnabledCheck_ != nullptr) {
        const QSignalBlocker blocker(scriptEnabledCheck_);
        scriptEnabledCheck_->setEnabled(inspectedScript != nullptr);
        scriptEnabledCheck_->setChecked(inspectedScript != nullptr && inspectedScript->enabled);
    }
    if (removeScriptButton_ != nullptr) {
        removeScriptButton_->setEnabled(inspectedScript != nullptr);
    }
    if (scriptStatusLabel_ != nullptr) {
        scriptStatusLabel_->clear();
        if (inspectedScript != nullptr && scriptRegistry_.find(inspectedScript->scriptName) == nullptr) {
            scriptStatusLabel_->setText(QStringLiteral("Script asset found but class is not registered."));
        }
    }
    if (scriptFieldsTable_ != nullptr) {
        const QSignalBlocker blocker(scriptFieldsTable_);
        scriptFieldsTable_->setEnabled(inspectedScript != nullptr);
        const auto* descriptor = inspectedScript == nullptr ? nullptr : scriptRegistry_.find(inspectedScript->scriptName);
        const auto rowCount = descriptor != nullptr
            ? descriptor->fields.size()
            : (inspectedScript == nullptr ? 0U : inspectedScript->fields.size());
        scriptFieldsTable_->setRowCount(static_cast<int>(rowCount));
        for (std::size_t row = 0; row < rowCount; ++row) {
            const auto name = descriptor != nullptr ? descriptor->fields[row].name : inspectedScript->fields[row].name;
            const auto fallback = descriptor != nullptr ? descriptor->fields[row].defaultValue : inspectedScript->fields[row].value;
            const auto value = scene::scriptFieldValue(*inspectedScript, name, fallback);
            auto* nameItem = new QTableWidgetItem(QString::fromStdString(name));
            nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
            scriptFieldsTable_->setItem(static_cast<int>(row), 0, nameItem);
            scriptFieldsTable_->setItem(static_cast<int>(row), 1, new QTableWidgetItem(QString::number(value, 'g', 7)));
        }
    }
    const auto* rigidbody = hasSelection && entity->rigidbody.has_value() ? &*entity->rigidbody : nullptr;
    if (rigidbodyEnabledCheck_ != nullptr) {
        const QSignalBlocker blocker(rigidbodyEnabledCheck_);
        rigidbodyEnabledCheck_->setEnabled(rigidbody != nullptr);
        rigidbodyEnabledCheck_->setChecked(rigidbody != nullptr && rigidbody->enabled);
    }
    if (rigidbodyBodyTypeCombo_ != nullptr) {
        const QSignalBlocker blocker(rigidbodyBodyTypeCombo_);
        rigidbodyBodyTypeCombo_->setEnabled(rigidbody != nullptr);
        const auto index = rigidbody == nullptr ? 0 : (rigidbody->kinematic ? 1 : (!rigidbody->enabled ? 2 : 0));
        rigidbodyBodyTypeCombo_->setCurrentIndex(index);
    }
    if (rigidbodyMass_ != nullptr) {
        const QSignalBlocker blocker(rigidbodyMass_);
        rigidbodyMass_->setEnabled(rigidbody != nullptr);
        rigidbodyMass_->setValue(rigidbody == nullptr ? 1.0 : rigidbody->mass);
    }
    if (rigidbodyGravityCheck_ != nullptr) {
        const QSignalBlocker blocker(rigidbodyGravityCheck_);
        rigidbodyGravityCheck_->setEnabled(rigidbody != nullptr);
        rigidbodyGravityCheck_->setChecked(rigidbody != nullptr && rigidbody->useGravity);
    }
    if (rigidbodyLinearDrag_ != nullptr) {
        const QSignalBlocker blocker(rigidbodyLinearDrag_);
        rigidbodyLinearDrag_->setEnabled(rigidbody != nullptr);
        rigidbodyLinearDrag_->setValue(rigidbody == nullptr ? 0.0 : rigidbody->linearDrag);
    }
    if (rigidbodyAngularDrag_ != nullptr) {
        const QSignalBlocker blocker(rigidbodyAngularDrag_);
        rigidbodyAngularDrag_->setEnabled(rigidbody != nullptr);
        rigidbodyAngularDrag_->setValue(rigidbody == nullptr ? 0.05 : rigidbody->angularDrag);
    }

    const auto* collider = hasSelection && entity->collider.has_value() ? &*entity->collider : nullptr;
    if (colliderEnabledCheck_ != nullptr) {
        const QSignalBlocker blocker(colliderEnabledCheck_);
        colliderEnabledCheck_->setEnabled(collider != nullptr);
        colliderEnabledCheck_->setChecked(collider != nullptr && collider->enabled);
    }
    if (colliderShapeCombo_ != nullptr) {
        const QSignalBlocker blocker(colliderShapeCombo_);
        colliderShapeCombo_->setEnabled(collider != nullptr);
        colliderShapeCombo_->setCurrentIndex(collider == nullptr ? 0 : colliderShapeIndex(collider->shape));
    }
    const auto setVec3 = [](QDoubleSpinBox* x, QDoubleSpinBox* y, QDoubleSpinBox* z, math::Vec3 value, bool enabled) {
        for (auto* box : {x, y, z}) {
            if (box != nullptr) { box->setEnabled(enabled); }
        }
        if (x != nullptr) { const QSignalBlocker blocker(x); x->setValue(value.x); }
        if (y != nullptr) { const QSignalBlocker blocker(y); y->setValue(value.y); }
        if (z != nullptr) { const QSignalBlocker blocker(z); z->setValue(value.z); }
    };
    setVec3(colliderSizeX_, colliderSizeY_, colliderSizeZ_, collider == nullptr ? math::Vec3 {1.0F, 1.0F, 1.0F} : collider->size, collider != nullptr);
    setVec3(colliderOffsetX_, colliderOffsetY_, colliderOffsetZ_, collider == nullptr ? math::Vec3 {} : collider->offset, collider != nullptr);
    if (colliderRadius_ != nullptr) {
        const QSignalBlocker blocker(colliderRadius_);
        colliderRadius_->setEnabled(collider != nullptr);
        colliderRadius_->setValue(collider == nullptr ? 0.5 : collider->radius);
    }
    if (colliderHeight_ != nullptr) {
        const QSignalBlocker blocker(colliderHeight_);
        colliderHeight_->setEnabled(collider != nullptr);
        colliderHeight_->setValue(collider == nullptr ? 2.0 : collider->height);
    }
    if (colliderTriggerCheck_ != nullptr) {
        const QSignalBlocker blocker(colliderTriggerCheck_);
        colliderTriggerCheck_->setEnabled(collider != nullptr);
        colliderTriggerCheck_->setChecked(collider != nullptr && collider->trigger);
    }

    if (deleteEntityButton_ != nullptr) {
        deleteEntityButton_->setEnabled(hasSelection);
    }
    if (duplicateEntityButton_ != nullptr) {
        duplicateEntityButton_->setEnabled(hasSelection);
    }
    if (addComponentButton_ != nullptr) {
        addComponentButton_->setEnabled(hasSelection);
    }
    syncTerrainPanelFromSelection();

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

    bool hierarchyChanged = false;
    if (entityNameEdit_ != nullptr) {
        const auto nextName = entityNameEdit_->text().toStdString();
        if (nextName != entity->name && scene_.setName(selectedEntityId_, nextName)) {
            hierarchyChanged = true;
            entity = scene_.findEntity(selectedEntityId_);
            if (entity == nullptr) {
                rebuildHierarchy();
                return;
            }
        }
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
    if (!math::nearlyEqual(entity->transform.position, transform.position)
        || !math::nearlyEqual(entity->transform.rotationEuler, transform.rotationEuler)
        || !math::nearlyEqual(entity->transform.scale, transform.scale)) {
        (void)scene_.setTransform(selectedEntityId_, transform);
    }
    auto* inspectedScript = scene::findScript(*entity, inspectedScriptInstanceId_);
    if (inspectedScript != nullptr && scriptFieldsTable_ != nullptr) {
        auto script = *inspectedScript;
        scriptRegistry_.applyDefaults(script);
        if (scriptEnabledCheck_ != nullptr) {
            script.enabled = scriptEnabledCheck_->isChecked();
        }
        for (int row = 0; row < scriptFieldsTable_->rowCount(); ++row) {
            const auto* nameItem = scriptFieldsTable_->item(row, 0);
            const auto* valueItem = scriptFieldsTable_->item(row, 1);
            if (nameItem == nullptr || valueItem == nullptr) {
                continue;
            }
            bool ok = false;
            const auto value = valueItem->text().toFloat(&ok);
            if (ok) {
                scene::setScriptFieldValue(script, nameItem->text().toStdString(), value);
            }
        }
        (void)scene_.updateScript(selectedEntityId_, std::move(script));
    }
    if (entity->rigidbody.has_value()) {
        auto rigidbody = *entity->rigidbody;
        if (rigidbodyEnabledCheck_ != nullptr) {
            rigidbody.enabled = rigidbodyEnabledCheck_->isChecked();
        }
        if (rigidbodyBodyTypeCombo_ != nullptr) {
            rigidbody.kinematic = rigidbodyBodyTypeCombo_->currentIndex() == 1;
            if (rigidbodyBodyTypeCombo_->currentIndex() == 2) {
                rigidbody.enabled = false;
                rigidbody.kinematic = true;
            }
        }
        if (rigidbodyMass_ != nullptr) {
            rigidbody.mass = static_cast<float>(rigidbodyMass_->value());
        }
        if (rigidbodyGravityCheck_ != nullptr) {
            rigidbody.useGravity = rigidbodyGravityCheck_->isChecked();
        }
        if (rigidbodyLinearDrag_ != nullptr) {
            rigidbody.linearDrag = static_cast<float>(rigidbodyLinearDrag_->value());
        }
        if (rigidbodyAngularDrag_ != nullptr) {
            rigidbody.angularDrag = static_cast<float>(rigidbodyAngularDrag_->value());
        }
        (void)scene_.setRigidbody(selectedEntityId_, rigidbody);
    }
    if (entity->collider.has_value()) {
        auto collider = *entity->collider;
        if (colliderEnabledCheck_ != nullptr) {
            collider.enabled = colliderEnabledCheck_->isChecked();
        }
        if (colliderShapeCombo_ != nullptr) {
            collider.shape = colliderShapeFromIndex(colliderShapeCombo_->currentIndex());
        }
        if (colliderSizeX_ != nullptr) {
            collider.size = {
                static_cast<float>(colliderSizeX_->value()),
                static_cast<float>(colliderSizeY_->value()),
                static_cast<float>(colliderSizeZ_->value()),
            };
        }
        if (colliderOffsetX_ != nullptr) {
            collider.offset = {
                static_cast<float>(colliderOffsetX_->value()),
                static_cast<float>(colliderOffsetY_->value()),
                static_cast<float>(colliderOffsetZ_->value()),
            };
        }
        if (colliderRadius_ != nullptr) {
            collider.radius = static_cast<float>(colliderRadius_->value());
        }
        if (colliderHeight_ != nullptr) {
            collider.height = static_cast<float>(colliderHeight_->value());
        }
        if (colliderTriggerCheck_ != nullptr) {
            collider.trigger = colliderTriggerCheck_->isChecked();
        }
        (void)scene_.setCollider(selectedEntityId_, collider);
    }
    if (hierarchyChanged) {
        rebuildHierarchy();
    }
    refreshViewports();
}

void MainWindow::applyLightingSettings()
{
    if (skyColorR_ == nullptr
        || skyColorG_ == nullptr
        || skyColorB_ == nullptr
        || groundColorR_ == nullptr
        || groundColorG_ == nullptr
        || groundColorB_ == nullptr
        || environmentIntensity_ == nullptr
        || sunAzimuth_ == nullptr
        || sunElevation_ == nullptr
        || sunColorR_ == nullptr
        || sunColorG_ == nullptr
        || sunColorB_ == nullptr
        || sunIntensity_ == nullptr) {
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
    updateEditorSunFromControls();
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
        sceneViewport_->setEditorSunLight(editorSunLight_);
        sceneViewport_->setShadowUpdateMode(shadowUpdateMode_);
    }
    if (gameViewport_ != nullptr) {
        gameViewport_->setEnvironmentSettings(environmentSettings_);
        gameViewport_->setEditorSunLight(editorSunLight_);
        gameViewport_->setShadowUpdateMode(shadowUpdateMode_);
    }
    if (playRuntimeViewport_ != nullptr) {
        playRuntimeViewport_->setEnvironmentSettings(environmentSettings_);
        playRuntimeViewport_->setEditorSunLight(editorSunLight_);
        playRuntimeViewport_->setShadowUpdateMode(shadowUpdateMode_);
    }
}

void MainWindow::updateEditorSunFromControls()
{
    if (sunAzimuth_ != nullptr) {
        editorSunAzimuthDegrees_ = static_cast<float>(sunAzimuth_->value());
    }
    if (sunElevation_ != nullptr) {
        editorSunElevationDegrees_ = static_cast<float>(sunElevation_->value());
    }
    const std::array<float, 3> color {
        sunColorR_ == nullptr ? editorSunLight_.color[0] : static_cast<float>(sunColorR_->value()),
        sunColorG_ == nullptr ? editorSunLight_.color[1] : static_cast<float>(sunColorG_->value()),
        sunColorB_ == nullptr ? editorSunLight_.color[2] : static_cast<float>(sunColorB_->value()),
    };
    const auto intensity = sunIntensity_ == nullptr
        ? editorSunLight_.intensity
        : static_cast<float>(sunIntensity_->value());
    editorSunLight_ = editorSunFromAngles(editorSunAzimuthDegrees_, editorSunElevationDegrees_, color, intensity);
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
        gameViewport_->setGameCameraEntity({});
        gameViewport_->setGameScriptRuntime(nullptr);
        gameViewport_->setGameInputEnabled(false);
        gameViewport_->setGameRuntimeSnapshotEnabled(false);
    }
    if (playRuntimeViewport_ != nullptr) {
        playRuntimeViewport_->setScene(&playRuntimeScene_);
        playRuntimeViewport_->setSelectedEntity({});
        playRuntimeViewport_->setGameCameraEntity(playRuntimeCameraEntityId_);
        playRuntimeViewport_->setGameScriptRuntime(playModeActive_ ? &scriptRuntime_ : nullptr);
        playRuntimeViewport_->setGameInputEnabled(playModeActive_);
        playRuntimeViewport_->setGameRuntimeSnapshotEnabled(playModeActive_);
    }
}

} // namespace projectunity::editor
