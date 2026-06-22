#include <projectunity/editor/MainWindow.hpp>

#include <projectunity/editor/ProjectAssetTreeWidget.hpp>
#include <projectunity/editor/ProjectBrowserWidget.hpp>
#include <projectunity/editor/SceneHierarchyWidget.hpp>
#include <projectunity/editor/SettingsDialog.hpp>
#include <projectunity/editor/ViewportTuningImGuiWindow.hpp>
#include <projectunity/editor/ViewportWidget.hpp>
#include <projectunity/renderer/IRenderer.hpp>

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
#include <QSlider>
#include <QSpinBox>
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

[[nodiscard]] int comboData(const QComboBox* combo, int fallback)
{
    if (combo == nullptr || combo->currentIndex() < 0) {
        return fallback;
    }
    return combo->currentData().toInt();
}

void setComboData(QComboBox* combo, int value)
{
    if (combo == nullptr) {
        return;
    }
    const auto index = combo->findData(value);
    combo->setCurrentIndex(index < 0 ? 0 : index);
}

[[nodiscard]] QWidget* makeSliderRow(QSlider*& slider, QLabel*& valueLabel, int minimum, int maximum)
{
    auto* row = new QWidget;
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    slider = new QSlider(Qt::Horizontal);
    slider->setRange(minimum, maximum);
    slider->setTracking(true);
    slider->setSingleStep(1);
    slider->setPageStep(std::max(1, (maximum - minimum) / 20));
    slider->setMinimumWidth(180);
    valueLabel = new QLabel(QStringLiteral("-"));
    valueLabel->setMinimumWidth(72);
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(slider, 1);
    layout->addWidget(valueLabel);
    return row;
}

[[nodiscard]] QSpinBox* makeBudgetSpinBox()
{
    auto* spin = new QSpinBox;
    spin->setRange(1, 1000000);
    spin->setSingleStep(8);
    spin->setAccelerated(true);
    spin->setKeyboardTracking(false);
    return spin;
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
    editMenu->addAction(QStringLiteral("Preferencias"), this, [this] {
        const auto* stats = sceneViewport_ != nullptr
            ? sceneViewport_->lastRendererStats()
            : (renderer_ != nullptr && renderer_->isReady() ? &renderer_->stats() : nullptr);
        SettingsDialog dialog(qualitySettings_, stats, this);
        connect(&dialog, &SettingsDialog::settingsApplied, this, [this](const EditorQualitySettings& settings) {
            applyEditorQualitySettings(settings, true);
            statusBar()->showMessage(QStringLiteral("Ajustes aplicados"), 2500);
        });
        dialog.exec();
    });

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
    toolsMenu->addAction(QStringLiteral("Viewport Tuning (Dear ImGui)"), this, [this] {
        showViewportTuningWindow();
    });
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
    auto* optimizationDock = createDockWidget(QStringLiteral("Optimization Studio"), createOptimizationEditorPanel());
    optimizationDock_ = optimizationDock;
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
    dockManager_->addDockWidget(ads::CenterDockWidgetArea, optimizationDock, centerArea);
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

void MainWindow::focusOptimizationStudio()
{
    if (optimizationDock_ == nullptr) {
        return;
    }
    optimizationDock_->toggleView(true);
    optimizationDock_->setAsCurrentTab();
    optimizationDock_->raise();
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
    applyQualitySettingsToViewport(sceneViewport_);
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
    applyQualitySettingsToViewport(gameViewport_);
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

QWidget* MainWindow::createViewportTuningPanel()
{
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);

    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(10);

    auto* header = new QLabel(QStringLiteral("Viewport Tuning"));
    header->setObjectName(QStringLiteral("PanelHeader"));
    layout->addWidget(header);
    layout->addWidget(makeMutedLabel(QStringLiteral("Ajustes vivos para diagnosticar popping, culling, HLOD y sombras sin recompilar.")));

    auto* presetGroup = makeInspectorSection(QStringLiteral("Presets rapidos"));
    auto* presetLayout = new QHBoxLayout(presetGroup);
    auto* safeNearButton = makeToolButton(QStringLiteral("Seguro cerca"));
    auto* noCullButton = makeToolButton(QStringLiteral("Sin culling"));
    auto* balancedButton = makeToolButton(QStringLiteral("Balanceado"));
    presetLayout->addWidget(safeNearButton);
    presetLayout->addWidget(noCullButton);
    presetLayout->addWidget(balancedButton);
    layout->addWidget(presetGroup);

    auto* lodGroup = makeInspectorSection(QStringLiteral("LOD / HLOD"));
    auto* lodForm = new QFormLayout(lodGroup);
    lodForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    tuningHlodCheck_ = new QCheckBox(QStringLiteral("Activado"));
    lodForm->addRow(QStringLiteral("HLOD"), tuningHlodCheck_);

    tuningLodQualityCombo_ = new QComboBox;
    tuningLodQualityCombo_->addItem(QStringLiteral("Rendimiento"), static_cast<int>(LodQuality::Performance));
    tuningLodQualityCombo_->addItem(QStringLiteral("Balanceado"), static_cast<int>(LodQuality::Balanced));
    tuningLodQualityCombo_->addItem(QStringLiteral("Calidad"), static_cast<int>(LodQuality::Quality));
    tuningLodQualityCombo_->addItem(QStringLiteral("Ultra"), static_cast<int>(LodQuality::Ultra));
    lodForm->addRow(QStringLiteral("Calidad LOD"), tuningLodQualityCombo_);

    tuningHlodAggressivenessCombo_ = new QComboBox;
    tuningHlodAggressivenessCombo_->addItem(QStringLiteral("Baja"), static_cast<int>(HlodAggressiveness::Low));
    tuningHlodAggressivenessCombo_->addItem(QStringLiteral("Media"), static_cast<int>(HlodAggressiveness::Medium));
    tuningHlodAggressivenessCombo_->addItem(QStringLiteral("Alta"), static_cast<int>(HlodAggressiveness::High));
    lodForm->addRow(QStringLiteral("Agresividad"), tuningHlodAggressivenessCombo_);

    tuningHlodOverrideCombo_ = new QComboBox;
    tuningHlodOverrideCombo_->addItem(QStringLiteral("Automatico"), static_cast<int>(ViewportHlodDebugOverride::Automatic));
    tuningHlodOverrideCombo_->addItem(QStringLiteral("Forzar detallado"), static_cast<int>(ViewportHlodDebugOverride::ForceDetailed));
    tuningHlodOverrideCombo_->addItem(QStringLiteral("Forzar HLOD"), static_cast<int>(ViewportHlodDebugOverride::ForceHlod));
    lodForm->addRow(QStringLiteral("Override"), tuningHlodOverrideCombo_);

    lodForm->addRow(
        QStringLiteral("Distancia"),
        makeSliderRow(tuningLodDistanceSlider_, tuningLodDistanceValue_, 0, 250));
    tuningLodDistanceSlider_->setToolTip(QStringLiteral("Sube este valor si los assets pasan a HLOD demasiado cerca."));

    lodForm->addRow(
        QStringLiteral("Screen error"),
        makeSliderRow(tuningScreenErrorSlider_, tuningScreenErrorValue_, 5, 400));
    tuningScreenErrorSlider_->setToolTip(QStringLiteral("Baja este valor si el HLOD aparece agresivamente o con popping visible."));

    lodForm->addRow(
        QStringLiteral("Hysteresis"),
        makeSliderRow(tuningHysteresisSlider_, tuningHysteresisValue_, 0, 45));
    tuningHysteresisSlider_->setToolTip(QStringLiteral("Sube este valor para reducir cambios de LOD al mover la camara."));

    tuningChunkBudgetSpin_ = makeBudgetSpinBox();
    lodForm->addRow(QStringLiteral("Chunk budget"), tuningChunkBudgetSpin_);
    tuningDrawPacketBudgetSpin_ = makeBudgetSpinBox();
    lodForm->addRow(QStringLiteral("Draw packets"), tuningDrawPacketBudgetSpin_);
    tuningShadowCasterBudgetSpin_ = makeBudgetSpinBox();
    lodForm->addRow(QStringLiteral("Shadow casters"), tuningShadowCasterBudgetSpin_);
    layout->addWidget(lodGroup);

    auto* cullingGroup = makeInspectorSection(QStringLiteral("Culling"));
    auto* cullingForm = new QFormLayout(cullingGroup);
    cullingForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    tuningOcclusionCullingCheck_ = new QCheckBox(QStringLiteral("Rechazar chunks ocultos"));
    tuningOcclusionCullingCheck_->setToolTip(QStringLiteral("Apagalo si algo desaparece al mirar hacia assets o entre estructuras."));
    cullingForm->addRow(QStringLiteral("Occlusion"), tuningOcclusionCullingCheck_);
    tuningSpatialCullingCheck_ = new QCheckBox(QStringLiteral("Usar grilla espacial"));
    tuningSpatialCullingCheck_->setToolTip(QStringLiteral("Apagalo para probar todos los chunks y descartar errores de spatial cells."));
    cullingForm->addRow(QStringLiteral("Spatial cells"), tuningSpatialCullingCheck_);
    cullingForm->addRow(
        QStringLiteral("Bounds padding"),
        makeSliderRow(tuningCullingPaddingSlider_, tuningCullingPaddingValue_, 0, 500));
    tuningCullingPaddingSlider_->setToolTip(QStringLiteral("Inflado conservador de bounds usado solo para culling. Sube si algo desaparece muy cerca."));
    layout->addWidget(cullingGroup);

    auto* terrainGroup = makeInspectorSection(QStringLiteral("Terrain cerca"));
    auto* terrainForm = new QFormLayout(terrainGroup);
    terrainForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    tuningTerrainNearQualityCheck_ = new QCheckBox(QStringLiteral("Forzar detalle cerca"));
    terrainForm->addRow(QStringLiteral("LOD0 cerca"), tuningTerrainNearQualityCheck_);
    terrainForm->addRow(
        QStringLiteral("Radio cerca"),
        makeSliderRow(tuningTerrainNearRadiusSlider_, tuningTerrainNearRadiusValue_, 0, 500));
    tuningTerrainNearRadiusSlider_->setToolTip(QStringLiteral("Radio en metros para mantener terrain detallado cerca de la camara."));
    tuningDisableTerrainHlodCheck_ = new QCheckBox(QStringLiteral("No usar HLOD terrain"));
    tuningDisableTerrainChunkLodCheck_ = new QCheckBox(QStringLiteral("No degradar chunks terrain"));
    terrainForm->addRow(QStringLiteral("Disable HLOD"), tuningDisableTerrainHlodCheck_);
    terrainForm->addRow(QStringLiteral("Disable chunk LOD"), tuningDisableTerrainChunkLodCheck_);
    layout->addWidget(terrainGroup);

    auto* shadowGroup = makeInspectorSection(QStringLiteral("Sombras"));
    auto* shadowForm = new QFormLayout(shadowGroup);
    tuningShadowModeCombo_ = new QComboBox;
    tuningShadowModeCombo_->addItem(QStringLiteral("Off"), static_cast<int>(renderer::RenderShadowUpdateMode::Off));
    tuningShadowModeCombo_->addItem(QStringLiteral("Live"), static_cast<int>(renderer::RenderShadowUpdateMode::Live));
    tuningShadowModeCombo_->addItem(QStringLiteral("Frozen"), static_cast<int>(renderer::RenderShadowUpdateMode::Frozen));
    shadowForm->addRow(QStringLiteral("Modo"), tuningShadowModeCombo_);
    layout->addWidget(shadowGroup);

    auto* debugGroup = makeInspectorSection(QStringLiteral("Overlays"));
    auto* debugLayout = new QVBoxLayout(debugGroup);
    tuningLodColorsCheck_ = new QCheckBox(QStringLiteral("Colores LOD / HLOD"));
    tuningShadowCastersCheck_ = new QCheckBox(QStringLiteral("Shadow casters"));
    tuningBoundsXrayCheck_ = new QCheckBox(QStringLiteral("Bounds / XRay"));
    tuningSourceObjectsCheck_ = new QCheckBox(QStringLiteral("Source objects"));
    tuningSunDirectionCheck_ = new QCheckBox(QStringLiteral("Direccion del sol"));
    debugLayout->addWidget(tuningLodColorsCheck_);
    debugLayout->addWidget(tuningShadowCastersCheck_);
    debugLayout->addWidget(tuningBoundsXrayCheck_);
    debugLayout->addWidget(tuningSourceObjectsCheck_);
    debugLayout->addWidget(tuningSunDirectionCheck_);
    layout->addWidget(debugGroup);

    auto* statsGroup = makeInspectorSection(QStringLiteral("Frame actual"));
    auto* statsLayout = new QVBoxLayout(statsGroup);
    tuningStatsLabel_ = makeMutedLabel(QStringLiteral("Sin frame Vulkan todavia."));
    statsLayout->addWidget(tuningStatsLabel_);
    layout->addWidget(statsGroup);
    layout->addStretch();

    const auto apply = [this]() {
        applyViewportTuningFromControls();
    };
    connect(safeNearButton, &QPushButton::clicked, this, [this] {
        auto next = qualitySettings_;
        next.graphics.preset = QualityPreset::Custom;
        next.lod.hlodEnabled = false;
        next.lod.debugOverride = ViewportHlodDebugOverride::ForceDetailed;
        next.lod.quality = LodQuality::Ultra;
        next.lod.aggressiveness = HlodAggressiveness::Low;
        next.lod.lodDistance = 80.0F;
        next.lod.screenError = 0.50F;
        next.lod.hysteresis = 0.30F;
        next.lod.chunkBudget = std::max(next.lod.chunkBudget, 256);
        next.lod.drawPacketBudget = std::max(next.lod.drawPacketBudget, 1024);
        next.lod.shadowCasterBudget = std::max(next.lod.shadowCasterBudget, 1024);
        next.lod.cullingBoundsPadding = 1.0F;
        next.lod.occlusionCullingEnabled = false;
        next.lod.spatialCellCullingEnabled = true;
        next.lod.terrainNearHighQualityEnabled = true;
        next.lod.terrainNearHighQualityRadius = std::max(next.lod.terrainNearHighQualityRadius, 120.0F);
        next.lod.debugDisableTerrainHlod = true;
        next.lod.debugDisableTerrainChunkLod = true;
        next.debug.boundsXray = true;
        next.debug.lodColors = true;
        next.lod.debugColors = true;
        applyEditorQualitySettings(std::move(next), true);
    });
    connect(noCullButton, &QPushButton::clicked, this, [this] {
        auto next = qualitySettings_;
        next.graphics.preset = QualityPreset::Custom;
        next.lod.hlodEnabled = false;
        next.lod.debugOverride = ViewportHlodDebugOverride::ForceDetailed;
        next.lod.occlusionCullingEnabled = false;
        next.lod.spatialCellCullingEnabled = false;
        next.lod.cullingBoundsPadding = 1.5F;
        next.lod.chunkBudget = std::max(next.lod.chunkBudget, 512);
        next.lod.drawPacketBudget = std::max(next.lod.drawPacketBudget, 2048);
        next.lod.shadowCasterBudget = std::max(next.lod.shadowCasterBudget, 2048);
        next.debug.boundsXray = true;
        next.debug.sourceObjects = true;
        applyEditorQualitySettings(std::move(next), true);
    });
    connect(balancedButton, &QPushButton::clicked, this, [this] {
        applyOptimizationBalancedMode();
    });
    connect(tuningHlodCheck_, &QCheckBox::toggled, this, [apply](bool) { apply(); });
    connect(tuningLodQualityCombo_, &QComboBox::currentIndexChanged, this, [apply](int) { apply(); });
    connect(tuningHlodAggressivenessCombo_, &QComboBox::currentIndexChanged, this, [apply](int) { apply(); });
    connect(tuningHlodOverrideCombo_, &QComboBox::currentIndexChanged, this, [apply](int) { apply(); });
    connect(tuningLodDistanceSlider_, &QSlider::valueChanged, this, [this, apply](int value) {
        if (tuningLodDistanceValue_ != nullptr) {
            tuningLodDistanceValue_->setText(QStringLiteral("%1 m").arg(value));
        }
        apply();
    });
    connect(tuningScreenErrorSlider_, &QSlider::valueChanged, this, [this, apply](int value) {
        if (tuningScreenErrorValue_ != nullptr) {
            tuningScreenErrorValue_->setText(QStringLiteral("%1").arg(static_cast<double>(value) / 100.0, 0, 'f', 2));
        }
        apply();
    });
    connect(tuningHysteresisSlider_, &QSlider::valueChanged, this, [this, apply](int value) {
        if (tuningHysteresisValue_ != nullptr) {
            tuningHysteresisValue_->setText(QStringLiteral("%1").arg(static_cast<double>(value) / 100.0, 0, 'f', 2));
        }
        apply();
    });
    connect(tuningChunkBudgetSpin_, &QSpinBox::valueChanged, this, [apply](int) { apply(); });
    connect(tuningDrawPacketBudgetSpin_, &QSpinBox::valueChanged, this, [apply](int) { apply(); });
    connect(tuningShadowCasterBudgetSpin_, &QSpinBox::valueChanged, this, [apply](int) { apply(); });
    connect(tuningOcclusionCullingCheck_, &QCheckBox::toggled, this, [apply](bool) { apply(); });
    connect(tuningSpatialCullingCheck_, &QCheckBox::toggled, this, [apply](bool) { apply(); });
    connect(tuningCullingPaddingSlider_, &QSlider::valueChanged, this, [this, apply](int value) {
        if (tuningCullingPaddingValue_ != nullptr) {
            tuningCullingPaddingValue_->setText(QStringLiteral("%1 m").arg(static_cast<double>(value) / 100.0, 0, 'f', 2));
        }
        apply();
    });
    connect(tuningTerrainNearQualityCheck_, &QCheckBox::toggled, this, [apply](bool) { apply(); });
    connect(tuningTerrainNearRadiusSlider_, &QSlider::valueChanged, this, [this, apply](int value) {
        if (tuningTerrainNearRadiusValue_ != nullptr) {
            tuningTerrainNearRadiusValue_->setText(QStringLiteral("%1 m").arg(value));
        }
        apply();
    });
    connect(tuningDisableTerrainHlodCheck_, &QCheckBox::toggled, this, [apply](bool) { apply(); });
    connect(tuningDisableTerrainChunkLodCheck_, &QCheckBox::toggled, this, [apply](bool) { apply(); });
    connect(tuningShadowModeCombo_, &QComboBox::currentIndexChanged, this, [apply](int) { apply(); });
    connect(tuningLodColorsCheck_, &QCheckBox::toggled, this, [apply](bool) { apply(); });
    connect(tuningShadowCastersCheck_, &QCheckBox::toggled, this, [apply](bool) { apply(); });
    connect(tuningBoundsXrayCheck_, &QCheckBox::toggled, this, [apply](bool) { apply(); });
    connect(tuningSourceObjectsCheck_, &QCheckBox::toggled, this, [apply](bool) { apply(); });
    connect(tuningSunDirectionCheck_, &QCheckBox::toggled, this, [apply](bool) { apply(); });

    scroll->setWidget(panel);
    syncViewportTuningPanel();
    return scroll;
}

void MainWindow::applyViewportTuningFromControls()
{
    if (viewportTuningUpdating_) {
        return;
    }

    auto next = qualitySettings_;
    next.graphics.preset = QualityPreset::Custom;
    if (tuningHlodCheck_ != nullptr) {
        next.lod.hlodEnabled = tuningHlodCheck_->isChecked();
    }
    if (tuningLodQualityCombo_ != nullptr) {
        next.lod.quality = static_cast<LodQuality>(
            comboData(tuningLodQualityCombo_, static_cast<int>(next.lod.quality)));
    }
    if (tuningHlodAggressivenessCombo_ != nullptr) {
        next.lod.aggressiveness = static_cast<HlodAggressiveness>(
            comboData(tuningHlodAggressivenessCombo_, static_cast<int>(next.lod.aggressiveness)));
    }
    if (tuningHlodOverrideCombo_ != nullptr) {
        next.lod.debugOverride = static_cast<ViewportHlodDebugOverride>(
            comboData(tuningHlodOverrideCombo_, static_cast<int>(next.lod.debugOverride)));
    }
    if (tuningLodDistanceSlider_ != nullptr) {
        next.lod.lodDistance = static_cast<float>(tuningLodDistanceSlider_->value());
    }
    if (tuningScreenErrorSlider_ != nullptr) {
        next.lod.screenError = static_cast<float>(tuningScreenErrorSlider_->value()) / 100.0F;
    }
    if (tuningHysteresisSlider_ != nullptr) {
        next.lod.hysteresis = static_cast<float>(tuningHysteresisSlider_->value()) / 100.0F;
    }
    if (tuningChunkBudgetSpin_ != nullptr) {
        next.lod.chunkBudget = tuningChunkBudgetSpin_->value();
    }
    if (tuningDrawPacketBudgetSpin_ != nullptr) {
        next.lod.drawPacketBudget = tuningDrawPacketBudgetSpin_->value();
    }
    if (tuningShadowCasterBudgetSpin_ != nullptr) {
        next.lod.shadowCasterBudget = tuningShadowCasterBudgetSpin_->value();
    }
    if (tuningOcclusionCullingCheck_ != nullptr) {
        next.lod.occlusionCullingEnabled = tuningOcclusionCullingCheck_->isChecked();
    }
    if (tuningSpatialCullingCheck_ != nullptr) {
        next.lod.spatialCellCullingEnabled = tuningSpatialCullingCheck_->isChecked();
    }
    if (tuningCullingPaddingSlider_ != nullptr) {
        next.lod.cullingBoundsPadding = static_cast<float>(tuningCullingPaddingSlider_->value()) / 100.0F;
    }
    if (tuningTerrainNearQualityCheck_ != nullptr) {
        next.lod.terrainNearHighQualityEnabled = tuningTerrainNearQualityCheck_->isChecked();
    }
    if (tuningTerrainNearRadiusSlider_ != nullptr) {
        next.lod.terrainNearHighQualityRadius = static_cast<float>(tuningTerrainNearRadiusSlider_->value());
    }
    if (tuningDisableTerrainHlodCheck_ != nullptr) {
        next.lod.debugDisableTerrainHlod = tuningDisableTerrainHlodCheck_->isChecked();
    }
    if (tuningDisableTerrainChunkLodCheck_ != nullptr) {
        next.lod.debugDisableTerrainChunkLod = tuningDisableTerrainChunkLodCheck_->isChecked();
    }
    if (tuningShadowModeCombo_ != nullptr) {
        next.shadow.updateMode = static_cast<renderer::RenderShadowUpdateMode>(
            comboData(tuningShadowModeCombo_, static_cast<int>(next.shadow.updateMode)));
        next.shadow.quality = next.shadow.updateMode == renderer::RenderShadowUpdateMode::Off
            ? ShadowQuality::Off
            : (next.shadow.quality == ShadowQuality::Off ? ShadowQuality::High : next.shadow.quality);
    }
    if (tuningLodColorsCheck_ != nullptr) {
        next.lod.debugColors = tuningLodColorsCheck_->isChecked();
        next.debug.lodColors = next.lod.debugColors;
    }
    if (tuningShadowCastersCheck_ != nullptr) {
        next.debug.shadowCasters = tuningShadowCastersCheck_->isChecked();
    }
    if (tuningBoundsXrayCheck_ != nullptr) {
        next.debug.boundsXray = tuningBoundsXrayCheck_->isChecked();
    }
    if (tuningSourceObjectsCheck_ != nullptr) {
        next.debug.sourceObjects = tuningSourceObjectsCheck_->isChecked();
    }
    if (tuningSunDirectionCheck_ != nullptr) {
        next.debug.sunDirection = tuningSunDirectionCheck_->isChecked();
    }

    applyEditorQualitySettings(std::move(next), true);
}

void MainWindow::syncViewportTuningPanel()
{
    if (viewportTuningImGuiWindow_ != nullptr) {
        viewportTuningImGuiWindow_->syncSettings(qualitySettings_);
    }
    if (tuningHlodCheck_ == nullptr) {
        return;
    }

    viewportTuningUpdating_ = true;
    const auto clearUpdating = [this]() {
        viewportTuningUpdating_ = false;
    };

    const QSignalBlocker blockHlod(tuningHlodCheck_);
    const QSignalBlocker blockLodQuality(tuningLodQualityCombo_);
    const QSignalBlocker blockAggressiveness(tuningHlodAggressivenessCombo_);
    const QSignalBlocker blockOverride(tuningHlodOverrideCombo_);
    const QSignalBlocker blockDistance(tuningLodDistanceSlider_);
    const QSignalBlocker blockScreen(tuningScreenErrorSlider_);
    const QSignalBlocker blockHysteresis(tuningHysteresisSlider_);
    const QSignalBlocker blockChunk(tuningChunkBudgetSpin_);
    const QSignalBlocker blockPackets(tuningDrawPacketBudgetSpin_);
    const QSignalBlocker blockShadowCasters(tuningShadowCasterBudgetSpin_);
    const QSignalBlocker blockOcclusion(tuningOcclusionCullingCheck_);
    const QSignalBlocker blockSpatial(tuningSpatialCullingCheck_);
    const QSignalBlocker blockPadding(tuningCullingPaddingSlider_);
    const QSignalBlocker blockTerrainNear(tuningTerrainNearQualityCheck_);
    const QSignalBlocker blockTerrainRadius(tuningTerrainNearRadiusSlider_);
    const QSignalBlocker blockTerrainHlod(tuningDisableTerrainHlodCheck_);
    const QSignalBlocker blockTerrainChunkLod(tuningDisableTerrainChunkLodCheck_);
    const QSignalBlocker blockShadowMode(tuningShadowModeCombo_);
    const QSignalBlocker blockLodColors(tuningLodColorsCheck_);
    const QSignalBlocker blockShadowOverlay(tuningShadowCastersCheck_);
    const QSignalBlocker blockBounds(tuningBoundsXrayCheck_);
    const QSignalBlocker blockSourceObjects(tuningSourceObjectsCheck_);
    const QSignalBlocker blockSun(tuningSunDirectionCheck_);

    tuningHlodCheck_->setChecked(qualitySettings_.lod.hlodEnabled);
    setComboData(tuningLodQualityCombo_, static_cast<int>(qualitySettings_.lod.quality));
    setComboData(tuningHlodAggressivenessCombo_, static_cast<int>(qualitySettings_.lod.aggressiveness));
    setComboData(tuningHlodOverrideCombo_, static_cast<int>(qualitySettings_.lod.debugOverride));
    tuningLodDistanceSlider_->setValue(std::clamp(static_cast<int>(qualitySettings_.lod.lodDistance), 0, 250));
    tuningLodDistanceValue_->setText(QStringLiteral("%1 m").arg(tuningLodDistanceSlider_->value()));
    tuningScreenErrorSlider_->setValue(std::clamp(static_cast<int>(qualitySettings_.lod.screenError * 100.0F), 5, 400));
    tuningScreenErrorValue_->setText(QStringLiteral("%1").arg(qualitySettings_.lod.screenError, 0, 'f', 2));
    tuningHysteresisSlider_->setValue(std::clamp(static_cast<int>(qualitySettings_.lod.hysteresis * 100.0F), 0, 45));
    tuningHysteresisValue_->setText(QStringLiteral("%1").arg(qualitySettings_.lod.hysteresis, 0, 'f', 2));
    tuningChunkBudgetSpin_->setValue(qualitySettings_.lod.chunkBudget);
    tuningDrawPacketBudgetSpin_->setValue(qualitySettings_.lod.drawPacketBudget);
    tuningShadowCasterBudgetSpin_->setValue(qualitySettings_.lod.shadowCasterBudget);
    tuningOcclusionCullingCheck_->setChecked(qualitySettings_.lod.occlusionCullingEnabled);
    tuningSpatialCullingCheck_->setChecked(qualitySettings_.lod.spatialCellCullingEnabled);
    tuningCullingPaddingSlider_->setValue(std::clamp(static_cast<int>(qualitySettings_.lod.cullingBoundsPadding * 100.0F), 0, 500));
    tuningCullingPaddingValue_->setText(QStringLiteral("%1 m").arg(
        static_cast<double>(tuningCullingPaddingSlider_->value()) / 100.0,
        0,
        'f',
        2));
    tuningTerrainNearQualityCheck_->setChecked(qualitySettings_.lod.terrainNearHighQualityEnabled);
    tuningTerrainNearRadiusSlider_->setValue(std::clamp(static_cast<int>(qualitySettings_.lod.terrainNearHighQualityRadius), 0, 500));
    tuningTerrainNearRadiusValue_->setText(QStringLiteral("%1 m").arg(tuningTerrainNearRadiusSlider_->value()));
    tuningDisableTerrainHlodCheck_->setChecked(qualitySettings_.lod.debugDisableTerrainHlod);
    tuningDisableTerrainChunkLodCheck_->setChecked(qualitySettings_.lod.debugDisableTerrainChunkLod);
    setComboData(tuningShadowModeCombo_, static_cast<int>(qualitySettings_.shadow.updateMode));
    tuningLodColorsCheck_->setChecked(qualitySettings_.debug.lodColors || qualitySettings_.lod.debugColors);
    tuningShadowCastersCheck_->setChecked(qualitySettings_.debug.shadowCasters);
    tuningBoundsXrayCheck_->setChecked(qualitySettings_.debug.boundsXray);
    tuningSourceObjectsCheck_->setChecked(qualitySettings_.debug.sourceObjects);
    tuningSunDirectionCheck_->setChecked(qualitySettings_.debug.sunDirection);

    clearUpdating();
    updateViewportTuningStats();
}

void MainWindow::updateViewportTuningStats()
{
    if (tuningStatsLabel_ == nullptr) {
        return;
    }
    const auto* stats = sceneViewport_ != nullptr ? sceneViewport_->lastRendererStats() : nullptr;
    if (stats == nullptr && renderer_ != nullptr && renderer_->isReady()) {
        stats = &renderer_->stats();
    }
    if (stats == nullptr) {
        tuningStatsLabel_->setText(QStringLiteral("Sin frame Vulkan todavia."));
        return;
    }
    tuningStatsLabel_->setText(QStringLiteral(
        "FPS %1 | chunks %2/%3 final %4 | draws %5/%6 culled %7 | HLOD %8 %9 reason %10 | OCC %11 %12/%13 | CELL %14 rej %15 | SH %16/%17 rejected %18")
        .arg(stats->FPS, 0, 'f', 1)
        .arg(static_cast<qulonglong>(stats->lastFrameVisibleRenderChunkCount))
        .arg(static_cast<qulonglong>(stats->lastFrameRenderChunkCount))
        .arg(static_cast<qulonglong>(stats->lastFrameFinalVisibleChunkCount))
        .arg(static_cast<qulonglong>(stats->lastFrameMeshDrawCount))
        .arg(static_cast<qulonglong>(stats->lastFrameCandidateMeshDrawCount))
        .arg(static_cast<qulonglong>(stats->lastFrameCulledMeshDrawCount))
        .arg(qualitySettings_.lod.hlodEnabled ? QStringLiteral("on") : QStringLiteral("off"))
        .arg(static_cast<qulonglong>(stats->lastFrameHlodMeshDrawCount))
        .arg(QString::fromStdString(stats->lastFrameHlodReason.empty() ? "inactive" : stats->lastFrameHlodReason))
        .arg(qualitySettings_.lod.occlusionCullingEnabled ? QStringLiteral("on") : QStringLiteral("off"))
        .arg(static_cast<qulonglong>(stats->lastFrameOcclusionTestedChunkCount))
        .arg(static_cast<qulonglong>(stats->lastFrameOcclusionRejectedChunkCount))
        .arg(static_cast<qulonglong>(stats->lastFrameSpatialCellTestCount))
        .arg(static_cast<qulonglong>(stats->lastFrameSpatialCellRejectedCount))
        .arg(static_cast<qulonglong>(stats->shadowBatchesSubmitted))
        .arg(static_cast<qulonglong>(stats->shadowCandidateInstances))
        .arg(static_cast<qulonglong>(stats->shadowPolicyRejectedInstances)));
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
