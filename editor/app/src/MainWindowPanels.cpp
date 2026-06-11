#include <projectunity/editor/MainWindow.hpp>

#include <projectunity/editor/ViewportWidget.hpp>

#include <DockAreaWidget.h>
#include <DockManager.h>
#include <DockWidget.h>

#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <array>
#include <utility>

namespace projectunity::editor {
namespace {

constexpr int kEntityIdRole = Qt::UserRole + 1;

[[nodiscard]] QPushButton* makeToolButton(const QString& text)
{
    auto* button = new QPushButton(text);
    button->setMinimumHeight(28);
    return button;
}

[[nodiscard]] QDoubleSpinBox* makeLightingSpinBox(double maximum)
{
    auto* spinBox = new QDoubleSpinBox;
    spinBox->setDecimals(3);
    spinBox->setRange(0.0, maximum);
    spinBox->setSingleStep(0.05);
    spinBox->setMinimumWidth(72);
    return spinBox;
}

[[nodiscard]] scene::EntityId entityIdFromItem(const QTreeWidgetItem* item)
{
    if (item == nullptr) {
        return {};
    }
    return scene::EntityId(item->data(0, kEntityIdRole).toULongLong());
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
    fileMenu->addAction(QStringLiteral("Exit"), qApp, &QApplication::quit);

    menuBar()->addMenu(QStringLiteral("&Edit"));
    menuBar()->addMenu(QStringLiteral("&Assets"));
    auto* gameObjectMenu = menuBar()->addMenu(QStringLiteral("&GameObject"));
    gameObjectMenu->addAction(QStringLiteral("Create Empty"), this, [this] {
        createEmptyEntity(QStringLiteral("GameObject"));
    });
    gameObjectMenu->addAction(QStringLiteral("Duplicate"), this, [this] {
        duplicateSelectedEntity();
    });
    gameObjectMenu->addAction(QStringLiteral("Delete"), this, [this] {
        deleteSelectedEntity();
    });
    menuBar()->addMenu(QStringLiteral("&Component"));
    windowMenu_ = menuBar()->addMenu(QStringLiteral("&Window"));
    windowMenu_->addAction(QStringLiteral("Reset Layout"), this, [this] {
        resetEditorLayout();
    });
    windowMenu_->addSeparator();
    menuBar()->addMenu(QStringLiteral("&Tools"));
    menuBar()->addMenu(QStringLiteral("&Build"));
    menuBar()->addMenu(QStringLiteral("&Help"));
}

void MainWindow::createToolbar()
{
    auto* toolbar = addToolBar(QStringLiteral("Toolbar"));
    toolbar->setObjectName(QStringLiteral("MainToolbar"));
    toolbar->setMovable(false);

    auto* transformGroup = new QActionGroup(this);
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
    toolbar->addAction(QStringLiteral("Play"), this, [] {
        core::logInfo(core::LogCategory::Editor, "Play command invoked");
    });
    toolbar->addAction(QStringLiteral("Pause"), this, [] {
        core::logInfo(core::LogCategory::Editor, "Pause command invoked");
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
    auto* projectDock = createDockWidget(QStringLiteral("Project / Assets / Packages"), createProjectPanel());
    auto* bottomDock = createDockWidget(QStringLiteral("Console / Profiler / Network"), createBottomPanel());
    auto* importDock = createDockWidget(QStringLiteral("Asset Import"), createAssetImportPanel());
    auto* terrainDock = createDockWidget(QStringLiteral("Terrain"), createTextPanel(
        QStringLiteral("Terrain"),
        {QStringLiteral("Generator"), QStringLiteral("Brushes"), QStringLiteral("Chunks and LOD")}));
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

    auto* tree = new QTreeWidget;
    tree->setHeaderLabel(QStringLiteral("GameObjects"));
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
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
        const auto selectedItems = hierarchyTree_->selectedItems();
        if (selectedItems.isEmpty()) {
            clearSelection();
            return;
        }
        selectedEntityId_ = entityIdFromItem(selectedItems.front());
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
    layout->addLayout(form);

    auto* addComponent = makeToolButton(QStringLiteral("Add Component"));
    addComponent->setEnabled(false);
    layout->addWidget(addComponent);
    layout->addStretch();

    connect(sceneNameEdit_, &QLineEdit::editingFinished, this, [this] {
        applyInspectorToSelection();
    });
    connect(entityNameEdit_, &QLineEdit::editingFinished, this, [this] {
        applyInspectorToSelection();
    });

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
    assetTable_ = new QTableWidget(0, 5);
    assetTable_->setHorizontalHeaderLabels({
        QStringLiteral("Name"),
        QStringLiteral("Type"),
        QStringLiteral("Source"),
        QStringLiteral("Cache"),
        QStringLiteral("Vertices"),
    });
    assetTable_->horizontalHeader()->setStretchLastSection(true);
    assetTable_->verticalHeader()->setVisible(false);
    assetTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    assetTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    assetsLayout->addWidget(assetTable_);
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

QWidget* MainWindow::createLightingPanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 8, 8, 8);

    auto* header = new QLabel(QStringLiteral("Lighting / Bake"));
    header->setObjectName(QStringLiteral("PanelHeader"));
    layout->addWidget(header);

    auto* form = new QFormLayout;
    const auto makeRgbRow = [this](QDoubleSpinBox*& r, QDoubleSpinBox*& g, QDoubleSpinBox*& b, const std::array<float, 3>& values) {
        auto* row = new QWidget;
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        r = makeLightingSpinBox(8.0);
        g = makeLightingSpinBox(8.0);
        b = makeLightingSpinBox(8.0);
        r->setValue(values[0]);
        g->setValue(values[1]);
        b->setValue(values[2]);
        rowLayout->addWidget(r);
        rowLayout->addWidget(g);
        rowLayout->addWidget(b);
        return row;
    };

    form->addRow(
        QStringLiteral("Sky RGB"),
        makeRgbRow(skyColorR_, skyColorG_, skyColorB_, environmentSettings_.skyColor));
    form->addRow(
        QStringLiteral("Ground RGB"),
        makeRgbRow(groundColorR_, groundColorG_, groundColorB_, environmentSettings_.groundColor));
    environmentIntensity_ = makeLightingSpinBox(16.0);
    environmentIntensity_->setValue(environmentSettings_.intensity);
    form->addRow(QStringLiteral("IBL Intensity"), environmentIntensity_);
    shadowModeCombo_ = new QComboBox;
    shadowModeCombo_->addItem(QStringLiteral("Live"), static_cast<int>(renderer::RenderShadowUpdateMode::Live));
    shadowModeCombo_->addItem(QStringLiteral("Frozen"), static_cast<int>(renderer::RenderShadowUpdateMode::Frozen));
    shadowModeCombo_->addItem(QStringLiteral("Off"), static_cast<int>(renderer::RenderShadowUpdateMode::Off));
    form->addRow(QStringLiteral("Shadows"), shadowModeCombo_);
    environmentTextureEdit_ = new QLineEdit;
    environmentTextureEdit_->setReadOnly(true);
    form->addRow(QStringLiteral("Environment"), environmentTextureEdit_);
    layout->addLayout(form);

    auto* environmentButtons = new QHBoxLayout;
    useEnvironmentTextureButton_ = makeToolButton(QStringLiteral("Use Project Texture"));
    clearEnvironmentTextureButton_ = makeToolButton(QStringLiteral("Clear Texture"));
    environmentButtons->addWidget(useEnvironmentTextureButton_);
    environmentButtons->addWidget(clearEnvironmentTextureButton_);
    layout->addLayout(environmentButtons);

    resetLightingButton_ = makeToolButton(QStringLiteral("Reset Defaults"));
    layout->addWidget(resetLightingButton_);
    layout->addStretch();

    const std::array<QDoubleSpinBox*, 7> spinBoxes {
        skyColorR_, skyColorG_, skyColorB_,
        groundColorR_, groundColorG_, groundColorB_,
        environmentIntensity_,
    };
    for (auto* spinBox : spinBoxes) {
        connect(spinBox, &QDoubleSpinBox::valueChanged, this, [this](double) {
            scheduleLightingSettingsApply();
        });
    }
    connect(shadowModeCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        const auto value = shadowModeCombo_->itemData(index).toInt();
        shadowUpdateMode_ = static_cast<renderer::RenderShadowUpdateMode>(value);
        pushLightingSettingsToViewports();
        saveLightingSettings();
    });
    connect(useEnvironmentTextureButton_, &QPushButton::clicked, this, [this] {
        useSelectedTextureAsEnvironment();
    });
    connect(clearEnvironmentTextureButton_, &QPushButton::clicked, this, [this] {
        clearEnvironmentTexture();
    });
    connect(resetLightingButton_, &QPushButton::clicked, this, [this] {
        resetLightingDefaults();
    });
    refreshEnvironmentTextureLabel();
    return panel;
}

void MainWindow::restoreLightingSettings()
{
    QSettings settings;
    environmentSettings_.skyColor = {
        settings.value(QStringLiteral("editor/lighting/skyR"), environmentSettings_.skyColor[0]).toFloat(),
        settings.value(QStringLiteral("editor/lighting/skyG"), environmentSettings_.skyColor[1]).toFloat(),
        settings.value(QStringLiteral("editor/lighting/skyB"), environmentSettings_.skyColor[2]).toFloat(),
    };
    environmentSettings_.groundColor = {
        settings.value(QStringLiteral("editor/lighting/groundR"), environmentSettings_.groundColor[0]).toFloat(),
        settings.value(QStringLiteral("editor/lighting/groundG"), environmentSettings_.groundColor[1]).toFloat(),
        settings.value(QStringLiteral("editor/lighting/groundB"), environmentSettings_.groundColor[2]).toFloat(),
    };
    environmentSettings_.intensity = settings.value(
        QStringLiteral("editor/lighting/intensity"),
        environmentSettings_.intensity).toFloat();
    const auto shadowModeValue = settings.value(
        QStringLiteral("editor/lighting/shadowUpdateMode"),
        static_cast<int>(renderer::RenderShadowUpdateMode::Live)).toInt();
    shadowUpdateMode_ = shadowModeValue >= static_cast<int>(renderer::RenderShadowUpdateMode::Live)
            && shadowModeValue <= static_cast<int>(renderer::RenderShadowUpdateMode::Off)
        ? static_cast<renderer::RenderShadowUpdateMode>(shadowModeValue)
        : renderer::RenderShadowUpdateMode::Live;
    environmentTextureId_ = assets::AssetId(settings.value(QStringLiteral("editor/lighting/environmentTexture"), 0).toULongLong());
    environmentTexture_ = environmentTextureId_.isValid() ? assetManager_.texture(environmentTextureId_) : nullptr;
    updateLightingPanelControls();
    pushLightingSettingsToViewports();
}

void MainWindow::saveLightingSettings()
{
    QSettings settings;
    settings.setValue(QStringLiteral("editor/lighting/skyR"), environmentSettings_.skyColor[0]);
    settings.setValue(QStringLiteral("editor/lighting/skyG"), environmentSettings_.skyColor[1]);
    settings.setValue(QStringLiteral("editor/lighting/skyB"), environmentSettings_.skyColor[2]);
    settings.setValue(QStringLiteral("editor/lighting/groundR"), environmentSettings_.groundColor[0]);
    settings.setValue(QStringLiteral("editor/lighting/groundG"), environmentSettings_.groundColor[1]);
    settings.setValue(QStringLiteral("editor/lighting/groundB"), environmentSettings_.groundColor[2]);
    settings.setValue(QStringLiteral("editor/lighting/intensity"), environmentSettings_.intensity);
    settings.setValue(QStringLiteral("editor/lighting/shadowUpdateMode"), static_cast<int>(shadowUpdateMode_));
    settings.setValue(QStringLiteral("editor/lighting/environmentTexture"), static_cast<qulonglong>(environmentTextureId_.value()));
}

void MainWindow::updateLightingPanelControls()
{
    const auto setValue = [](QDoubleSpinBox* spinBox, float value) {
        if (spinBox == nullptr) {
            return;
        }
        const QSignalBlocker blocker(spinBox);
        spinBox->setValue(value);
    };
    setValue(skyColorR_, environmentSettings_.skyColor[0]);
    setValue(skyColorG_, environmentSettings_.skyColor[1]);
    setValue(skyColorB_, environmentSettings_.skyColor[2]);
    setValue(groundColorR_, environmentSettings_.groundColor[0]);
    setValue(groundColorG_, environmentSettings_.groundColor[1]);
    setValue(groundColorB_, environmentSettings_.groundColor[2]);
    setValue(environmentIntensity_, environmentSettings_.intensity);
    if (shadowModeCombo_ != nullptr) {
        const QSignalBlocker blocker(shadowModeCombo_);
        shadowModeCombo_->setCurrentIndex(shadowModeCombo_->findData(static_cast<int>(shadowUpdateMode_)));
    }
    refreshEnvironmentTextureLabel();
}

void MainWindow::useSelectedTextureAsEnvironment()
{
    if (assetTable_ == nullptr) {
        return;
    }
    const auto row = assetTable_->currentRow();
    const auto records = assetManager_.records();
    if (row < 0 || row >= static_cast<int>(records.size())) {
        core::logWarning(core::LogCategory::Editor, "Select a Texture2D row in Project Browser before pressing Use Project Texture");
        return;
    }
    const auto& record = records[static_cast<std::size_t>(row)];
    if (record.type != assets::AssetType::Texture2D) {
        core::logWarning(core::LogCategory::Editor, "Selected Project asset is not a Texture2D environment source");
        return;
    }
    auto texture = assetManager_.texture(record.id);
    if (texture == nullptr) {
        core::logError(core::LogCategory::Assets, "Selected environment texture asset is not loaded");
        return;
    }
    environmentTextureId_ = record.id;
    environmentTexture_ = std::move(texture);
    refreshEnvironmentTextureLabel();
    pushLightingSettingsToViewports();
    saveLightingSettings();
    core::logInfo(core::LogCategory::Renderer, "Lighting environment texture assigned from Project Browser");
}

void MainWindow::clearEnvironmentTexture()
{
    environmentTextureId_ = {};
    environmentTexture_.reset();
    refreshEnvironmentTextureLabel();
    pushLightingSettingsToViewports();
    saveLightingSettings();
    core::logInfo(core::LogCategory::Renderer, "Lighting environment texture cleared");
}

void MainWindow::resetLightingDefaults()
{
    environmentSettings_ = renderer::RenderEnvironmentSettings {};
    shadowUpdateMode_ = renderer::RenderShadowUpdateMode::Live;
    environmentTextureId_ = {};
    environmentTexture_.reset();
    updateLightingPanelControls();
    pushLightingSettingsToViewports();
    saveLightingSettings();
    core::logInfo(core::LogCategory::Renderer, "Lighting environment reset to defaults");
}

void MainWindow::refreshEnvironmentTextureLabel()
{
    if (environmentTextureEdit_ == nullptr) {
        return;
    }
    if (environmentTexture_ == nullptr) {
        environmentTextureEdit_->setText(environmentTextureId_.isValid()
            ? QStringLiteral("Texture asset not loaded - procedural fallback active")
            : QStringLiteral("Procedural sky/ground (Sky/Ground/Intensity active)"));
        return;
    }
    environmentTextureEdit_->setText(QStringLiteral("Texture: %1 (%2x%3) - Intensity active")
        .arg(QString::fromStdString(environmentTexture_->name))
        .arg(environmentTexture_->width)
        .arg(environmentTexture_->height));
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
