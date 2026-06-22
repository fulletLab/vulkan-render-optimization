#include <projectunity/editor/MainWindow.hpp>

#include <projectunity/core/Log.hpp>
#include <projectunity/editor/ProjectBrowserWidget.hpp>
#include <projectunity/editor/ViewportTuningImGuiWindow.hpp>
#include <projectunity/editor/ViewportWidget.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <string>

namespace projectunity::editor {
namespace {

[[nodiscard]] QPushButton* toolButton(const QString& text)
{
    auto* button = new QPushButton(text);
    button->setMinimumHeight(28);
    return button;
}

[[nodiscard]] QLabel* mutedLabel(const QString& text)
{
    auto* label = new QLabel(text);
    label->setWordWrap(true);
    label->setStyleSheet(QStringLiteral("color: #9aa6b7;"));
    return label;
}

[[nodiscard]] QString lightTypeName(scene::LightComponentType type)
{
    switch (type) {
    case scene::LightComponentType::Directional: return QStringLiteral("Directional");
    case scene::LightComponentType::Point: return QStringLiteral("Point");
    case scene::LightComponentType::Spot: return QStringLiteral("Spot");
    }
    return QStringLiteral("Light");
}

[[nodiscard]] QDoubleSpinBox* lightSpin(double minimum, double maximum, double value, double step = 0.05)
{
    auto* spin = new QDoubleSpinBox;
    spin->setRange(minimum, maximum);
    spin->setDecimals(3);
    spin->setSingleStep(step);
    spin->setValue(value);
    return spin;
}

[[nodiscard]] std::uint64_t modelTriangleCount(const assets::ModelAsset& model)
{
    std::uint64_t result = 0;
    for (const auto& primitive : model.primitives) {
        result += primitive.indices.size() / 3U;
    }
    return result;
}

[[nodiscard]] std::size_t modelLodCount(const assets::ModelAsset& model)
{
    std::size_t result = 0;
    for (const auto& primitive : model.primitives) {
        result += primitive.lods.size();
    }
    return result;
}

[[nodiscard]] bool modelHasMissingLod(const assets::ModelAsset& model)
{
    return std::any_of(model.primitives.begin(), model.primitives.end(), [](const auto& primitive) {
        return primitive.indices.size() >= 6U && primitive.lods.empty();
    });
}

[[nodiscard]] QString readShaderSource()
{
    const auto shaderPath = QStringLiteral(PROJECTUNITY_SOURCE_DIR "/engine/renderer/shaders/TexturedMesh.frag");
    QFile file(shaderPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QStringLiteral("No se pudo abrir %1").arg(shaderPath);
    }
    return QString::fromUtf8(file.readAll());
}

} // namespace

#if 0
QWidget* MainWindow::createOptimizationEditorPanel()
{
    auto* tabs = new QTabWidget;
    tabs->setDocumentMode(true);
    tabs->setObjectName(QStringLiteral("OptimizationEditorTabs"));

    auto* overview = new QWidget;
    auto* overviewLayout = new QVBoxLayout(overview);
    overviewLayout->setContentsMargins(6, 6, 6, 6);
    overviewLayout->setSpacing(6);

    auto* toolbar = new QHBoxLayout;
    auto* fullAsset = toolButton(QStringLiteral("Asset completo"));
    auto* lodOnly = toolButton(QStringLiteral("Solo LOD"));
    auto* noCulling = toolButton(QStringLiteral("Sin culling"));
    auto* balanced = toolButton(QStringLiteral("Balanceado seguro"));
    toolbar->addWidget(fullAsset);
    toolbar->addWidget(lodOnly);
    toolbar->addWidget(noCulling);
    toolbar->addWidget(balanced);
    toolbar->addSpacing(12);
    toolbar->addWidget(new QLabel(QStringLiteral("Debug viewport:")));
    optimizationDebugModeCombo_ = new QComboBox;
    optimizationDebugModeCombo_->addItem(QStringLiteral("Sombras"), static_cast<int>(renderer::RenderDebugViewMode::ShadowVisibility));
    optimizationDebugModeCombo_->addItem(QStringLiteral("Normales / Face color"), static_cast<int>(renderer::RenderDebugViewMode::FaceNormals));
    optimizationDebugModeCombo_->addItem(QStringLiteral("Depth"), static_cast<int>(renderer::RenderDebugViewMode::Depth));
    optimizationDebugModeCombo_->addItem(QStringLiteral("Wireframe"), 100);
    optimizationDebugModeCombo_->addItem(QStringLiteral("Lit"), static_cast<int>(renderer::RenderDebugViewMode::Lit));
    toolbar->addWidget(optimizationDebugModeCombo_);
    toolbar->addStretch();
    overviewLayout->addLayout(toolbar);

    optimizationRecoveryLabel_ = mutedLabel(QStringLiteral("Fallback Balanceado: armado; esperando una transición."));
    overviewLayout->addWidget(optimizationRecoveryLabel_);

    auto* viewportSplit = new QSplitter(Qt::Horizontal);
    optimizationPrimaryViewport_ = new ViewportWidget(ViewportMode::Scene);
    optimizationPrimaryViewport_->setObjectName(QStringLiteral("OptimizationPrimaryViewport"));
    optimizationDebugViewport_ = new ViewportWidget(ViewportMode::Scene);
    optimizationDebugViewport_->setObjectName(QStringLiteral("OptimizationDebugViewport"));
    for (auto* viewport : {optimizationPrimaryViewport_, optimizationDebugViewport_}) {
        viewport->setScene(&scene_);
        viewport->setAssetManager(&assetManager_);
        viewport->setRenderer(renderer_.get());
        viewport->setEnvironmentSettings(environmentSettings_);
        viewport->setEditorSunLight(editorSunLight_);
        viewport->setShadowUpdateMode(shadowUpdateMode_);
        viewport->setProfilingHudEnabled(true);
        viewport->setSelectionCallback([this](scene::EntityId id) {
            id.isValid() ? selectEntity(id) : clearSelection();
        });
        applyQualitySettingsToViewport(viewport);
    }
    optimizationDebugViewport_->setDebugViewMode(renderer::RenderDebugViewMode::ShadowVisibility);
    optimizationDebugViewport_->setShadowDebugOverlayEnabled(true);
    viewportSplit->addWidget(optimizationPrimaryViewport_);
    viewportSplit->addWidget(optimizationDebugViewport_);
    viewportSplit->setStretchFactor(0, 1);
    viewportSplit->setStretchFactor(1, 1);
    overviewLayout->addWidget(viewportSplit, 1);

    optimizationStatsLabel_ = mutedLabel(QStringLiteral("Esperando el primer frame Vulkan."));
    overviewLayout->addWidget(optimizationStatsLabel_);
    tabs->addTab(overview, QStringLiteral("Vista General"));

    auto* lodPage = new QWidget;
    auto* lodLayout = new QVBoxLayout(lodPage);
    optimizationLodTable_ = new QTableWidget(0, 7);
    optimizationLodTable_->setHorizontalHeaderLabels({
        QStringLiteral("Objeto"), QStringLiteral("Asset"), QStringLiteral("LOD disponibles"),
        QStringLiteral("Distancia"), QStringLiteral("Nivel activo"), QStringLiteral("Tri LOD0"),
        QStringLiteral("Estado"),
    });
    optimizationLodTable_->horizontalHeader()->setStretchLastSection(true);
    optimizationLodTable_->verticalHeader()->setVisible(false);
    optimizationLodTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    lodLayout->addWidget(optimizationLodTable_);
    tabs->addTab(lodPage, QStringLiteral("LOD Manager"));

    auto* shadowPage = new QWidget;
    auto* shadowLayout = new QVBoxLayout(shadowPage);
    optimizationShadowStatsLabel_ = mutedLabel(QStringLiteral("Shadow map: esperando frame."));
    shadowLayout->addWidget(optimizationShadowStatsLabel_);
    auto* shadowForm = new QFormLayout;
    optimizationShadowResolutionSpin_ = new QSpinBox;
    optimizationShadowResolutionSpin_->setRange(512, 8192);
    optimizationShadowResolutionSpin_->setSingleStep(512);
    optimizationShadowResolutionSpin_->setValue(qualitySettings_.shadow.resolution);
    optimizationShadowFrustumSpin_ = lightSpin(1.0, 5000.0, qualitySettings_.shadow.distance, 1.0);
    optimizationShadowNearSpin_ = lightSpin(0.001, 100.0, 0.05, 0.01);
    optimizationShadowFarSpin_ = lightSpin(1.0, 100000.0, qualitySettings_.shadow.distance, 1.0);
    shadowForm->addRow(QStringLiteral("Shadow Resolution"), optimizationShadowResolutionSpin_);
    shadowForm->addRow(QStringLiteral("Frustum Size"), optimizationShadowFrustumSpin_);
    shadowForm->addRow(QStringLiteral("Near Plane"), optimizationShadowNearSpin_);
    shadowForm->addRow(QStringLiteral("Far Plane"), optimizationShadowFarSpin_);
    shadowLayout->addLayout(shadowForm);
    shadowLayout->addWidget(mutedLabel(QStringLiteral(
        "La vista gris muestra visibilidad del shadow map en tiempo real; el overlay dibuja candidatos/casters. "
        "Near/Far quedan visibles para el diagnóstico del frustum.")));
    shadowLayout->addStretch();
    tabs->addTab(shadowPage, QStringLiteral("Shadow Debugger"));

    auto* normalPage = new QWidget;
    auto* normalLayout = new QVBoxLayout(normalPage);
    auto* normalToggle = new QCheckBox(QStringLiteral("Override global por normales"));
    normalToggle->setChecked(false);
    normalLayout->addWidget(normalToggle);
    normalLayout->addWidget(mutedLabel(QStringLiteral(
        "+Y = verde, +Z = azul/teal, +/-X = rojo/rosa. Las diagonales mezclan RGB. "
        "El override se aplica en el frame uniform y no modifica materiales ni assets.")));
    normalLayout->addStretch();
    tabs->addTab(normalPage, QStringLiteral("Normal Debugger"));

    auto* lightPage = new QWidget;
    auto* lightLayout = new QVBoxLayout(lightPage);
    optimizationLightTree_ = new QTreeWidget;
    optimizationLightTree_->setColumnCount(3);
    optimizationLightTree_->setHeaderLabels({QStringLiteral("Light Tree"), QStringLiteral("Valor"), QStringLiteral("Tipo")});
    optimizationLightTree_->header()->setStretchLastSection(false);
    optimizationLightTree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    optimizationLightTree_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    lightLayout->addWidget(optimizationLightTree_);
    tabs->addTab(lightPage, QStringLiteral("Light Editor"));

    auto* warningPage = new QWidget;
    auto* warningLayout = new QVBoxLayout(warningPage);
    auto* warningControls = new QHBoxLayout;
    warningControls->addWidget(new QLabel(QStringLiteral("Warning LOD0 >")));
    optimizationTriangleWarningSpin_ = new QSpinBox;
    optimizationTriangleWarningSpin_->setRange(1'000, 100'000'000);
    optimizationTriangleWarningSpin_->setValue(250'000);
    warningControls->addWidget(optimizationTriangleWarningSpin_);
    warningControls->addWidget(new QLabel(QStringLiteral("triángulos")));
    auto* rescan = toolButton(QStringLiteral("Rescan"));
    warningControls->addWidget(rescan);
    warningControls->addStretch();
    warningLayout->addLayout(warningControls);
    optimizationWarningsTable_ = new QTableWidget(0, 5);
    optimizationWarningsTable_->setHorizontalHeaderLabels({
        QStringLiteral("Severidad"), QStringLiteral("Objeto"), QStringLiteral("Diagnóstico"),
        QStringLiteral("Métrica"), QStringLiteral("Fix"),
    });
    optimizationWarningsTable_->horizontalHeader()->setStretchLastSection(false);
    optimizationWarningsTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    optimizationWarningsTable_->verticalHeader()->setVisible(false);
    optimizationWarningsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    warningLayout->addWidget(optimizationWarningsTable_);
    tabs->addTab(warningPage, QStringLiteral("Optimization Warnings"));

    auto* shaderPage = new QWidget;
    auto* shaderLayout = new QVBoxLayout(shaderPage);
    auto* shaderToolbar = new QHBoxLayout;
    auto* reloadShaders = toolButton(QStringLiteral("Reload All"));
    auto* refreshSource = toolButton(QStringLiteral("Refresh Source"));
    shaderToolbar->addWidget(reloadShaders);
    shaderToolbar->addWidget(refreshSource);
    shaderToolbar->addStretch();
    shaderLayout->addLayout(shaderToolbar);
    optimizationShaderCode_ = new QPlainTextEdit;
    optimizationShaderCode_->setReadOnly(true);
    optimizationShaderCode_->setLineWrapMode(QPlainTextEdit::NoWrap);
    optimizationShaderCode_->setPlainText(readShaderSource());
    shaderLayout->addWidget(optimizationShaderCode_);
    tabs->addTab(shaderPage, QStringLiteral("Shader Code"));

    connect(fullAsset, &QPushButton::clicked, this, [this] {
        auto next = qualitySettings_;
        next.graphics.preset = QualityPreset::Custom;
        next.lod.hlodEnabled = false;
        next.lod.debugOverride = ViewportHlodDebugOverride::ForceDetailed;
        next.lod.forceLod0 = true;
        next.lod.triangleBudgetEnabled = false;
        applyEditorQualitySettings(std::move(next), true);
    });
    connect(lodOnly, &QPushButton::clicked, this, [this] {
        auto next = qualitySettings_;
        next.graphics.preset = QualityPreset::Custom;
        next.lod.hlodEnabled = false;
        next.lod.debugOverride = ViewportHlodDebugOverride::ForceDetailed;
        next.lod.forceLod0 = false;
        next.lod.frustumCullingEnabled = false;
        next.lod.instanceCullingEnabled = false;
        next.lod.occlusionCullingEnabled = false;
        next.lod.spatialCellCullingEnabled = false;
        next.lod.triangleBudgetEnabled = true;
        applyEditorQualitySettings(std::move(next), true);
    });
    connect(noCulling, &QPushButton::clicked, this, [this] {
        auto next = qualitySettings_;
        next.graphics.preset = QualityPreset::Custom;
        next.lod.hlodEnabled = false;
        next.lod.debugOverride = ViewportHlodDebugOverride::ForceDetailed;
        next.lod.frustumCullingEnabled = false;
        next.lod.instanceCullingEnabled = false;
        next.lod.occlusionCullingEnabled = false;
        next.lod.spatialCellCullingEnabled = false;
        next.lod.triangleBudgetEnabled = false;
        applyEditorQualitySettings(std::move(next), true);
    });
    connect(balanced, &QPushButton::clicked, this, [this] { applyOptimizationBalancedMode(); });
    connect(optimizationDebugModeCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        if (optimizationDebugViewport_ == nullptr) {
            return;
        }
        const auto value = optimizationDebugModeCombo_->currentData().toInt();
        const auto wireframe = value == 100;
        optimizationDebugViewport_->setMeshWireOverlayEnabled(wireframe);
        optimizationDebugViewport_->setDebugViewMode(wireframe
                ? renderer::RenderDebugViewMode::Lit
                : static_cast<renderer::RenderDebugViewMode>(value));
        optimizationDebugViewport_->setShadowDebugOverlayEnabled(
            value == static_cast<int>(renderer::RenderDebugViewMode::ShadowVisibility));
    });
    connect(normalToggle, &QCheckBox::toggled, this, [this](bool enabled) {
        if (optimizationDebugViewport_ != nullptr) {
            optimizationDebugViewport_->setMeshWireOverlayEnabled(false);
            optimizationDebugViewport_->setDebugViewMode(enabled
                    ? renderer::RenderDebugViewMode::FaceNormals
                    : renderer::RenderDebugViewMode::Lit);
        }
    });
    connect(rescan, &QPushButton::clicked, this, [this] { rebuildOptimizationWarnings(); });
    connect(optimizationTriangleWarningSpin_, &QSpinBox::valueChanged, this, [this](int) { rebuildOptimizationWarnings(); });
    connect(optimizationShadowResolutionSpin_, &QSpinBox::editingFinished, this, [this] {
        auto next = qualitySettings_;
        next.shadow.resolution = optimizationShadowResolutionSpin_->value();
        next.graphics.preset = QualityPreset::Custom;
        applyEditorQualitySettings(std::move(next), true);
    });
    connect(optimizationShadowFrustumSpin_, &QDoubleSpinBox::editingFinished, this, [this] {
        auto next = qualitySettings_;
        next.shadow.distance = static_cast<float>(optimizationShadowFrustumSpin_->value());
        next.graphics.preset = QualityPreset::Custom;
        applyEditorQualitySettings(std::move(next), true);
    });
    connect(optimizationShadowFarSpin_, &QDoubleSpinBox::editingFinished, this, [this] {
        auto next = qualitySettings_;
        next.shadow.distance = static_cast<float>(optimizationShadowFarSpin_->value());
        next.graphics.preset = QualityPreset::Custom;
        applyEditorQualitySettings(std::move(next), true);
    });
    connect(refreshSource, &QPushButton::clicked, this, [this] { optimizationShaderCode_->setPlainText(readShaderSource()); });
    connect(reloadShaders, &QPushButton::clicked, this, [this, reloadShaders] {
        reloadShaders->setEnabled(false);
        reloadShaders->setText(QStringLiteral("Compiling..."));
        auto* process = new QProcess(this);
        connect(process, &QProcess::finished, this, [this, process, reloadShaders](int exitCode) {
            const auto output = QString::fromUtf8(process->readAllStandardOutput())
                + QString::fromUtf8(process->readAllStandardError());
            core::logInfo(core::LogCategory::Renderer, output.toStdString());
            reloadShaders->setEnabled(true);
            reloadShaders->setText(exitCode == 0 ? QStringLiteral("Reload All ✓") : QStringLiteral("Reload All ✗"));
            optimizationShaderCode_->setPlainText(readShaderSource());
            process->deleteLater();
        });
        process->setWorkingDirectory(QStringLiteral(PROJECTUNITY_SOURCE_DIR));
        process->start(QStringLiteral(PROJECTUNITY_CMAKE_COMMAND), {
            QStringLiteral("--build"), QStringLiteral("--preset"),
            QStringLiteral("dev-editor-local-qt"), QStringLiteral("--target"),
            QStringLiteral("projectunity_renderer"),
        });
    });

    rebuildOptimizationLightTree();
    rebuildOptimizationWarnings();
    updateOptimizationEditorPanel();
    return tabs;
}

#endif

QWidget* MainWindow::createOptimizationEditorPanel()
{
    auto* workspace = new QFrame;
    workspace->setObjectName(QStringLiteral("OptimizationStudioWorkspace"));
    workspace->setStyleSheet(QStringLiteral(
        "#OptimizationStudioWorkspace { background: #101216; }"
        "#OptimizationStudioPanel { background: #111317; border: 1px solid #343941; }"
        "#OptimizationStudioHeader { background: #171a1f; color: #d8dde6; padding: 2px 5px; font-weight: 600; }"
        "QSplitter::handle { background: #343b46; }"));
    auto* workspaceLayout = new QVBoxLayout(workspace);
    workspaceLayout->setContentsMargins(0, 0, 0, 0);
    workspaceLayout->setSpacing(0);

    const auto makePanel = [](const QString& title, QWidget* content, QWidget* headerControl = nullptr) {
        auto* panel = new QFrame;
        panel->setObjectName(QStringLiteral("OptimizationStudioPanel"));
        auto* layout = new QVBoxLayout(panel);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        auto* header = new QWidget;
        header->setObjectName(QStringLiteral("OptimizationStudioHeader"));
        header->setFixedHeight(24);
        auto* headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(5, 1, 5, 1);
        headerLayout->setSpacing(6);
        auto* titleLabel = new QLabel(QStringLiteral("▾  %1").arg(title));
        headerLayout->addWidget(titleLabel);
        if (headerControl != nullptr) {
            headerLayout->addWidget(headerControl);
        }
        headerLayout->addStretch();
        layout->addWidget(header);
        layout->addWidget(content, 1);
        return panel;
    };

    auto* rootSplit = new QSplitter(Qt::Horizontal);
    rootSplit->setChildrenCollapsible(false);
    rootSplit->setHandleWidth(2);
    workspaceLayout->addWidget(rootSplit);

    createViewportTuningWindow(rootSplit);
    viewportTuningImGuiWindow_->setObjectName(QStringLiteral("EmbeddedViewportTuningImGui"));
    rootSplit->addWidget(viewportTuningImGuiWindow_);

    auto* centerSplit = new QSplitter(Qt::Vertical);
    centerSplit->setChildrenCollapsible(false);
    centerSplit->setHandleWidth(2);
    rootSplit->addWidget(centerSplit);

    auto* viewportRow = new QSplitter(Qt::Horizontal);
    viewportRow->setChildrenCollapsible(false);
    viewportRow->setHandleWidth(2);
    centerSplit->addWidget(viewportRow);

    optimizationPrimaryViewport_ = new ViewportWidget(ViewportMode::Scene);
    optimizationPrimaryViewport_->setObjectName(QStringLiteral("OptimizationPrimaryViewport"));
    optimizationPrimaryViewport_->setScene(&scene_);
    optimizationPrimaryViewport_->setAssetManager(&assetManager_);
    optimizationPrimaryViewport_->setRenderer(renderer_.get());
    optimizationPrimaryViewport_->setEnvironmentSettings(environmentSettings_);
    optimizationPrimaryViewport_->setEditorSunLight(editorSunLight_);
    optimizationPrimaryViewport_->setShadowUpdateMode(shadowUpdateMode_);
    optimizationPrimaryViewport_->setProfilingHudEnabled(false);
    optimizationPrimaryViewport_->setSelectionCallback([this](scene::EntityId id) {
        id.isValid() ? selectEntity(id) : clearSelection();
    });
    applyQualitySettingsToViewport(optimizationPrimaryViewport_);
    auto* primaryModeCombo = new QComboBox;
    primaryModeCombo->setMinimumWidth(120);
    primaryModeCombo->addItem(QStringLiteral("Lit"), static_cast<int>(renderer::RenderDebugViewMode::Lit));
    primaryModeCombo->addItem(QStringLiteral("Depth"), static_cast<int>(renderer::RenderDebugViewMode::Depth));
    primaryModeCombo->addItem(QStringLiteral("Normals"), static_cast<int>(renderer::RenderDebugViewMode::FaceNormals));
    primaryModeCombo->addItem(QStringLiteral("Wireframe"), 100);
    viewportRow->addWidget(makePanel(QStringLiteral("Viewport"), optimizationPrimaryViewport_, primaryModeCombo));

    auto* shadowColumn = new QSplitter(Qt::Vertical);
    shadowColumn->setChildrenCollapsible(false);
    shadowColumn->setHandleWidth(2);

    optimizationDebugModeCombo_ = new QComboBox;
    optimizationDebugModeCombo_->setMinimumWidth(140);
    optimizationDebugModeCombo_->addItem(QStringLiteral("Shadows"), static_cast<int>(renderer::RenderDebugViewMode::ShadowVisibility));
    optimizationDebugModeCombo_->addItem(QStringLiteral("Normals"), static_cast<int>(renderer::RenderDebugViewMode::FaceNormals));
    optimizationDebugModeCombo_->addItem(QStringLiteral("Depth"), static_cast<int>(renderer::RenderDebugViewMode::Depth));
    optimizationDebugModeCombo_->addItem(QStringLiteral("Wireframe"), 100);
    optimizationDebugModeCombo_->addItem(QStringLiteral("Lit"), static_cast<int>(renderer::RenderDebugViewMode::Lit));

    optimizationDebugViewport_ = new ViewportWidget(ViewportMode::Scene);
    optimizationDebugViewport_->setObjectName(QStringLiteral("OptimizationDebugViewport"));
    optimizationDebugViewport_->setScene(&scene_);
    optimizationDebugViewport_->setAssetManager(&assetManager_);
    optimizationDebugViewport_->setRenderer(renderer_.get());
    optimizationDebugViewport_->setEnvironmentSettings(environmentSettings_);
    optimizationDebugViewport_->setEditorSunLight(editorSunLight_);
    optimizationDebugViewport_->setShadowUpdateMode(shadowUpdateMode_);
    optimizationDebugViewport_->setDebugViewMode(renderer::RenderDebugViewMode::ShadowVisibility);
    optimizationDebugViewport_->setShadowDebugOverlayEnabled(true);
    optimizationDebugViewport_->setProfilingHudEnabled(false);
    optimizationDebugViewport_->setSelectionCallback([this](scene::EntityId id) {
        id.isValid() ? selectEntity(id) : clearSelection();
    });
    applyQualitySettingsToViewport(optimizationDebugViewport_);
    shadowColumn->addWidget(makePanel(
        QStringLiteral("Shadow debugging viewport"),
        optimizationDebugViewport_,
        optimizationDebugModeCombo_));

    auto* shadowParameters = new QWidget;
    auto* shadowForm = new QFormLayout(shadowParameters);
    shadowForm->setContentsMargins(8, 5, 8, 5);
    shadowForm->setSpacing(3);
    optimizationShadowResolutionSpin_ = new QSpinBox;
    optimizationShadowResolutionSpin_->setRange(512, 8192);
    optimizationShadowResolutionSpin_->setSingleStep(512);
    optimizationShadowResolutionSpin_->setValue(qualitySettings_.shadow.resolution);
    optimizationShadowFrustumSpin_ = lightSpin(1.0, 5000.0, qualitySettings_.shadow.distance, 1.0);
    optimizationShadowNearSpin_ = lightSpin(0.001, 100.0, 0.05, 0.01);
    optimizationShadowFarSpin_ = lightSpin(1.0, 100000.0, qualitySettings_.shadow.distance, 1.0);
    shadowForm->addRow(QStringLiteral("Shadow resolution:"), optimizationShadowResolutionSpin_);
    shadowForm->addRow(QStringLiteral("Frustum size:"), optimizationShadowFrustumSpin_);
    shadowForm->addRow(QStringLiteral("Near plane:"), optimizationShadowNearSpin_);
    shadowForm->addRow(QStringLiteral("Far plane:"), optimizationShadowFarSpin_);
    optimizationShadowStatsLabel_ = mutedLabel(QStringLiteral("Waiting for shadow frame..."));
    shadowForm->addRow(optimizationShadowStatsLabel_);
    shadowColumn->addWidget(makePanel(QStringLiteral("Shadow parameters"), shadowParameters));
    shadowColumn->setSizes({590, 125});
    viewportRow->addWidget(shadowColumn);
    viewportRow->setSizes({760, 580});

    auto* shaderRow = new QSplitter(Qt::Horizontal);
    shaderRow->setChildrenCollapsible(false);
    shaderRow->setHandleWidth(2);

    auto* reloadContent = new QWidget;
    auto* reloadLayout = new QVBoxLayout(reloadContent);
    reloadLayout->setContentsMargins(6, 5, 6, 5);
    auto* reloadShaders = toolButton(QStringLiteral("Reload all"));
    reloadLayout->addWidget(reloadShaders, 0, Qt::AlignLeft);
    reloadLayout->addWidget(mutedLabel(QStringLiteral("Recompile embedded GLSL/SPIR-V shaders.")));
    reloadLayout->addStretch();
    shaderRow->addWidget(makePanel(QStringLiteral("Shader hot reload"), reloadContent));

    auto* shaderContent = new QWidget;
    auto* shaderLayout = new QVBoxLayout(shaderContent);
    shaderLayout->setContentsMargins(4, 4, 4, 4);
    auto* refreshSource = toolButton(QStringLiteral("Refresh source"));
    optimizationShaderCode_ = new QPlainTextEdit;
    optimizationShaderCode_->setReadOnly(true);
    optimizationShaderCode_->setLineWrapMode(QPlainTextEdit::NoWrap);
    optimizationShaderCode_->setPlainText(readShaderSource());
    shaderLayout->addWidget(refreshSource, 0, Qt::AlignLeft);
    shaderLayout->addWidget(optimizationShaderCode_, 1);
    shaderRow->addWidget(makePanel(QStringLiteral("Shader code display"), shaderContent));
    shaderRow->setSizes({520, 820});
    centerSplit->addWidget(shaderRow);
    centerSplit->setSizes({700, 130});

    auto* rightSplit = new QSplitter(Qt::Vertical);
    rightSplit->setChildrenCollapsible(false);
    rightSplit->setHandleWidth(2);
    rootSplit->addWidget(rightSplit);

    auto* lightsContent = new QWidget;
    auto* lightsLayout = new QVBoxLayout(lightsContent);
    lightsLayout->setContentsMargins(3, 3, 3, 3);
    auto* lightButtons = new QHBoxLayout;
    auto* addDirectional = toolButton(QStringLiteral("+ Directional"));
    auto* addPoint = toolButton(QStringLiteral("+ Point"));
    auto* addSpot = toolButton(QStringLiteral("+ Spot"));
    lightButtons->addWidget(addDirectional);
    lightButtons->addWidget(addPoint);
    lightButtons->addWidget(addSpot);
    lightsLayout->addLayout(lightButtons);
    optimizationLightTree_ = new QTreeWidget;
    optimizationLightTree_->setColumnCount(3);
    optimizationLightTree_->setHeaderLabels({QStringLiteral("Light tree"), QStringLiteral("Value"), QStringLiteral("Type")});
    optimizationLightTree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    optimizationLightTree_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    optimizationLightTree_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    lightsLayout->addWidget(optimizationLightTree_, 1);
    rightSplit->addWidget(makePanel(QStringLiteral("Scene / Lights"), lightsContent));

    auto* assetsContent = new QWidget;
    auto* assetsLayout = new QVBoxLayout(assetsContent);
    assetsLayout->setContentsMargins(5, 5, 5, 5);
    assetsLayout->setSpacing(4);
    auto* importButton = toolButton(QStringLiteral("Import Asset"));
    assetsLayout->addWidget(importButton, 0, Qt::AlignLeft);
    optimizationAssetImportStatus_ = mutedLabel(QStringLiteral("Ready"));
    assetsLayout->addWidget(optimizationAssetImportStatus_);
    optimizationAssetImportProgress_ = new QProgressBar;
    optimizationAssetImportProgress_->setRange(0, 100);
    optimizationAssetImportProgress_->setValue(0);
    optimizationAssetImportProgress_->setFormat(QStringLiteral("Ready"));
    assetsLayout->addWidget(optimizationAssetImportProgress_);
    optimizationProjectBrowser_ = new ProjectBrowserWidget;
    optimizationProjectBrowser_->setObjectName(QStringLiteral("OptimizationProjectBrowser"));
    optimizationProjectBrowser_->setAssetManager(&assetManager_);
    optimizationProjectBrowser_->setProjectRoot(std::filesystem::path(PROJECTUNITY_SOURCE_DIR) / "Project" / "Assets");
    optimizationProjectBrowser_->setSelectionChangedCallback([](const ProjectBrowserSelection& selection) {
        core::logInfo(
            core::LogCategory::Assets,
            "Optimization Asset Manager selected assetId=" + std::to_string(selection.assetId.value()));
    });
    optimizationProjectBrowser_->setAssetActivatedCallback([this](const ProjectBrowserSelection& selection) {
        if (selection.subAssetId != 0U) {
            return;
        }
        const auto records = assetManager_.records();
        const auto record = std::find_if(records.begin(), records.end(), [selection](const auto& candidate) {
            return candidate.id == selection.assetId;
        });
        if (record != records.end() && record->type == assets::AssetType::Model) {
            createImportedModelEntity(*record);
        }
    });
    optimizationProjectBrowser_->rebuild();
    assetsLayout->addWidget(optimizationProjectBrowser_, 1);
    rightSplit->addWidget(makePanel(QStringLiteral("Asset manager"), assetsContent));
    rightSplit->setSizes({520, 280});

    rootSplit->setStretchFactor(0, 0);
    rootSplit->setStretchFactor(1, 1);
    rootSplit->setStretchFactor(2, 0);
    rootSplit->setSizes({230, 1330, 360});

    optimizationStatsLabel_ = new QLabel(workspace);
    optimizationStatsLabel_->hide();
    optimizationRecoveryLabel_ = new QLabel(workspace);
    optimizationRecoveryLabel_->hide();

    connect(optimizationDebugModeCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        const auto value = optimizationDebugModeCombo_->currentData().toInt();
        const auto wireframe = value == 100;
        optimizationDebugViewport_->setMeshWireOverlayEnabled(wireframe);
        optimizationDebugViewport_->setDebugViewMode(wireframe
                ? renderer::RenderDebugViewMode::Lit
                : static_cast<renderer::RenderDebugViewMode>(value));
        optimizationDebugViewport_->setShadowDebugOverlayEnabled(
            value == static_cast<int>(renderer::RenderDebugViewMode::ShadowVisibility));
    });
    connect(primaryModeCombo, &QComboBox::currentIndexChanged, this, [this, primaryModeCombo](int) {
        const auto value = primaryModeCombo->currentData().toInt();
        const auto wireframe = value == 100;
        optimizationPrimaryViewport_->setMeshWireOverlayEnabled(wireframe);
        optimizationPrimaryViewport_->setDebugViewMode(wireframe
                ? renderer::RenderDebugViewMode::Lit
                : static_cast<renderer::RenderDebugViewMode>(value));
    });
    connect(importButton, &QPushButton::clicked, this, [this] { importAsset(); });
    connect(addDirectional, &QPushButton::clicked, this, [this] {
        createLightEntity(scene::LightComponentType::Directional);
        rebuildOptimizationLightTree();
    });
    connect(addPoint, &QPushButton::clicked, this, [this] {
        createLightEntity(scene::LightComponentType::Point);
        rebuildOptimizationLightTree();
    });
    connect(addSpot, &QPushButton::clicked, this, [this] {
        createLightEntity(scene::LightComponentType::Spot);
        rebuildOptimizationLightTree();
    });
    connect(optimizationShadowResolutionSpin_, &QSpinBox::editingFinished, this, [this] {
        auto next = qualitySettings_;
        next.shadow.resolution = optimizationShadowResolutionSpin_->value();
        next.graphics.preset = QualityPreset::Custom;
        applyEditorQualitySettings(std::move(next), true);
    });
    connect(optimizationShadowFrustumSpin_, &QDoubleSpinBox::editingFinished, this, [this] {
        auto next = qualitySettings_;
        next.shadow.distance = static_cast<float>(optimizationShadowFrustumSpin_->value());
        next.graphics.preset = QualityPreset::Custom;
        applyEditorQualitySettings(std::move(next), true);
    });
    connect(optimizationShadowFarSpin_, &QDoubleSpinBox::editingFinished, this, [this] {
        auto next = qualitySettings_;
        next.shadow.distance = static_cast<float>(optimizationShadowFarSpin_->value());
        next.graphics.preset = QualityPreset::Custom;
        applyEditorQualitySettings(std::move(next), true);
    });
    connect(refreshSource, &QPushButton::clicked, this, [this] {
        optimizationShaderCode_->setPlainText(readShaderSource());
    });
    connect(reloadShaders, &QPushButton::clicked, this, [this, reloadShaders] {
        reloadShaders->setEnabled(false);
        reloadShaders->setText(QStringLiteral("Compiling..."));
        auto* process = new QProcess(this);
        connect(process, &QProcess::finished, this, [this, process, reloadShaders](int exitCode) {
            const auto output = QString::fromUtf8(process->readAllStandardOutput())
                + QString::fromUtf8(process->readAllStandardError());
            core::logInfo(core::LogCategory::Renderer, output.toStdString());
            reloadShaders->setEnabled(true);
            reloadShaders->setText(exitCode == 0 ? QStringLiteral("Reload all ✓") : QStringLiteral("Reload all ✗"));
            optimizationShaderCode_->setPlainText(readShaderSource());
            process->deleteLater();
        });
        process->setWorkingDirectory(QStringLiteral(PROJECTUNITY_SOURCE_DIR));
        process->start(QStringLiteral(PROJECTUNITY_CMAKE_COMMAND), {
            QStringLiteral("--build"), QStringLiteral("--preset"),
            QStringLiteral("dev-editor-local-qt"), QStringLiteral("--target"),
            QStringLiteral("projectunity_renderer"),
        });
    });

    rebuildOptimizationLightTree();
    updateOptimizationEditorPanel();
    return workspace;
}

void MainWindow::applyOptimizationBalancedMode()
{
    const auto hlodWasEnabled = qualitySettings_.lod.hlodEnabled;
    auto next = qualitySettings_;
    applyQualityPreset(QualityPreset::Medium, next);
    next.graphics.preset = QualityPreset::Custom;
    if (!hlodWasEnabled) {
        next.lod.hlodEnabled = false;
        next.lod.debugOverride = ViewportHlodDebugOverride::ForceDetailed;
    }

    bool missingLod = false;
    for (const auto& entity : scene_.entities()) {
        if (!entity.meshRenderer.has_value()) {
            continue;
        }
        const auto model = assetManager_.model(entity.meshRenderer->modelAssetId);
        missingLod = missingLod || (model != nullptr && modelHasMissingLod(*model));
    }
    if (missingLod && !next.lod.hlodEnabled) {
        next.lod.debugOverride = ViewportHlodDebugOverride::ForceDetailed;
    }
    optimizationBalancedPending_ = true;
    optimizationAutoRecovered_ = false;
    core::logInfo(
        core::LogCategory::Renderer,
        "[OptimizationBalanced] armed fallback validLodCheck=true preserveHlodDisabled=true");
    applyEditorQualitySettings(std::move(next), true);
}

void MainWindow::updateOptimizationEditorPanel()
{
    if (optimizationPanelUpdating_ || optimizationStatsLabel_ == nullptr) {
        return;
    }
    optimizationPanelUpdating_ = true;
    const auto* stats = sceneViewport_ != nullptr ? sceneViewport_->lastRendererStats() : nullptr;
    if (stats != nullptr) {
        const auto frameMs = stats->FPS > 0.001 ? 1000.0 / stats->FPS : 0.0;
        optimizationStatsLabel_->setText(QStringLiteral(
            "FPS %1 | Frame %2 ms | Draw calls %3 | Triángulos %4 | Objetos CPU %5/%6 | "
            "GPU residency: %7 meshes, %8 textures | Upload frame %9 KB")
            .arg(stats->FPS, 0, 'f', 1)
            .arg(frameMs, 0, 'f', 2)
            .arg(static_cast<qulonglong>(stats->vkDrawIndexed))
            .arg(static_cast<qulonglong>(stats->lastFrameVisibleTriangleCount))
            .arg(static_cast<qulonglong>(stats->lastFrameVisibleRenderInstanceCount))
            .arg(static_cast<qulonglong>(stats->lastFrameRenderInstanceCount))
            .arg(static_cast<qulonglong>(stats->residentMeshCount))
            .arg(static_cast<qulonglong>(stats->residentTextureCount))
            .arg(static_cast<qulonglong>(stats->lastFrameStaticUploadBytes / 1024U)));
        if (optimizationShadowStatsLabel_ != nullptr) {
            optimizationShadowStatsLabel_->setText(QStringLiteral(
                "Shadow candidates %1 | rejected %2 | submitted %3 | batches %4 | triangles %5 | CPU %6 ms | GPU %7 ms")
                .arg(static_cast<qulonglong>(stats->shadowCandidates))
                .arg(static_cast<qulonglong>(stats->shadowRejectedByPolicy + stats->shadowRejectedByCasterCull))
                .arg(static_cast<qulonglong>(stats->shadowSubmitted))
                .arg(static_cast<qulonglong>(stats->shadowBatchesSubmitted))
                .arg(static_cast<qulonglong>(stats->shadowTrianglesSubmitted))
                .arg(stats->shadowCpuMs, 0, 'f', 3)
                .arg(stats->shadowGpuMs, 0, 'f', 3));
        }

        if (optimizationBalancedPending_) {
            const auto candidates = stats->lastFrameCandidateMeshDrawCount;
            const auto visible = stats->lastFrameMeshDrawCount;
            if (candidates > 0U && visible == 0U) {
                auto safe = qualitySettings_;
                safe.graphics.preset = QualityPreset::Custom;
                safe.lod.hlodEnabled = false;
                safe.lod.debugOverride = ViewportHlodDebugOverride::ForceDetailed;
                safe.lod.occlusionCullingEnabled = false;
                safe.lod.spatialCellCullingEnabled = false;
                safe.lod.cullingBoundsPadding = std::max(safe.lod.cullingBoundsPadding, 1.0F);
                optimizationAutoRecovered_ = true;
                optimizationBalancedPending_ = false;
                const auto message = QStringLiteral(
                    "[OptimizationBalancedFallback] candidates=%1 visible=0 reason=aggressive-culling; "
                    "action=ForceDetailed+disableOcclusionSpatial")
                    .arg(static_cast<qulonglong>(candidates));
                core::logWarning(core::LogCategory::Renderer, message.toStdString());
                optimizationRecoveryLabel_->setText(message);
                optimizationPanelUpdating_ = false;
                applyEditorQualitySettings(std::move(safe), true);
                rebuildOptimizationWarnings();
                return;
            }
            if (visible > 0U || candidates == 0U) {
                optimizationBalancedPending_ = false;
                optimizationRecoveryLabel_->setText(QStringLiteral(
                    "Balanceado validado: %1 candidatos, %2 draws visibles; fallback no necesario.")
                    .arg(static_cast<qulonglong>(candidates))
                    .arg(static_cast<qulonglong>(visible)));
            }
        }
    }

    if (optimizationLodTable_ != nullptr) {
        optimizationLodTable_->setRowCount(0);
        for (const auto& entity : scene_.entities()) {
            if (!entity.meshRenderer.has_value()) {
                continue;
            }
            const auto model = assetManager_.model(entity.meshRenderer->modelAssetId);
            if (model == nullptr) {
                continue;
            }
            const auto row = optimizationLodTable_->rowCount();
            optimizationLodTable_->insertRow(row);
            const auto distance = entity.transform.position.length();
            const auto missing = modelHasMissingLod(*model);
            const auto active = qualitySettings_.lod.forceLod0 || !qualitySettings_.lod.automaticLod
                ? QStringLiteral("LOD0")
                : (missing ? QStringLiteral("LOD0 fallback") : QStringLiteral("Auto"));
            const QStringList values {
                QString::fromStdString(entity.name), QString::fromStdString(model->name),
                QString::number(static_cast<qulonglong>(modelLodCount(*model))),
                QStringLiteral("%1 m").arg(distance, 0, 'f', 1), active,
                QString::number(static_cast<qulonglong>(modelTriangleCount(*model))),
                missing ? QStringLiteral("SIN LOD") : QStringLiteral("OK"),
            };
            for (int column = 0; column < values.size(); ++column) {
                auto* item = new QTableWidgetItem(values[column]);
                if (missing) {
                    item->setForeground(QColor(235, 92, 92));
                }
                optimizationLodTable_->setItem(row, column, item);
            }
        }
    }
    const auto lightCount = static_cast<int>(std::count_if(scene_.entities().begin(), scene_.entities().end(), [](const auto& entity) {
        return entity.light.has_value();
    }));
    if (optimizationLightTree_ != nullptr && optimizationLightTree_->topLevelItemCount() != lightCount) {
        rebuildOptimizationLightTree();
    }
    optimizationPanelUpdating_ = false;
}

void MainWindow::rebuildOptimizationWarnings()
{
    if (optimizationWarningsTable_ == nullptr) {
        return;
    }
    optimizationWarningsTable_->setRowCount(0);
    const auto triangleThreshold = optimizationTriangleWarningSpin_ == nullptr
        ? 250'000ULL
        : static_cast<std::uint64_t>(optimizationTriangleWarningSpin_->value());

    const auto addWarning = [this](
        const QString& severity,
        const QString& object,
        const QString& diagnosis,
        const QString& metric,
        const QString& fixText,
        auto fix) {
        const auto row = optimizationWarningsTable_->rowCount();
        optimizationWarningsTable_->insertRow(row);
        const QColor color = severity == QStringLiteral("ERROR") ? QColor(235, 92, 92) : QColor(235, 190, 75);
        const QStringList values {severity, object, diagnosis, metric};
        for (int column = 0; column < values.size(); ++column) {
            auto* item = new QTableWidgetItem(values[column]);
            item->setForeground(color);
            optimizationWarningsTable_->setItem(row, column, item);
        }
        auto* button = toolButton(fixText);
        connect(button, &QPushButton::clicked, this, fix);
        optimizationWarningsTable_->setCellWidget(row, 4, button);
    };

    std::map<std::uint64_t, std::vector<scene::EntityId>> staticGroups;
    for (const auto& entity : scene_.entities()) {
        if (entity.meshRenderer.has_value()) {
            const auto model = assetManager_.model(entity.meshRenderer->modelAssetId);
            if (model == nullptr) {
                addWarning(QStringLiteral("ERROR"), QString::fromStdString(entity.name),
                    QStringLiteral("Asset de malla no resolvió en CPU"), QStringLiteral("missing asset"),
                    QStringLiteral("Select"), [this, id = entity.id] { selectEntity(id); });
                continue;
            }
            if (modelHasMissingLod(*model)) {
                addWarning(QStringLiteral("ERROR"), QString::fromStdString(entity.name),
                    QStringLiteral("Mesh sin LOD válido; Balanceado usará LOD0 seguro"),
                    QStringLiteral("%1 primitives").arg(model->primitives.size()),
                    QStringLiteral("Force LOD0"), [this, id = entity.id] {
                        selectEntity(id);
                        auto next = qualitySettings_;
                        next.lod.debugOverride = ViewportHlodDebugOverride::ForceDetailed;
                        next.lod.forceLod0 = true;
                        applyEditorQualitySettings(std::move(next), true);
                    });
            }
            const auto triangles = modelTriangleCount(*model);
            if (triangles > triangleThreshold) {
                addWarning(QStringLiteral("WARNING"), QString::fromStdString(entity.name),
                    QStringLiteral("LOD0 supera el límite configurado"),
                    QStringLiteral("%1 tri").arg(static_cast<qulonglong>(triangles)),
                    QStringLiteral("Select"), [this, id = entity.id] { selectEntity(id); });
            }
            if (entity.meshRenderer->runtimeCook.staticBatchable) {
                staticGroups[entity.meshRenderer->modelAssetId.value()].push_back(entity.id);
            }
        }
        if (entity.light.has_value()) {
            if (!entity.light->castsShadow) {
                addWarning(QStringLiteral("WARNING"), QString::fromStdString(entity.name),
                    QStringLiteral("Luz excluida del shadow culling/pass"), QStringLiteral("castsShadow=false"),
                    QStringLiteral("Enable"), [this, id = entity.id] {
                        if (auto* target = scene_.findEntity(id); target != nullptr && target->light.has_value()) {
                            auto light = *target->light;
                            light.castsShadow = true;
                            (void)scene_.setLight(id, light);
                            refreshViewports();
                            rebuildOptimizationWarnings();
                            rebuildOptimizationLightTree();
                        }
                    });
            }
            if (entity.light->type != scene::LightComponentType::Directional && entity.light->range <= 0.0F) {
                addWarning(QStringLiteral("WARNING"), QString::fromStdString(entity.name),
                    QStringLiteral("Luz local sin rango finito para culling"), QStringLiteral("range=0"),
                    QStringLiteral("Range 25"), [this, id = entity.id] {
                        if (auto* target = scene_.findEntity(id); target != nullptr && target->light.has_value()) {
                            auto light = *target->light;
                            light.range = 25.0F;
                            (void)scene_.setLight(id, light);
                            refreshViewports();
                            rebuildOptimizationWarnings();
                            rebuildOptimizationLightTree();
                        }
                    });
            }
        }
    }
    for (const auto& [assetId, ids] : staticGroups) {
        if (ids.size() < 2U) {
            continue;
        }
        addWarning(QStringLiteral("SUGGEST"), QStringLiteral("Static batch group"),
            QStringLiteral("Instancias compatibles pueden mergearse/instanciarse"),
            QStringLiteral("asset %1 × %2").arg(static_cast<qulonglong>(assetId)).arg(ids.size()),
            QStringLiteral("Select first"), [this, id = ids.front()] { selectEntity(id); });
    }
    if (const auto* stats = sceneViewport_ != nullptr ? sceneViewport_->lastRendererStats() : nullptr;
        stats != nullptr && stats->objectsConsidered > stats->passedFrustum) {
        addWarning(QStringLiteral("SUGGEST"), QStringLiteral("CPU scene"),
            QStringLiteral("Objetos fuera del frustum aún considerados por CPU"),
            QStringLiteral("%1").arg(static_cast<qulonglong>(stats->objectsConsidered - stats->passedFrustum)),
            QStringLiteral("Enable culling"), [this] {
                auto next = qualitySettings_;
                next.lod.frustumCullingEnabled = true;
                next.lod.instanceCullingEnabled = true;
                next.lod.spatialCellCullingEnabled = true;
                applyEditorQualitySettings(std::move(next), true);
            });
    }
}

void MainWindow::rebuildOptimizationLightTree()
{
    if (optimizationLightTree_ == nullptr) {
        return;
    }
    disconnect(optimizationLightTree_, &QTreeWidget::itemChanged, this, nullptr);
    optimizationLightTree_->clear();
    for (const auto& entity : scene_.entities()) {
        if (!entity.light.has_value()) {
            continue;
        }
        const auto source = *entity.light;
        auto* root = new QTreeWidgetItem(optimizationLightTree_);
        root->setText(0, QString::fromStdString(entity.name));
        root->setText(2, lightTypeName(source.type));
        root->setCheckState(0, source.enabled ? Qt::Checked : Qt::Unchecked);
        root->setExpanded(true);
        connect(optimizationLightTree_, &QTreeWidget::itemChanged, this, [this, root, id = entity.id](QTreeWidgetItem* item, int column) {
            if (item != root || column != 0 || optimizationPanelUpdating_) {
                return;
            }
            if (auto* target = scene_.findEntity(id); target != nullptr && target->light.has_value()) {
                auto light = *target->light;
                light.enabled = root->checkState(0) == Qt::Checked;
                (void)scene_.setLight(id, light);
                refreshViewports();
            }
        });

        const auto addSpin = [this, root, id = entity.id](
            const QString& name,
            double value,
            double minimum,
            double maximum,
            auto setter) {
            auto* item = new QTreeWidgetItem(root);
            item->setText(0, name);
            auto* spin = lightSpin(minimum, maximum, value);
            optimizationLightTree_->setItemWidget(item, 1, spin);
            connect(spin, &QDoubleSpinBox::valueChanged, this, [this, id, setter](double nextValue) {
                if (auto* target = scene_.findEntity(id); target != nullptr && target->light.has_value()) {
                    auto light = *target->light;
                    auto transform = target->transform;
                    setter(light, transform, static_cast<float>(nextValue));
                    (void)scene_.setLight(id, light);
                    (void)scene_.setTransform(id, transform);
                    refreshViewports();
                }
            });
        };
        addSpin(QStringLiteral("Direction X"), source.direction.x, -1.0, 1.0,
            [](auto& light, auto&, float value) { light.direction.x = value; });
        addSpin(QStringLiteral("Direction Y"), source.direction.y, -1.0, 1.0,
            [](auto& light, auto&, float value) { light.direction.y = value; });
        addSpin(QStringLiteral("Direction Z"), source.direction.z, -1.0, 1.0,
            [](auto& light, auto&, float value) { light.direction.z = value; });
        if (source.type != scene::LightComponentType::Directional) {
            addSpin(QStringLiteral("Position X"), entity.transform.position.x, -100000.0, 100000.0,
                [](auto&, auto& transform, float value) { transform.position.x = value; });
            addSpin(QStringLiteral("Position Y"), entity.transform.position.y, -100000.0, 100000.0,
                [](auto&, auto& transform, float value) { transform.position.y = value; });
            addSpin(QStringLiteral("Position Z"), entity.transform.position.z, -100000.0, 100000.0,
                [](auto&, auto& transform, float value) { transform.position.z = value; });
        }
        addSpin(QStringLiteral("Color R (HDR)"), source.color[0], 0.0, 32.0,
            [](auto& light, auto&, float value) { light.color[0] = value; });
        addSpin(QStringLiteral("Color G (HDR)"), source.color[1], 0.0, 32.0,
            [](auto& light, auto&, float value) { light.color[1] = value; });
        addSpin(QStringLiteral("Color B (HDR)"), source.color[2], 0.0, 32.0,
            [](auto& light, auto&, float value) { light.color[2] = value; });
        addSpin(QStringLiteral("Intensity"), source.intensity, 0.0, 100000.0,
            [](auto& light, auto&, float value) { light.intensity = value; });
        addSpin(QStringLiteral("Range"), source.range, 0.0, 100000.0,
            [](auto& light, auto&, float value) { light.range = value; });
        addSpin(QStringLiteral("Attenuation Linear"), source.linearAttenuation, 0.0, 100.0,
            [](auto& light, auto&, float value) { light.linearAttenuation = value; });
        addSpin(QStringLiteral("Attenuation Quadratic"), source.quadraticAttenuation, 0.0001, 100.0,
            [](auto& light, auto&, float value) { light.quadraticAttenuation = value; });

        auto* shadowItem = new QTreeWidgetItem(root);
        shadowItem->setText(0, QStringLiteral("Casts Shadow / Culling"));
        auto* shadowCheck = new QCheckBox;
        shadowCheck->setChecked(source.castsShadow);
        optimizationLightTree_->setItemWidget(shadowItem, 1, shadowCheck);
        connect(shadowCheck, &QCheckBox::toggled, this, [this, id = entity.id](bool enabled) {
            if (auto* target = scene_.findEntity(id); target != nullptr && target->light.has_value()) {
                auto light = *target->light;
                light.castsShadow = enabled;
                (void)scene_.setLight(id, light);
                refreshViewports();
                rebuildOptimizationWarnings();
            }
        });
    }
}

} // namespace projectunity::editor
