#include <projectunity/editor/MainWindow.hpp>

#include <projectunity/editor/ProjectAssetTreeWidget.hpp>
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
#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPoint>
#include <QPolygonF>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QStyle>
#include <QStatusBar>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <array>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>

namespace projectunity::editor {
namespace {

constexpr int kMaterialSlotColumn = 0;
constexpr int kMaterialNameColumn = 1;
constexpr int kBaseColorColumn = 2;
constexpr int kNormalColumn = 3;
constexpr int kMetallicRoughnessColumn = 4;
constexpr int kEmissiveColumn = 5;
constexpr int kTilingColumn = 6;
constexpr int kOffsetColumn = 7;
constexpr int kOverrideColumn = 8;

[[nodiscard]] QPushButton* makeToolButton(const QString& text)
{
    auto* button = new QPushButton(text);
    button->setMinimumHeight(34);
    button->setObjectName(QStringLiteral("EditorButton"));
    return button;
}

[[nodiscard]] QLabel* makeMutedLabel(const QString& text = {})
{
    auto* label = new QLabel(text);
    label->setObjectName(QStringLiteral("MutedLabel"));
    label->setWordWrap(true);
    return label;
}

[[nodiscard]] QGroupBox* makeInspectorSection(const QString& title)
{
    auto* group = new QGroupBox(title);
    group->setObjectName(QStringLiteral("InspectorSection"));
    return group;
}

void configureTabWidget(QTabWidget& tabs)
{
    tabs.setDocumentMode(false);
    tabs.setElideMode(Qt::ElideRight);
    tabs.setUsesScrollButtons(true);
    tabs.tabBar()->setExpanding(false);
}

void addEditorTab(QTabWidget& tabs, QWidget* page, const QString& title, const QString& tooltip = {})
{
    const auto index = tabs.addTab(page, title);
    tabs.setTabToolTip(index, tooltip.isEmpty() ? title : tooltip);
}

[[nodiscard]] QIcon makeToolbarIcon(const QString& name, QColor color)
{
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color.lighter(135), 1.8));
    painter.setBrush(color);

    if (name == QStringLiteral("hand")) {
        painter.drawRoundedRect(QRectF(8.0, 5.0, 3.0, 12.0), 1.3, 1.3);
        painter.drawRoundedRect(QRectF(11.0, 4.0, 3.0, 13.0), 1.3, 1.3);
        painter.drawRoundedRect(QRectF(14.0, 6.0, 3.0, 11.0), 1.3, 1.3);
        painter.drawRoundedRect(QRectF(5.0, 10.0, 3.0, 8.0), 1.3, 1.3);
        painter.drawRoundedRect(QRectF(6.0, 15.0, 12.0, 6.0), 2.0, 2.0);
    } else if (name == QStringLiteral("move")) {
        painter.drawLine(QPointF(12.0, 3.0), QPointF(12.0, 21.0));
        painter.drawLine(QPointF(3.0, 12.0), QPointF(21.0, 12.0));
        painter.drawPolygon(QPolygonF({QPointF(12.0, 2.0), QPointF(9.0, 6.0), QPointF(15.0, 6.0)}));
        painter.drawPolygon(QPolygonF({QPointF(12.0, 22.0), QPointF(9.0, 18.0), QPointF(15.0, 18.0)}));
        painter.drawPolygon(QPolygonF({QPointF(2.0, 12.0), QPointF(6.0, 9.0), QPointF(6.0, 15.0)}));
        painter.drawPolygon(QPolygonF({QPointF(22.0, 12.0), QPointF(18.0, 9.0), QPointF(18.0, 15.0)}));
    } else if (name == QStringLiteral("rotate")) {
        painter.setBrush(Qt::NoBrush);
        painter.drawArc(QRectF(4.0, 4.0, 16.0, 16.0), 20 * 16, 285 * 16);
        painter.setBrush(color);
        painter.drawPolygon(QPolygonF({QPointF(18.5, 5.0), QPointF(20.5, 11.0), QPointF(14.5, 9.0)}));
    } else if (name == QStringLiteral("scale")) {
        painter.drawRoundedRect(QRectF(5.0, 12.0, 7.0, 7.0), 1.5, 1.5);
        painter.drawRoundedRect(QRectF(13.0, 5.0, 7.0, 7.0), 1.5, 1.5);
        painter.drawLine(QPointF(10.5, 13.0), QPointF(14.0, 10.5));
        painter.drawPolygon(QPolygonF({QPointF(18.0, 5.0), QPointF(20.5, 10.5), QPointF(15.0, 8.0)}));
    } else if (name == QStringLiteral("debug")) {
        painter.drawRoundedRect(QRectF(5.0, 5.0, 14.0, 14.0), 3.0, 3.0);
        painter.setPen(QPen(QColor(255, 255, 255, 145), 1.4));
        painter.drawLine(QPointF(8.0, 9.0), QPointF(16.0, 9.0));
        painter.drawLine(QPointF(8.0, 13.0), QPointF(16.0, 13.0));
        painter.drawLine(QPointF(8.0, 17.0), QPointF(13.0, 17.0));
    } else if (name == QStringLiteral("step")) {
        painter.drawPolygon(QPolygonF({QPointF(6.0, 5.0), QPointF(16.0, 12.0), QPointF(6.0, 19.0)}));
        painter.drawRoundedRect(QRectF(17.0, 5.0, 2.5, 14.0), 1.0, 1.0);
    } else {
        painter.drawEllipse(QRectF(5.0, 5.0, 14.0, 14.0));
    }
    return QIcon(pixmap);
}

class MaterialSlotsTable final : public QTableWidget {
public:
    using DropCallback = std::function<void(
        int,
        int,
        assets::AssetId,
        int,
        std::optional<std::uint32_t>)>;

    explicit MaterialSlotsTable(QWidget* parent = nullptr)
        : QTableWidget(parent)
    {
        setAcceptDrops(true);
        setDragDropMode(QAbstractItemView::DropOnly);
        setDropIndicatorShown(true);
    }

    void setDropCallback(DropCallback callback)
    {
        dropCallback_ = std::move(callback);
    }

protected:
    void dragEnterEvent(QDragEnterEvent* event) override
    {
        if (isProjectAssetDrag(event->mimeData())) {
            event->acceptProposedAction();
            return;
        }
        QTableWidget::dragEnterEvent(event);
    }

    void dragMoveEvent(QDragMoveEvent* event) override
    {
        if (isProjectAssetDrag(event->mimeData())) {
            event->acceptProposedAction();
            return;
        }
        QTableWidget::dragMoveEvent(event);
    }

    void dropEvent(QDropEvent* event) override
    {
        if (dropCallback_ == nullptr || !isProjectAssetDrag(event->mimeData())) {
            QTableWidget::dropEvent(event);
            return;
        }
        const auto* mime = event->mimeData();
        const auto assetId = assets::AssetId(mime->data(kProjectUnityAssetIdMime).toULongLong());
        const auto kind = mime->data(kProjectUnityAssetKindMime).toInt();
        std::optional<std::uint32_t> subAssetIndex;
        if (mime->hasFormat(kProjectUnitySubAssetIndexMime)) {
            subAssetIndex = mime->data(kProjectUnitySubAssetIndexMime).toUInt();
        }
        const auto target = itemAt(event->position().toPoint());
        const auto row = target == nullptr ? currentRow() : target->row();
        const auto column = target == nullptr ? currentColumn() : target->column();
        dropCallback_(row, column, assetId, kind, subAssetIndex);
        event->acceptProposedAction();
    }

private:
    [[nodiscard]] bool isProjectAssetDrag(const QMimeData* mime) const
    {
        return mime != nullptr
            && mime->hasFormat(kProjectUnityAssetIdMime)
            && mime->hasFormat(kProjectUnityAssetKindMime);
    }

    DropCallback dropCallback_;
};

} // namespace

void MainWindow::createMenus()
{
    auto* fileMenu = menuBar()->addMenu(QStringLiteral("&Archivo"));
    fileMenu->addAction(QStringLiteral("Nueva escena"), this, [this] {
        newScene();
    });
    fileMenu->addAction(QStringLiteral("Abrir escena"), this, [this] {
        openScene();
    });
    fileMenu->addAction(QStringLiteral("Guardar escena"), this, [this] {
        saveScene();
    });
    fileMenu->addAction(QStringLiteral("Guardar escena como"), this, [this] {
        saveSceneAs();
    });
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("Configuracion de build"), this, [] {
        core::logInfo(core::LogCategory::Editor, "Build Settings is not implemented yet");
    });
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("Salir"), qApp, &QApplication::quit);

    auto* editMenu = menuBar()->addMenu(QStringLiteral("&Editar"));
    editMenu->addAction(QStringLiteral("Deshacer"));
    editMenu->addAction(QStringLiteral("Rehacer"));
    editMenu->addSeparator();
    editMenu->addAction(QStringLiteral("Eliminar"), this, [this] { deleteSelectedEntity(); });
    editMenu->addAction(QStringLiteral("Duplicar"), this, [this] { duplicateSelectedEntity(); });
    editMenu->addSeparator();
    editMenu->addAction(QStringLiteral("Preferencias"));

    auto* assetsMenu = menuBar()->addMenu(QStringLiteral("&Assets"));
    assetsMenu->addAction(QStringLiteral("Importar asset"), this, [this] { importAsset(); });
    assetsMenu->addAction(QStringLiteral("Crear material"));
    assetsMenu->addAction(QStringLiteral("Crear textura placeholder"));
    assetsMenu->addAction(QStringLiteral("Crear script"), this, [this] { ensureFlyPlayerScriptAsset(); });
    assetsMenu->addSeparator();
    assetsMenu->addAction(QStringLiteral("Construir modulo de scripts"), this, [this] { (void)buildProjectScriptsModule(); });
    assetsMenu->addAction(QStringLiteral("Recargar scripts"), this, [this] { (void)reloadProjectScriptsModule(); });
    assetsMenu->addSeparator();
    assetsMenu->addAction(QStringLiteral("Refrescar"), this, [this] { rebuildAssetBrowser(); });
    assetsMenu->addAction(QStringLiteral("Reimportar seleccionado"));

    auto* gameObjectMenu = menuBar()->addMenu(QStringLiteral("&GameObject"));
    gameObjectMenu->addAction(QStringLiteral("Crear vacio"), this, [this] {
        createEmptyEntity(QStringLiteral("GameObject"));
    });
    auto* object3dMenu = gameObjectMenu->addMenu(QStringLiteral("Objeto 3D"));
    object3dMenu->addAction(QStringLiteral("Cube"), this, [this] { createCubeEntity(); });
    object3dMenu->addAction(QStringLiteral("Sphere"), this, [this] { createSphereEntity(); });
    object3dMenu->addAction(QStringLiteral("Plane"), this, [this] { createPlaneEntity(); });
    object3dMenu->addAction(QStringLiteral("Terrain"), this, [this] { createTerrainEntity(); });
    auto* lightMenu = gameObjectMenu->addMenu(QStringLiteral("Luz"));
    lightMenu->addAction(QStringLiteral("Luz direccional"), this, [this] {
        createLightEntity(scene::LightComponentType::Directional);
    });
    lightMenu->addAction(QStringLiteral("Luz puntual"), this, [this] {
        createLightEntity(scene::LightComponentType::Point);
    });
    lightMenu->addAction(QStringLiteral("Luz spot"), this, [this] {
        createLightEntity(scene::LightComponentType::Spot);
    });
    gameObjectMenu->addAction(QStringLiteral("Camera"), this, [this] { createCameraEntity(); });
    gameObjectMenu->addAction(QStringLiteral("Player"), this, [this] {
        createPlayerEntity();
    });
    gameObjectMenu->addSeparator();
    gameObjectMenu->addAction(QStringLiteral("Duplicar"), this, [this] {
        duplicateSelectedEntity();
    });
    gameObjectMenu->addAction(QStringLiteral("Eliminar"), this, [this] {
        deleteSelectedEntity();
    });

    auto* componentMenu = menuBar()->addMenu(QStringLiteral("&Componentes"));
    componentMenu->addAction(QStringLiteral("Agregar componente"), this, [this] {
        showAddComponentMenu(nullptr);
    });

    windowMenu_ = menuBar()->addMenu(QStringLiteral("&Ventana"));
    windowMenu_->addAction(QStringLiteral("Restablecer layout"), this, [this] {
        resetEditorLayout();
    });
    windowMenu_->addSeparator();

    auto* toolsMenu = menuBar()->addMenu(QStringLiteral("&Herramientas"));
    toolsMenu->addAction(QStringLiteral("Construir modulo de scripts"), this, [this] { (void)buildProjectScriptsModule(); });
    toolsMenu->addAction(QStringLiteral("Recargar scripts"), this, [this] { (void)reloadProjectScriptsModule(); });
    toolsMenu->addSeparator();
    toolsMenu->addAction(QStringLiteral("Generador de terreno"), this, [this] {
        createTerrainEntity();
    });
    toolsMenu->addAction(QStringLiteral("Pincel de terreno (PARCIAL)"));
    toolsMenu->addAction(QStringLiteral("Iluminacion / Bake"));
    toolsMenu->addAction(QStringLiteral("Bake de navegacion"));
    toolsMenu->addAction(QStringLiteral("Debug de fisica"));

    auto* helpMenu = menuBar()->addMenu(QStringLiteral("&Ayuda"));
    helpMenu->addAction(QStringLiteral("Acerca de"), this, [] {
        core::logInfo(core::LogCategory::Editor, "ProjectUnity Editor");
    });
    helpMenu->addAction(QStringLiteral("Documentacion"));
}

void MainWindow::createToolbar()
{
    auto* toolbar = addToolBar(QStringLiteral("Herramientas"));
    toolbar->setObjectName(QStringLiteral("MainToolbar"));
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(20, 20));
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    auto* transformGroup = new QActionGroup(this);
    transformGroup->setExclusive(true);
    struct ToolAction {
        QString label;
        QString iconName;
        ViewportTool tool {ViewportTool::Move};
    };
    const std::array<ToolAction, 4> tools {{
        {QStringLiteral("Mano"), QStringLiteral("hand"), ViewportTool::Hand},
        {QStringLiteral("Mover"), QStringLiteral("move"), ViewportTool::Move},
        {QStringLiteral("Rotar"), QStringLiteral("rotate"), ViewportTool::Rotate},
        {QStringLiteral("Escalar"), QStringLiteral("scale"), ViewportTool::Scale},
    }};
    for (const auto& toolAction : tools) {
        auto* action = toolbar->addAction(
            makeToolbarIcon(toolAction.iconName, QColor(103, 185, 214)),
            toolAction.label);
        action->setCheckable(true);
        action->setToolTip(QStringLiteral("Herramienta: %1").arg(toolAction.label));
        transformGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, toolAction] {
            if (sceneViewport_ != nullptr) {
                sceneViewport_->setTool(toolAction.tool);
            }
            core::logInfo(core::LogCategory::Editor, QStringLiteral("Herramienta seleccionada: %1").arg(toolAction.label).toStdString());
        });
    }
    transformGroup->actions().at(1)->setChecked(true);
    toolbar->addSeparator();
    auto* spaceGroup = new QActionGroup(this);
    spaceGroup->setExclusive(true);
    auto* localAction = toolbar->addAction(makeToolbarIcon(QStringLiteral("local"), QColor(159, 183, 213)), QStringLiteral("Local"));
    auto* globalAction = toolbar->addAction(makeToolbarIcon(QStringLiteral("global"), QColor(159, 183, 213)), QStringLiteral("Global"));
    localAction->setToolTip(QStringLiteral("Espacio de transformacion local"));
    globalAction->setToolTip(QStringLiteral("Espacio de transformacion global"));
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
    auto* wireAction = debugMenu->addAction(QStringLiteral("Wireframe"));
    wireAction->setCheckable(true);
    connect(wireAction, &QAction::triggered, this, [this](bool enabled) {
        if (sceneViewport_ != nullptr) {
            sceneViewport_->setMeshWireOverlayEnabled(enabled);
        }
    });
    auto* xrayAction = debugMenu->addAction(QStringLiteral("Rayos X"));
    xrayAction->setCheckable(true);
    connect(xrayAction, &QAction::triggered, this, [this](bool enabled) {
        if (sceneViewport_ != nullptr) {
            sceneViewport_->setAssetXrayDebugEnabled(enabled);
        }
    });
    auto* lodDebugAction = debugMenu->addAction(QStringLiteral("LOD / distancia"));
    lodDebugAction->setCheckable(true);
    connect(lodDebugAction, &QAction::triggered, this, [this](bool enabled) {
        if (sceneViewport_ != nullptr) {
            sceneViewport_->setLodDebugOverlayEnabled(enabled);
        }
        if (gameViewport_ != nullptr) {
            gameViewport_->setLodDebugOverlayEnabled(enabled);
        }
    });
    auto* shadowDebugAction = debugMenu->addAction(QStringLiteral("Marcas de sombras"));
    shadowDebugAction->setCheckable(true);
    connect(shadowDebugAction, &QAction::triggered, this, [this](bool enabled) {
        if (sceneViewport_ != nullptr) {
            sceneViewport_->setShadowDebugOverlayEnabled(enabled);
        }
        if (gameViewport_ != nullptr) {
            gameViewport_->setShadowDebugOverlayEnabled(enabled);
        }
    });
    auto* sourceDebugAction = debugMenu->addAction(QStringLiteral("Objetos fuente"));
    sourceDebugAction->setCheckable(true);
    connect(sourceDebugAction, &QAction::triggered, this, [this](bool enabled) {
        if (sceneViewport_ != nullptr) {
            sceneViewport_->setSourceObjectDebugOverlayEnabled(enabled);
        }
        if (gameViewport_ != nullptr) {
            gameViewport_->setSourceObjectDebugOverlayEnabled(enabled);
        }
    });
    auto* sunDebugAction = debugMenu->addAction(QStringLiteral("Direccion del sol"));
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
    debugButton->setIcon(makeToolbarIcon(QStringLiteral("debug"), QColor(157, 174, 196)));
    debugButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    debugButton->setMinimumHeight(34);
    debugButton->setPopupMode(QToolButton::InstantPopup);
    debugButton->setMenu(debugMenu);
    toolbar->addWidget(debugButton);
    toolbar->addSeparator();
    toolbar->addAction(style()->standardIcon(QStyle::SP_MediaPlay), QStringLiteral("Reproducir"), this, [this] {
        startPlayMode();
    });
    toolbar->addAction(style()->standardIcon(QStyle::SP_MediaPause), QStringLiteral("Pausar"), this, [this] {
        stopPlayMode();
    });
    toolbar->addAction(makeToolbarIcon(QStringLiteral("step"), QColor(116, 190, 142)), QStringLiteral("Paso"), this, [] {
        core::logInfo(core::LogCategory::Editor, "Step command invoked");
    });
}

void MainWindow::createDockLayout()
{
    ads::CDockManager::setConfigFlags(ads::CDockManager::DefaultOpaqueConfig);
    dockManager_ = new ads::CDockManager(this);
    dockManager_->setStyleSheet(qApp->styleSheet());

    auto* hierarchyDock = createDockWidget(QStringLiteral("Jerarquia"), createHierarchyPanel());
    auto* inspectorDock = createDockWidget(QStringLiteral("Inspector"), createInspectorPanel());
    auto* sceneDock = createDockWidget(QStringLiteral("Escena"), createSceneViewPanel());
    auto* gameDock = createDockWidget(QStringLiteral("Juego"), createGameViewPanel());
    gameDock_ = gameDock;
    auto* projectDock = createDockWidget(QStringLiteral("Proyecto"), createProjectPanel());
    auto* bottomDock = createDockWidget(QStringLiteral("Consola"), createBottomPanel());
    auto* importDock = createDockWidget(QStringLiteral("Importar"), createAssetImportPanel());
    auto* terrainDock = createDockWidget(QStringLiteral("Terreno"), createTerrainPanel());
    auto* lightingDock = createDockWidget(QStringLiteral("Iluminacion"), createLightingPanel());
    auto* physicsDock = createDockWidget(QStringLiteral("Fisica"), createTextPanel(
        QStringLiteral("Fisica"),
        {QStringLiteral("Cuerpos"), QStringLiteral("Colisionadores"), QStringLiteral("Consultas")}));
    auto* navigationDock = createDockWidget(QStringLiteral("Navegacion"), createTextPanel(
        QStringLiteral("Navegacion"),
        {QStringLiteral("Ajustes de bake"), QStringLiteral("Navmesh"), QStringLiteral("Rutas")}));
    auto* serverDock = createDockWidget(QStringLiteral("Servidor"), createTextPanel(
        QStringLiteral("Servidor"),
        {QStringLiteral("Modo"), QStringLiteral("Sesiones"), QStringLiteral("Eventos de seguridad")}));

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
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    auto* buttonRow = new QHBoxLayout;
    auto* createButton = makeToolButton(QStringLiteral("Crear"));
    auto* duplicateButton = makeToolButton(QStringLiteral("Duplicar"));
    auto* deleteButton = makeToolButton(QStringLiteral("Eliminar"));
    buttonRow->addWidget(createButton);
    buttonRow->addWidget(duplicateButton);
    buttonRow->addWidget(deleteButton);
    layout->addLayout(buttonRow);

    auto* tree = new SceneHierarchyWidget;
    tree->setScene(&scene_);
    tree->setAssetManager(&assetManager_);
    tree->setToolTip(QStringLiteral("Arrastra assets aqui para instanciarlos. Arrastra GameObjects para cambiar su padre."));
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
    tree->setHierarchyChangedCallback([this](scene::EntityId id) {
        selectedEntityId_ = id;
        rebuildHierarchy();
        selectEntity(id);
        refreshViewports();
        statusBar()->showMessage(QStringLiteral("Jerarquia actualizada"));
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
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(10);

    sceneNameEdit_ = new QLineEdit;
    entityNameEdit_ = new QLineEdit;

    auto makeVectorRow = [this](QDoubleSpinBox*& x, QDoubleSpinBox*& y, QDoubleSpinBox*& z) {
        auto* row = new QWidget;
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(6);
        x = createTransformSpinBox();
        y = createTransformSpinBox();
        z = createTransformSpinBox();
        rowLayout->addWidget(x);
        rowLayout->addWidget(y);
        rowLayout->addWidget(z);
        return row;
    };

    transformSection_ = makeInspectorSection(QStringLiteral("Seleccion"));
    auto* transformForm = new QFormLayout(transformSection_);
    transformForm->setLabelAlignment(Qt::AlignLeft);
    transformForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    transformForm->addRow(QStringLiteral("Escena"), sceneNameEdit_);
    transformForm->addRow(QStringLiteral("Nombre"), entityNameEdit_);
    transformForm->addRow(QStringLiteral("Posicion"), makeVectorRow(positionX_, positionY_, positionZ_));
    transformForm->addRow(QStringLiteral("Rotacion"), makeVectorRow(rotationX_, rotationY_, rotationZ_));
    transformForm->addRow(QStringLiteral("Escala"), makeVectorRow(scaleX_, scaleY_, scaleZ_));
    layout->addWidget(transformSection_);

    componentSection_ = makeInspectorSection(QStringLiteral("Componentes"));
    auto* componentLayout = new QVBoxLayout(componentSection_);
    componentSummary_ = new QLabel(QStringLiteral("-"));
    componentSummary_->setWordWrap(true);
    componentSummary_->setObjectName(QStringLiteral("ComponentSummary"));
    componentLayout->addWidget(componentSummary_);
    layout->addWidget(componentSection_);

    materialOverrideSection_ = makeInspectorSection(QStringLiteral("Materiales y texturas"));
    auto* materialLayout = new QVBoxLayout(materialOverrideSection_);
    materialSlotsTable_ = new MaterialSlotsTable;
    materialSlotsTable_->setColumnCount(9);
    materialSlotsTable_->setHorizontalHeaderLabels({
        QStringLiteral("Slot"),
        QStringLiteral("Material actual"),
        QStringLiteral("Base Color"),
        QStringLiteral("Normal"),
        QStringLiteral("Metal/Rough"),
        QStringLiteral("Emissive"),
        QStringLiteral("Tiling"),
        QStringLiteral("Offset"),
        QStringLiteral("Override"),
    });
    materialSlotsTable_->horizontalHeader()->setStretchLastSection(false);
    materialSlotsTable_->horizontalHeader()->setSectionResizeMode(kMaterialSlotColumn, QHeaderView::ResizeToContents);
    materialSlotsTable_->horizontalHeader()->setSectionResizeMode(kMaterialNameColumn, QHeaderView::Stretch);
    materialSlotsTable_->horizontalHeader()->setSectionResizeMode(kBaseColorColumn, QHeaderView::ResizeToContents);
    materialSlotsTable_->horizontalHeader()->setSectionResizeMode(kNormalColumn, QHeaderView::ResizeToContents);
    materialSlotsTable_->horizontalHeader()->setSectionResizeMode(kMetallicRoughnessColumn, QHeaderView::ResizeToContents);
    materialSlotsTable_->horizontalHeader()->setSectionResizeMode(kEmissiveColumn, QHeaderView::ResizeToContents);
    materialSlotsTable_->horizontalHeader()->setSectionResizeMode(kTilingColumn, QHeaderView::ResizeToContents);
    materialSlotsTable_->horizontalHeader()->setSectionResizeMode(kOffsetColumn, QHeaderView::ResizeToContents);
    materialSlotsTable_->horizontalHeader()->setSectionResizeMode(kOverrideColumn, QHeaderView::ResizeToContents);
    materialSlotsTable_->verticalHeader()->setVisible(false);
    materialSlotsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    materialSlotsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    materialSlotsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    materialSlotsTable_->setMinimumHeight(150);
    materialSlotsTable_->setToolTip(QStringLiteral("Arrastra un material o textura desde Proyecto a una celda de slot."));
    static_cast<MaterialSlotsTable*>(materialSlotsTable_)->setDropCallback(
        [this](int row, int column, assets::AssetId assetId, int kind, std::optional<std::uint32_t> subAssetIndex) {
            applyMaterialOverrideDrop(row, column, assetId, kind, subAssetIndex);
        });
    materialLayout->addWidget(materialSlotsTable_);
    auto* materialButtons = new QHBoxLayout;
    resetMaterialOverrideButton_ = makeToolButton(QStringLiteral("Reset override"));
    materialButtons->addWidget(resetMaterialOverrideButton_);
    materialButtons->addStretch();
    materialLayout->addLayout(materialButtons);
    materialOverrideStatus_ = makeMutedLabel(QStringLiteral("Sin renderer de malla seleccionado."));
    materialLayout->addWidget(materialOverrideStatus_);
    layout->addWidget(materialOverrideSection_);

    terrainQuickSection_ = makeInspectorSection(QStringLiteral("Terreno"));
    auto* terrainQuickLayout = new QVBoxLayout(terrainQuickSection_);
    terrainEditSelectionButton_ = makeToolButton(QStringLiteral("Editar terreno"));
    terrainEditSelectionButton_->setObjectName(QStringLiteral("PrimaryButton"));
    terrainQuickStatus_ = makeMutedLabel(QStringLiteral("Modo edicion inactivo."));
    terrainQuickLayout->addWidget(terrainEditSelectionButton_);
    terrainQuickLayout->addWidget(terrainQuickStatus_);
    layout->addWidget(terrainQuickSection_);

    scriptSection_ = makeInspectorSection(QStringLiteral("Script"));
    auto* scriptLayout = new QVBoxLayout(scriptSection_);
    auto* scriptForm = new QFormLayout;
    scriptComponentCombo_ = new QComboBox;
    scriptForm->addRow(QStringLiteral("Componente"), scriptComponentCombo_);
    scriptAssetCombo_ = new QComboBox;
    scriptForm->addRow(QStringLiteral("Script"), scriptAssetCombo_);
    scriptEnabledCheck_ = new QCheckBox(QStringLiteral("Activado"));
    scriptForm->addRow(QStringLiteral("Estado"), scriptEnabledCheck_);
    scriptLayout->addLayout(scriptForm);
    scriptFieldsTable_ = new QTableWidget;
    scriptFieldsTable_->setColumnCount(2);
    scriptFieldsTable_->setHorizontalHeaderLabels({QStringLiteral("Campo"), QStringLiteral("Valor")});
    scriptFieldsTable_->horizontalHeader()->setStretchLastSection(true);
    scriptFieldsTable_->verticalHeader()->setVisible(false);
    scriptFieldsTable_->setMinimumHeight(86);
    scriptLayout->addWidget(scriptFieldsTable_);
    scriptStatusLabel_ = makeMutedLabel();
    scriptLayout->addWidget(scriptStatusLabel_);
    auto* scriptButtons = new QHBoxLayout;
    addScriptButton_ = makeToolButton(QStringLiteral("Agregar script"));
    removeScriptButton_ = makeToolButton(QStringLiteral("Quitar script"));
    scriptButtons->addWidget(addScriptButton_);
    scriptButtons->addWidget(removeScriptButton_);
    scriptLayout->addLayout(scriptButtons);
    layout->addWidget(scriptSection_);

    rigidbodySection_ = makeInspectorSection(QStringLiteral("Cuerpo rigido"));
    auto* rigidbodyForm = new QFormLayout(rigidbodySection_);
    rigidbodyEnabledCheck_ = new QCheckBox(QStringLiteral("Activado"));
    rigidbodyForm->addRow(QStringLiteral("Cuerpo rigido"), rigidbodyEnabledCheck_);
    rigidbodyBodyTypeCombo_ = new QComboBox;
    rigidbodyBodyTypeCombo_->addItems({QStringLiteral("Dinamico"), QStringLiteral("Kinematic"), QStringLiteral("Estatico")});
    rigidbodyForm->addRow(QStringLiteral("Tipo de cuerpo"), rigidbodyBodyTypeCombo_);
    rigidbodyMass_ = createTransformSpinBox();
    rigidbodyMass_->setRange(0.001, 100000.0);
    rigidbodyMass_->setValue(1.0);
    rigidbodyForm->addRow(QStringLiteral("Masa"), rigidbodyMass_);
    rigidbodyGravityCheck_ = new QCheckBox(QStringLiteral("Usar gravedad"));
    rigidbodyForm->addRow(QStringLiteral("Gravedad"), rigidbodyGravityCheck_);
    rigidbodyLinearDrag_ = createTransformSpinBox();
    rigidbodyLinearDrag_->setRange(0.0, 10000.0);
    rigidbodyForm->addRow(QStringLiteral("Amortiguacion lineal"), rigidbodyLinearDrag_);
    rigidbodyAngularDrag_ = createTransformSpinBox();
    rigidbodyAngularDrag_->setRange(0.0, 10000.0);
    rigidbodyForm->addRow(QStringLiteral("Amortiguacion angular"), rigidbodyAngularDrag_);
    layout->addWidget(rigidbodySection_);

    colliderSection_ = makeInspectorSection(QStringLiteral("Colisionador"));
    auto* colliderLayout = new QVBoxLayout(colliderSection_);
    auto* colliderForm = new QFormLayout;
    colliderEnabledCheck_ = new QCheckBox(QStringLiteral("Activado"));
    colliderForm->addRow(QStringLiteral("Colisionador"), colliderEnabledCheck_);
    colliderShapeCombo_ = new QComboBox;
    colliderShapeCombo_->addItems({QStringLiteral("Caja"), QStringLiteral("Esfera"), QStringLiteral("Capsula"), QStringLiteral("Mesh"), QStringLiteral("Terreno")});
    colliderForm->addRow(QStringLiteral("Forma"), colliderShapeCombo_);
    colliderForm->addRow(QStringLiteral("Tamano"), makeVectorRow(colliderSizeX_, colliderSizeY_, colliderSizeZ_));
    colliderForm->addRow(QStringLiteral("Offset"), makeVectorRow(colliderOffsetX_, colliderOffsetY_, colliderOffsetZ_));
    colliderRadius_ = createTransformSpinBox();
    colliderRadius_->setRange(0.001, 100000.0);
    colliderForm->addRow(QStringLiteral("Radio"), colliderRadius_);
    colliderHeight_ = createTransformSpinBox();
    colliderHeight_->setRange(0.001, 100000.0);
    colliderForm->addRow(QStringLiteral("Altura"), colliderHeight_);
    colliderTriggerCheck_ = new QCheckBox(QStringLiteral("Es trigger"));
    colliderForm->addRow(QStringLiteral("Trigger"), colliderTriggerCheck_);
    colliderLayout->addLayout(colliderForm);
    showCollidersButton_ = makeToolButton(QStringLiteral("Ver colisionadores"));
    showCollidersButton_->setEnabled(false);
    showCollidersButton_->setToolTip(QStringLiteral("Overlay de colisionadores no disponible en esta fase."));
    physicsStatusLabel_ = makeMutedLabel(QStringLiteral("Overlay de colisionadores pendiente. La UI no activa una vista falsa."));
    colliderLayout->addWidget(showCollidersButton_);
    colliderLayout->addWidget(physicsStatusLabel_);
    layout->addWidget(colliderSection_);

    auto* addComponent = makeToolButton(QStringLiteral("Agregar componente"));
    addComponent->setObjectName(QStringLiteral("PrimaryButton"));
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
    connect(addScriptButton_, &QPushButton::clicked, this, [this] {
        showAddComponentMenu(addScriptButton_);
    });
    connect(resetMaterialOverrideButton_, &QPushButton::clicked, this, [this] {
        resetSelectedMaterialOverride();
    });
    connect(terrainEditSelectionButton_, &QPushButton::clicked, this, [this] {
        if (terrainBrushEnabled_ != nullptr) {
            terrainBrushEnabled_->setChecked(true);
        }
        syncTerrainPanelFromSelection();
        statusBar()->showMessage(QStringLiteral("Modo de edicion de terreno activo"));
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

    scroll->setWidget(panel);
    return scroll;
}

QWidget* MainWindow::createProjectPanel()
{
    auto* tabs = new QTabWidget;
    configureTabWidget(*tabs);
    auto* assetsPanel = new QWidget;
    auto* assetsLayout = new QVBoxLayout(assetsPanel);
    assetsLayout->setContentsMargins(8, 8, 8, 8);
    assetsLayout->setSpacing(8);
    auto* importButton = makeToolButton(QStringLiteral("Importar asset"));
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
    addEditorTab(*tabs, assetsPanel, QStringLiteral("Proyecto"), QStringLiteral("Proyecto / Assets"));
    addEditorTab(
        *tabs,
        createTextPanel(QStringLiteral("Paquetes"), {QStringLiteral("Paquetes del motor"), QStringLiteral("Paquetes de terceros")}),
        QStringLiteral("Paquetes"));
    return tabs;
}

QWidget* MainWindow::createAssetImportPanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    auto* header = new QLabel(QStringLiteral("Importar assets"));
    header->setObjectName(QStringLiteral("PanelHeader"));
    layout->addWidget(header);

    assetImportStatus_ = new QLabel(QStringLiteral("En espera"));
    assetImportStatus_->setWordWrap(true);
    layout->addWidget(assetImportStatus_);

    assetImportProgress_ = new QProgressBar;
    assetImportProgress_->setRange(0, 100);
    assetImportProgress_->setValue(0);
    assetImportProgress_->setFormat(QStringLiteral("En espera"));
    layout->addWidget(assetImportProgress_);

    layout->addWidget(createTextPanel(
        QStringLiteral("Pipeline"),
        {
            QStringLiteral("Leer archivo fuente"),
            QStringLiteral("Validar modelo o textura"),
            QStringLiteral("Procesar mallas y materiales"),
            QStringLiteral("Escribir cache de assets"),
        }));
    return panel;
}

QWidget* MainWindow::createBottomPanel()
{
    auto* tabs = new QTabWidget;
    configureTabWidget(*tabs);

    consoleView_ = new QPlainTextEdit;
    consoleView_->setReadOnly(true);
    consoleView_->setMaximumBlockCount(5000);

    addEditorTab(*tabs, consoleView_, QStringLiteral("Consola"));
    addEditorTab(*tabs, createProfilerPanel(), QStringLiteral("Perfilador"));
    addEditorTab(
        *tabs,
        createTextPanel(QStringLiteral("Red"), {QStringLiteral("Conexion"), QStringLiteral("Ping"), QStringLiteral("Paquetes rechazados")}),
        QStringLiteral("Red"));
    return tabs;
}

QWidget* MainWindow::createProfilerPanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);

    auto* header = new QLabel(QStringLiteral("Perfilador"));
    header->setObjectName(QStringLiteral("PanelHeader"));
    layout->addWidget(header);

    profilerTable_ = new QTableWidget(55, 2);
    profilerTable_->setHorizontalHeaderLabels({QStringLiteral("Metrica"), QStringLiteral("Valor")});
    profilerTable_->horizontalHeader()->setStretchLastSection(true);
    profilerTable_->verticalHeader()->setVisible(false);
    profilerTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    profilerTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    const QStringList rows {
        QStringLiteral("GPU / fuente del perfilador"),
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
