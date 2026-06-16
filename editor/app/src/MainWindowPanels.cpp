#include <projectunity/editor/MainWindow.hpp>

#include <projectunity/editor/ProjectBrowserWidget.hpp>
#include <projectunity/editor/SceneHierarchyWidget.hpp>
#include <projectunity/editor/ViewportWidget.hpp>

#include <DockAreaWidget.h>
#include <DockManager.h>
#include <DockWidget.h>

#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPoint>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <array>
#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>

namespace projectunity::editor {
namespace {

[[nodiscard]] QPushButton* makeToolButton(const QString& text)
{
    auto* button = new QPushButton(text);
    button->setMinimumHeight(28);
    return button;
}

} // namespace

void MainWindow::createMenus()
{
    auto* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    fileMenu->addAction(QStringLiteral("New Scene"), this, [this] {
        newScene();
    });
    fileMenu->addAction(QStringLiteral("Open Scene"), this, [this] {
        openScene();
    });
    fileMenu->addAction(QStringLiteral("Save Scene"), this, [this] {
        saveScene();
    });
    fileMenu->addAction(QStringLiteral("Save Scene As"), this, [this] {
        saveSceneAs();
    });
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("Build Settings"), this, [] {
        core::logInfo(core::LogCategory::Editor, "Build Settings is not implemented yet");
    });
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("Exit"), qApp, &QApplication::quit);

    auto* editMenu = menuBar()->addMenu(QStringLiteral("&Edit"));
    editMenu->addAction(QStringLiteral("Undo"));
    editMenu->addAction(QStringLiteral("Redo"));
    editMenu->addSeparator();
    editMenu->addAction(QStringLiteral("Delete"), this, [this] { deleteSelectedEntity(); });
    editMenu->addAction(QStringLiteral("Duplicate"), this, [this] { duplicateSelectedEntity(); });
    editMenu->addSeparator();
    editMenu->addAction(QStringLiteral("Preferences"));

    auto* assetsMenu = menuBar()->addMenu(QStringLiteral("&Assets"));
    assetsMenu->addAction(QStringLiteral("Import Asset"), this, [this] { importAsset(); });
    assetsMenu->addAction(QStringLiteral("Create Material"));
    assetsMenu->addAction(QStringLiteral("Create Texture Placeholder"));
    assetsMenu->addAction(QStringLiteral("Create Script"), this, [this] { ensureFlyPlayerScriptAsset(); });
    assetsMenu->addSeparator();
    assetsMenu->addAction(QStringLiteral("Refresh"), this, [this] { rebuildAssetBrowser(); });
    assetsMenu->addAction(QStringLiteral("Reimport Selected"));

    auto* gameObjectMenu = menuBar()->addMenu(QStringLiteral("&GameObject"));
    gameObjectMenu->addAction(QStringLiteral("Create Empty"), this, [this] {
        createEmptyEntity(QStringLiteral("GameObject"));
    });
    auto* object3dMenu = gameObjectMenu->addMenu(QStringLiteral("3D Object"));
    object3dMenu->addAction(QStringLiteral("Cube"), this, [this] { createCubeEntity(); });
    object3dMenu->addAction(QStringLiteral("Sphere"), this, [this] { createSphereEntity(); });
    object3dMenu->addAction(QStringLiteral("Plane"), this, [this] { createPlaneEntity(); });
    object3dMenu->addAction(QStringLiteral("Terrain"), this, [this] { createTerrainEntity(); });
    auto* lightMenu = gameObjectMenu->addMenu(QStringLiteral("Light"));
    lightMenu->addAction(QStringLiteral("Directional Light"), this, [this] {
        createLightEntity(scene::LightComponentType::Directional);
    });
    lightMenu->addAction(QStringLiteral("Point Light"), this, [this] {
        createLightEntity(scene::LightComponentType::Point);
    });
    lightMenu->addAction(QStringLiteral("Spot Light"), this, [this] {
        createLightEntity(scene::LightComponentType::Spot);
    });
    gameObjectMenu->addAction(QStringLiteral("Camera"), this, [this] { createCameraEntity(); });
    gameObjectMenu->addAction(QStringLiteral("Player"), this, [this] {
        createPlayerEntity();
    });
    gameObjectMenu->addSeparator();
    gameObjectMenu->addAction(QStringLiteral("Duplicate"), this, [this] {
        duplicateSelectedEntity();
    });
    gameObjectMenu->addAction(QStringLiteral("Delete"), this, [this] {
        deleteSelectedEntity();
    });

    auto* componentMenu = menuBar()->addMenu(QStringLiteral("&Component"));
    componentMenu->addAction(QStringLiteral("Add Component"), this, [this] {
        showAddComponentMenu(nullptr);
    });

    windowMenu_ = menuBar()->addMenu(QStringLiteral("&Window"));
    windowMenu_->addAction(QStringLiteral("Reset Layout"), this, [this] {
        resetEditorLayout();
    });
    windowMenu_->addSeparator();

    auto* toolsMenu = menuBar()->addMenu(QStringLiteral("&Tools"));
    toolsMenu->addAction(QStringLiteral("Terrain Generator"), this, [this] {
        createTerrainEntity();
    });
    toolsMenu->addAction(QStringLiteral("Terrain Brush (PARCIAL)"));
    toolsMenu->addAction(QStringLiteral("Lighting/Bake"));
    toolsMenu->addAction(QStringLiteral("NavMesh Bake"));
    toolsMenu->addAction(QStringLiteral("Physics Debug"));

    auto* helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
    helpMenu->addAction(QStringLiteral("About"), this, [] {
        core::logInfo(core::LogCategory::Editor, "ProjectUnity Editor");
    });
    helpMenu->addAction(QStringLiteral("Documentation"));
}

void MainWindow::createToolbar()
{
    auto* toolbar = addToolBar(QStringLiteral("Toolbar"));
    toolbar->setObjectName(QStringLiteral("MainToolbar"));
    toolbar->setMovable(false); auto* transformGroup = new QActionGroup(this);
    transformGroup->setExclusive(true);
    const std::array<std::pair<QString, ViewportTool>, 4> tools {{
        {QStringLiteral("Hand"), ViewportTool::Hand},
        {QStringLiteral("Move"), ViewportTool::Move},
        {QStringLiteral("Rotate"), ViewportTool::Rotate},
        {QStringLiteral("Scale"), ViewportTool::Scale},
    }};
    for (const auto& [toolName, tool] : tools) {
        auto* action = toolbar->addAction(toolName);
        action->setCheckable(true);
        transformGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, toolName, tool] {
            if (sceneViewport_ != nullptr) {
                sceneViewport_->setTool(tool);
            }
            core::logInfo(core::LogCategory::Editor, QStringLiteral("Tool selected: %1").arg(toolName).toStdString());
        });
    }
    transformGroup->actions().at(1)->setChecked(true);
    toolbar->addSeparator();
    auto* spaceGroup = new QActionGroup(this);
    spaceGroup->setExclusive(true);
    auto* localAction = toolbar->addAction(QStringLiteral("Local"));
    auto* globalAction = toolbar->addAction(QStringLiteral("Global"));
    localAction->setCheckable(true);
    globalAction->setCheckable(true);
    localAction->setChecked(true);
    spaceGroup->addAction(localAction);
    spaceGroup->addAction(globalAction);
    connect(localAction, &QAction::triggered, this, [this] {
        if (sceneViewport_ != nullptr) {
            sceneViewport_->setTransformSpace(TransformSpace::Local);
        }
    });
    connect(globalAction, &QAction::triggered, this, [this] {
        if (sceneViewport_ != nullptr) {
            sceneViewport_->setTransformSpace(TransformSpace::Global);
        }
    });
    toolbar->addSeparator();
    auto* debugMenu = new QMenu(QStringLiteral("Debug"), toolbar);
    auto* wireAction = debugMenu->addAction(QStringLiteral("Wire"));
    wireAction->setCheckable(true);
    connect(wireAction, &QAction::triggered, this, [this](bool enabled) {
        if (sceneViewport_ != nullptr) {
            sceneViewport_->setMeshWireOverlayEnabled(enabled);
        }
    });
    auto* xrayAction = debugMenu->addAction(QStringLiteral("XRay"));
    xrayAction->setCheckable(true);
    connect(xrayAction, &QAction::triggered, this, [this](bool enabled) {
        if (sceneViewport_ != nullptr) {
            sceneViewport_->setAssetXrayDebugEnabled(enabled);
        }
    });
    auto* lodDebugAction = debugMenu->addAction(QStringLiteral("LOD / Distance"));
    lodDebugAction->setCheckable(true);
    connect(lodDebugAction, &QAction::triggered, this, [this](bool enabled) {
        if (sceneViewport_ != nullptr) {
            sceneViewport_->setLodDebugOverlayEnabled(enabled);
        }
        if (gameViewport_ != nullptr) {
            gameViewport_->setLodDebugOverlayEnabled(enabled);
        }
    });
    auto* shadowDebugAction = debugMenu->addAction(QStringLiteral("Shadow Marks"));
    shadowDebugAction->setCheckable(true);
    connect(shadowDebugAction, &QAction::triggered, this, [this](bool enabled) {
        if (sceneViewport_ != nullptr) {
            sceneViewport_->setShadowDebugOverlayEnabled(enabled);
        }
        if (gameViewport_ != nullptr) {
            gameViewport_->setShadowDebugOverlayEnabled(enabled);
        }
    });
    auto* sourceDebugAction = debugMenu->addAction(QStringLiteral("Source Objects"));
    sourceDebugAction->setCheckable(true);
    connect(sourceDebugAction, &QAction::triggered, this, [this](bool enabled) {
        if (sceneViewport_ != nullptr) {
            sceneViewport_->setSourceObjectDebugOverlayEnabled(enabled);
        }
        if (gameViewport_ != nullptr) {
            gameViewport_->setSourceObjectDebugOverlayEnabled(enabled);
        }
    });
    auto* sunDebugAction = debugMenu->addAction(QStringLiteral("Sun Direction"));
    sunDebugAction->setCheckable(true);
    connect(sunDebugAction, &QAction::triggered, this, [this](bool enabled) {
        if (sceneViewport_ != nullptr) {
            sceneViewport_->setSunDirectionDebugEnabled(enabled);
        }
        if (gameViewport_ != nullptr) {
            gameViewport_->setSunDirectionDebugEnabled(enabled);
        }
    });
    auto* debugButton = new QToolButton(toolbar);
    debugButton->setText(QStringLiteral("Debug"));
    debugButton->setPopupMode(QToolButton::InstantPopup);
    debugButton->setMenu(debugMenu);
    toolbar->addWidget(debugButton);
    toolbar->addSeparator();
    toolbar->addAction(QStringLiteral("Play"), this, [this] {
        startPlayMode();
    });
    toolbar->addAction(QStringLiteral("Pause"), this, [this] {
        stopPlayMode();
    });
    toolbar->addAction(QStringLiteral("Step"), this, [] {
        core::logInfo(core::LogCategory::Editor, "Step command invoked");
    });
}

void MainWindow::createDockLayout()
{
    ads::CDockManager::setConfigFlags(ads::CDockManager::DefaultOpaqueConfig);
    dockManager_ = new ads::CDockManager(this);
    dockManager_->setStyleSheet(qApp->styleSheet());

    auto* hierarchyDock = createDockWidget(QStringLiteral("Hierarchy"), createHierarchyPanel());
    auto* inspectorDock = createDockWidget(QStringLiteral("Inspector"), createInspectorPanel());
    auto* sceneDock = createDockWidget(QStringLiteral("Scene View"), createSceneViewPanel());
    auto* gameDock = createDockWidget(QStringLiteral("Game View"), createGameViewPanel());
    gameDock_ = gameDock;
    auto* projectDock = createDockWidget(QStringLiteral("Project / Assets / Packages"), createProjectPanel());
    auto* bottomDock = createDockWidget(QStringLiteral("Console / Profiler / Network"), createBottomPanel());
    auto* importDock = createDockWidget(QStringLiteral("Asset Import"), createAssetImportPanel());
    auto* terrainDock = createDockWidget(QStringLiteral("Terrain"), createTerrainPanel());
    auto* lightingDock = createDockWidget(QStringLiteral("Lighting / Bake"), createLightingPanel());
    auto* physicsDock = createDockWidget(QStringLiteral("Physics Debug"), createTextPanel(
        QStringLiteral("Physics Debug"),
        {QStringLiteral("Bodies"), QStringLiteral("Colliders"), QStringLiteral("Queries")}));
    auto* navigationDock = createDockWidget(QStringLiteral("Navigation Debug"), createTextPanel(
        QStringLiteral("Navigation Debug"),
        {QStringLiteral("Bake settings"), QStringLiteral("Navmesh"), QStringLiteral("Paths")}));
    auto* serverDock = createDockWidget(QStringLiteral("Server / Session"), createTextPanel(
        QStringLiteral("Server / Session"),
        {QStringLiteral("Mode"), QStringLiteral("Sessions"), QStringLiteral("Security events")}));

    auto* centerArea = dockManager_->setCentralWidget(sceneDock);
    dockManager_->addDockWidget(ads::CenterDockWidgetArea, gameDock, centerArea);
    dockManager_->addDockWidget(ads::LeftDockWidgetArea, hierarchyDock, centerArea);
    auto* rightArea = dockManager_->addDockWidget(ads::RightDockWidgetArea, inspectorDock, centerArea);
    auto* bottomArea = dockManager_->addDockWidget(ads::BottomDockWidgetArea, projectDock, centerArea);
    dockManager_->addDockWidget(ads::CenterDockWidgetArea, bottomDock, bottomArea);

    dockManager_->addDockWidget(ads::CenterDockWidgetArea, importDock, bottomArea);
    dockManager_->addDockWidget(ads::CenterDockWidgetArea, terrainDock, rightArea);
    dockManager_->addDockWidget(ads::CenterDockWidgetArea, lightingDock, rightArea);
    dockManager_->addDockWidget(ads::CenterDockWidgetArea, physicsDock, rightArea);
    dockManager_->addDockWidget(ads::CenterDockWidgetArea, navigationDock, rightArea);
    dockManager_->addDockWidget(ads::CenterDockWidgetArea, serverDock, rightArea);
    defaultDockState_ = dockManager_->saveState();
}

ads::CDockWidget* MainWindow::createDockWidget(const QString& title, QWidget* content)
{
    auto* dock = new ads::CDockWidget(dockManager_, title);
    dock->setObjectName(title);
    dock->setWidget(content);
    if (windowMenu_ != nullptr) {
        windowMenu_->addAction(dock->toggleViewAction());
    }
    return dock;
}

QWidget* MainWindow::createSceneViewPanel()
{
    auto* frame = new QFrame;
    frame->setObjectName(QStringLiteral("SceneViewPanel"));
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(0, 0, 0, 0);

    sceneViewport_ = new ViewportWidget(ViewportMode::Scene);
    sceneViewport_->setObjectName(QStringLiteral("SceneViewport"));
    sceneViewport_->setScene(&scene_);
    sceneViewport_->setAssetManager(&assetManager_);
    sceneViewport_->setRenderer(renderer_.get());
    sceneViewport_->setEnvironmentSettings(environmentSettings_);
    sceneViewport_->setEditorSunLight(editorSunLight_);
    sceneViewport_->setSelectionCallback([this](scene::EntityId id) {
        if (id.isValid()) {
            selectEntity(id);
            return;
        }
        clearSelection();
    });
    sceneViewport_->setTransformEditedCallback([this](scene::EntityId id) {
        if (id != selectedEntityId_) {
            return;
        }
        updateInspector();
        if (sceneViewport_ != nullptr) {
            sceneViewport_->update();
        }
        if (gameViewport_ != nullptr) {
            gameViewport_->update();
        }
    });
    layout->addWidget(sceneViewport_);
    return frame;
}

QWidget* MainWindow::createGameViewPanel()
{
    auto* frame = new QFrame;
    frame->setObjectName(QStringLiteral("GameViewPanel"));
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(0, 0, 0, 0);

    gameViewport_ = new ViewportWidget(ViewportMode::Game);
    gameViewport_->setObjectName(QStringLiteral("GameViewport"));
    gameViewport_->setScene(&scene_);
    gameViewport_->setAssetManager(&assetManager_);
    gameViewport_->setRenderer(renderer_.get());
    gameViewport_->setEnvironmentSettings(environmentSettings_);
    gameViewport_->setEditorSunLight(editorSunLight_);
    gameViewport_->setTransformEditedCallback([this](scene::EntityId id) {
        if (id == selectedEntityId_) {
            updateInspector();
        }
        if (sceneViewport_ != nullptr) {
            sceneViewport_->update();
        }
    });
    layout->addWidget(gameViewport_);
    return frame;
}

QWidget* MainWindow::createHierarchyPanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(6, 6, 6, 6);

    auto* buttonRow = new QHBoxLayout;
    auto* createButton = makeToolButton(QStringLiteral("Create"));
    auto* duplicateButton = makeToolButton(QStringLiteral("Duplicate"));
    auto* deleteButton = makeToolButton(QStringLiteral("Delete"));
    buttonRow->addWidget(createButton);
    buttonRow->addWidget(duplicateButton);
    buttonRow->addWidget(deleteButton);
    layout->addLayout(buttonRow);

    auto* tree = new SceneHierarchyWidget;
    tree->setScene(&scene_);
    tree->setAssetManager(&assetManager_);
    tree->setAssetDropCallback([this](assets::AssetId assetId, const std::filesystem::path& path) {
        if (assetId.isValid()) {
            const auto records = assetManager_.records();
            const auto record = std::find_if(records.begin(), records.end(), [assetId](const assets::AssetRecord& candidate) {
                return candidate.id == assetId;
            });
            if (record != records.end() && record->type == assets::AssetType::Model) {
                createImportedModelEntity(*record);
                return;
            }
        }
        if (!path.empty()) {
            (void)importAssetFromPath(QString::fromStdWString(path.wstring()), true);
        }
    });
    layout->addWidget(tree);

    hierarchyTree_ = tree;
    connect(createButton, &QPushButton::clicked, this, [this] {
        createEmptyEntity(QStringLiteral("GameObject"));
    });
    connect(duplicateButton, &QPushButton::clicked, this, [this] {
        duplicateSelectedEntity();
    });
    connect(deleteButton, &QPushButton::clicked, this, [this] {
        deleteSelectedEntity();
    });
    connect(tree, &QTreeWidget::itemSelectionChanged, this, [this] {
        auto* hierarchy = dynamic_cast<SceneHierarchyWidget*>(hierarchyTree_);
        selectedEntityId_ = hierarchy == nullptr ? scene::EntityId {} : hierarchy->selectedSceneEntity();
        updateInspector();
        refreshViewports();
    });

    duplicateButton->setEnabled(false);
    deleteButton->setEnabled(false);
    duplicateEntityButton_ = duplicateButton;
    deleteEntityButton_ = deleteButton;

    return panel;
}

QWidget* MainWindow::createInspectorPanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 8, 8, 8);

    auto* form = new QFormLayout;
    sceneNameEdit_ = new QLineEdit;
    form->addRow(QStringLiteral("Scene"), sceneNameEdit_);

    entityNameEdit_ = new QLineEdit;
    form->addRow(QStringLiteral("Name"), entityNameEdit_);

    auto makeVectorRow = [this](QDoubleSpinBox*& x, QDoubleSpinBox*& y, QDoubleSpinBox*& z) {
        auto* row = new QWidget;
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        x = createTransformSpinBox();
        y = createTransformSpinBox();
        z = createTransformSpinBox();
        rowLayout->addWidget(x);
        rowLayout->addWidget(y);
        rowLayout->addWidget(z);
        return row;
    };

    form->addRow(QStringLiteral("Position"), makeVectorRow(positionX_, positionY_, positionZ_));
    form->addRow(QStringLiteral("Rotation"), makeVectorRow(rotationX_, rotationY_, rotationZ_));
    form->addRow(QStringLiteral("Scale"), makeVectorRow(scaleX_, scaleY_, scaleZ_));
    componentSummary_ = new QLabel(QStringLiteral("-"));
    componentSummary_->setWordWrap(true);
    form->addRow(QStringLiteral("Components"), componentSummary_);
    scriptComponentCombo_ = new QComboBox;
    form->addRow(QStringLiteral("Script Component"), scriptComponentCombo_);
    scriptAssetCombo_ = new QComboBox;
    form->addRow(QStringLiteral("Script Asset"), scriptAssetCombo_);
    scriptEnabledCheck_ = new QCheckBox(QStringLiteral("Script Enabled"));
    form->addRow(QStringLiteral("Enabled"), scriptEnabledCheck_);
    scriptFieldsTable_ = new QTableWidget;
    scriptFieldsTable_->setColumnCount(2);
    scriptFieldsTable_->setHorizontalHeaderLabels({QStringLiteral("Field"), QStringLiteral("Value")});
    scriptFieldsTable_->horizontalHeader()->setStretchLastSection(true);
    scriptFieldsTable_->verticalHeader()->setVisible(false);
    scriptFieldsTable_->setMinimumHeight(140);
    form->addRow(QStringLiteral("Fields"), scriptFieldsTable_);
    scriptStatusLabel_ = new QLabel;
    scriptStatusLabel_->setWordWrap(true);
    form->addRow(QStringLiteral("Status"), scriptStatusLabel_);
    removeScriptButton_ = makeToolButton(QStringLiteral("Remove Script Component"));
    form->addRow(QString(), removeScriptButton_);
    rigidbodyEnabledCheck_ = new QCheckBox(QStringLiteral("Rigidbody Enabled"));
    form->addRow(QStringLiteral("Rigidbody"), rigidbodyEnabledCheck_);
    rigidbodyBodyTypeCombo_ = new QComboBox;
    rigidbodyBodyTypeCombo_->addItems({QStringLiteral("Dynamic"), QStringLiteral("Kinematic"), QStringLiteral("Static")});
    form->addRow(QStringLiteral("Body Type"), rigidbodyBodyTypeCombo_);
    rigidbodyMass_ = createTransformSpinBox();
    rigidbodyMass_->setRange(0.001, 100000.0);
    rigidbodyMass_->setValue(1.0);
    form->addRow(QStringLiteral("Mass"), rigidbodyMass_);
    rigidbodyGravityCheck_ = new QCheckBox(QStringLiteral("Use Gravity"));
    form->addRow(QStringLiteral("Gravity"), rigidbodyGravityCheck_);
    rigidbodyLinearDrag_ = createTransformSpinBox();
    rigidbodyLinearDrag_->setRange(0.0, 10000.0);
    form->addRow(QStringLiteral("Linear Damping"), rigidbodyLinearDrag_);
    rigidbodyAngularDrag_ = createTransformSpinBox();
    rigidbodyAngularDrag_->setRange(0.0, 10000.0);
    form->addRow(QStringLiteral("Angular Damping"), rigidbodyAngularDrag_);
    colliderEnabledCheck_ = new QCheckBox(QStringLiteral("Collider Enabled"));
    form->addRow(QStringLiteral("Collider"), colliderEnabledCheck_);
    colliderShapeCombo_ = new QComboBox;
    colliderShapeCombo_->addItems({QStringLiteral("Box"), QStringLiteral("Sphere"), QStringLiteral("Capsule"), QStringLiteral("Mesh"), QStringLiteral("Terrain")});
    form->addRow(QStringLiteral("Collider Shape"), colliderShapeCombo_);
    form->addRow(QStringLiteral("Collider Size"), makeVectorRow(colliderSizeX_, colliderSizeY_, colliderSizeZ_));
    form->addRow(QStringLiteral("Collider Offset"), makeVectorRow(colliderOffsetX_, colliderOffsetY_, colliderOffsetZ_));
    colliderRadius_ = createTransformSpinBox();
    colliderRadius_->setRange(0.001, 100000.0);
    form->addRow(QStringLiteral("Radius"), colliderRadius_);
    colliderHeight_ = createTransformSpinBox();
    colliderHeight_->setRange(0.001, 100000.0);
    form->addRow(QStringLiteral("Height"), colliderHeight_);
    colliderTriggerCheck_ = new QCheckBox(QStringLiteral("Is Trigger"));
    form->addRow(QStringLiteral("Trigger"), colliderTriggerCheck_);
    layout->addLayout(form);

    auto* addComponent = makeToolButton(QStringLiteral("Add Component"));
    connect(addComponent, &QPushButton::clicked, this, [this, addComponent] {
        showAddComponentMenu(addComponent);
    });
    addComponentButton_ = addComponent;
    layout->addWidget(addComponent);
    layout->addStretch();

    connect(sceneNameEdit_, &QLineEdit::editingFinished, this, [this] {
        applyInspectorToSelection();
    });
    connect(entityNameEdit_, &QLineEdit::editingFinished, this, [this] {
        applyInspectorToSelection();
    });
    connect(scriptEnabledCheck_, &QCheckBox::toggled, this, [this](bool) {
        applyInspectorToSelection();
    });
    connect(scriptComponentCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (inspectorUpdating_ || index < 0) {
            return;
        }
        inspectedScriptInstanceId_ = scene::ScriptInstanceId(
            scriptComponentCombo_->itemData(index).toULongLong());
        updateInspector();
    });
    connect(scriptAssetCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (inspectorUpdating_ || index < 0 || !inspectedScriptInstanceId_.isValid()) {
            return;
        }
        auto* entity = scene_.findEntity(selectedEntityId_);
        auto* script = entity == nullptr ? nullptr : scene::findScript(*entity, inspectedScriptInstanceId_);
        if (script == nullptr) {
            return;
        }
        auto replacement = *script;
        std::string error;
        if (!scriptRegistry_.replaceComponentAsset(
                replacement,
                scriptAssetCombo_->itemData(index).toString().toStdString(),
                &error)) {
            scriptStatusLabel_->setText(QString::fromStdString(error));
            return;
        }
        (void)scene_.updateScript(selectedEntityId_, std::move(replacement));
        updateInspector();
    });
    connect(removeScriptButton_, &QPushButton::clicked, this, [this] {
        removeScriptFromSelection();
    });
    connect(scriptFieldsTable_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem*) {
        applyInspectorToSelection();
    });
    const auto applyPhysics = [this] { applyInspectorToSelection(); };
    connect(rigidbodyEnabledCheck_, &QCheckBox::toggled, this, applyPhysics);
    connect(rigidbodyBodyTypeCombo_, &QComboBox::currentIndexChanged, this, [this](int) { applyInspectorToSelection(); });
    connect(rigidbodyMass_, &QDoubleSpinBox::valueChanged, this, [this](double) { applyInspectorToSelection(); });
    connect(rigidbodyGravityCheck_, &QCheckBox::toggled, this, applyPhysics);
    connect(rigidbodyLinearDrag_, &QDoubleSpinBox::valueChanged, this, [this](double) { applyInspectorToSelection(); });
    connect(rigidbodyAngularDrag_, &QDoubleSpinBox::valueChanged, this, [this](double) { applyInspectorToSelection(); });
    connect(colliderEnabledCheck_, &QCheckBox::toggled, this, applyPhysics);
    connect(colliderShapeCombo_, &QComboBox::currentIndexChanged, this, [this](int) { applyInspectorToSelection(); });
    for (auto* spinBox : {colliderSizeX_, colliderSizeY_, colliderSizeZ_, colliderOffsetX_, colliderOffsetY_, colliderOffsetZ_, colliderRadius_, colliderHeight_}) {
        connect(spinBox, &QDoubleSpinBox::valueChanged, this, [this](double) { applyInspectorToSelection(); });
    }
    connect(colliderTriggerCheck_, &QCheckBox::toggled, this, applyPhysics);

    const std::array<QDoubleSpinBox*, 9> spinBoxes {
        positionX_, positionY_, positionZ_,
        rotationX_, rotationY_, rotationZ_,
        scaleX_, scaleY_, scaleZ_,
    };
    for (auto* spinBox : spinBoxes) {
        connect(spinBox, &QDoubleSpinBox::valueChanged, this, [this](double) {
            applyInspectorToSelection();
        });
    }

    return panel;
}

QWidget* MainWindow::createProjectPanel()
{
    auto* tabs = new QTabWidget;
    auto* assetsPanel = new QWidget;
    auto* assetsLayout = new QVBoxLayout(assetsPanel);
    assetsLayout->setContentsMargins(6, 6, 6, 6);
    auto* importButton = makeToolButton(QStringLiteral("Import Asset"));
    assetsLayout->addWidget(importButton, 0, Qt::AlignLeft);
    projectBrowser_ = new ProjectBrowserWidget;
    projectBrowser_->setObjectName(QStringLiteral("ProjectBrowser"));
    projectBrowser_->setAssetManager(&assetManager_);
    projectBrowser_->setProjectRoot(std::filesystem::path(PROJECTUNITY_SOURCE_DIR) / "Project" / "Assets");
    projectBrowser_->setSelectionChangedCallback([](const ProjectBrowserSelection& selection) {
        core::logInfo(
            core::LogCategory::Assets,
            "Project Browser selected assetId=" + std::to_string(selection.assetId.value())
                + " subAssetId=" + std::to_string(selection.subAssetId));
    });
    projectBrowser_->setAssetActivatedCallback([this](const ProjectBrowserSelection& selection) {
        if (selection.subAssetId != 0U) {
            return;
        }
        const auto records = assetManager_.records();
        const auto record = std::find_if(records.begin(), records.end(), [selection](const assets::AssetRecord& candidate) {
            return candidate.id == selection.assetId;
        });
        if (record != records.end() && record->type == assets::AssetType::Model) {
            createImportedModelEntity(*record);
        }
    });
    assetsLayout->addWidget(projectBrowser_);
    connect(importButton, &QPushButton::clicked, this, [this] {
        importAsset();
    });
    tabs->addTab(assetsPanel, QStringLiteral("Project"));
    tabs->addTab(createTextPanel(QStringLiteral("Packages"), {QStringLiteral("Engine packages"), QStringLiteral("Third party packages")}), QStringLiteral("Packages"));
    return tabs;
}

QWidget* MainWindow::createAssetImportPanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(6, 6, 6, 6);

    auto* header = new QLabel(QStringLiteral("Asset Import"));
    header->setObjectName(QStringLiteral("PanelHeader"));
    layout->addWidget(header);

    assetImportStatus_ = new QLabel(QStringLiteral("Idle"));
    assetImportStatus_->setWordWrap(true);
    layout->addWidget(assetImportStatus_);

    assetImportProgress_ = new QProgressBar;
    assetImportProgress_->setRange(0, 100);
    assetImportProgress_->setValue(0);
    assetImportProgress_->setFormat(QStringLiteral("Idle"));
    layout->addWidget(assetImportProgress_);

    layout->addWidget(createTextPanel(
        QStringLiteral("Pipeline"),
        {
            QStringLiteral("Read source file"),
            QStringLiteral("Validate model or texture"),
            QStringLiteral("Process meshes/materials"),
            QStringLiteral("Write asset cache"),
        }));
    return panel;
}

QWidget* MainWindow::createBottomPanel()
{
    auto* tabs = new QTabWidget;

    consoleView_ = new QPlainTextEdit;
    consoleView_->setReadOnly(true);
    consoleView_->setMaximumBlockCount(5000);

    tabs->addTab(consoleView_, QStringLiteral("Console"));
    tabs->addTab(createProfilerPanel(), QStringLiteral("Profiler"));
    tabs->addTab(createTextPanel(QStringLiteral("Network"), {QStringLiteral("Connection"), QStringLiteral("Ping"), QStringLiteral("Rejected packets")}), QStringLiteral("Network"));
    return tabs;
}

QWidget* MainWindow::createProfilerPanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);

    auto* header = new QLabel(QStringLiteral("Profiler"));
    header->setObjectName(QStringLiteral("PanelHeader"));
    layout->addWidget(header);

    profilerTable_ = new QTableWidget(55, 2);
    profilerTable_->setHorizontalHeaderLabels({QStringLiteral("Metric"), QStringLiteral("Value")});
    profilerTable_->horizontalHeader()->setStretchLastSection(true);
    profilerTable_->verticalHeader()->setVisible(false);
    profilerTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    profilerTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    const QStringList rows {
        QStringLiteral("GPU / profiler source"),
        QStringLiteral("Vulkan API"),
        QStringLiteral("Render CPU time"),
        QStringLiteral("Render CPU avg"),
        QStringLiteral("Resource prepare CPU"),
        QStringLiteral("Command record CPU"),
        QStringLiteral("Shadow record CPU"),
        QStringLiteral("Mesh record CPU"),
        QStringLiteral("Color record CPU"),
        QStringLiteral("Viewport frames"),
        QStringLiteral("Scene nodes"),
        QStringLiteral("Render chunks total"),
        QStringLiteral("Visible chunks / total"),
        QStringLiteral("Render instances total"),
        QStringLiteral("Visible instances / total"),
        QStringLiteral("Large chunks"),
        QStringLiteral("Max chunk extent"),
        QStringLiteral("Top chunk triangles"),
        QStringLiteral("Top chunk instances"),
        QStringLiteral("Mesh candidates last"),
        QStringLiteral("View-skipped mesh"),
        QStringLiteral("Mesh draws last"),
        QStringLiteral("Mesh batches last"),
        QStringLiteral("Visible triangles / candidates"),
        QStringLiteral("View-skipped triangles"),
        QStringLiteral("LOD draws last"),
        QStringLiteral("LOD triangle reduction"),
        QStringLiteral("HLOD active/available"),
        QStringLiteral("HLOD triangle reduction"),
        QStringLiteral("Mesh draws total"),
        QStringLiteral("Textured draws total"),
        QStringLiteral("Color draws last"),
        QStringLiteral("Color upload last"),
        QStringLiteral("Color upload total"),
        QStringLiteral("Mesh uploads last/total"),
        QStringLiteral("Texture uploads last/total"),
        QStringLiteral("Static upload last"),
        QStringLiteral("Static upload total"),
        QStringLiteral("Resident mesh/texture"),
        QStringLiteral("Lights last"),
        QStringLiteral("Shadow frames"),
        QStringLiteral("Shadow views last"),
        QStringLiteral("Shadow batches last"),
        QStringLiteral("Shadow batches culled"),
        QStringLiteral("Shadow casters last/total"),
        QStringLiteral("GPU timestamps"),
        QStringLiteral("GPU frame time"),
        QStringLiteral("GPU shadow time"),
        QStringLiteral("GPU mesh time"),
        QStringLiteral("GPU color time"),
        QStringLiteral("Viewport surface prepares"),
        QStringLiteral("Editor frame build CPU"),
        QStringLiteral("RenderWorld build CPU"),
        QStringLiteral("RenderWorld records rebuilt"),
        QStringLiteral("RenderWorld records reused"),
    };
    for (int row = 0; row < rows.size(); ++row) {
        profilerTable_->setItem(row, 0, new QTableWidgetItem(rows.at(row)));
        profilerTable_->setItem(row, 1, new QTableWidgetItem(QStringLiteral("-")));
    }
    layout->addWidget(profilerTable_);
    updateProfilerPanel();
    return panel;
}

QWidget* MainWindow::createTextPanel(const QString& title, const QStringList& lines) const
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);

    auto* header = new QLabel(title);
    header->setObjectName(QStringLiteral("PanelHeader"));
    layout->addWidget(header);

    const int rowCount = static_cast<int>(lines.size());
    auto* table = new QTableWidget(rowCount, 1);
    table->setHorizontalHeaderLabels({QStringLiteral("Item")});
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::SingleSelection);

    for (int row = 0; row < rowCount; ++row) {
        table->setItem(row, 0, new QTableWidgetItem(lines.at(row)));
    }

    layout->addWidget(table);
    return panel;
}

QDoubleSpinBox* MainWindow::createTransformSpinBox()
{
    auto* spinBox = new QDoubleSpinBox;
    spinBox->setDecimals(3);
    spinBox->setRange(-100000.0, 100000.0);
    spinBox->setSingleStep(0.1);
    spinBox->setMinimumWidth(72);
    return spinBox;
}

} // namespace projectunity::editor
