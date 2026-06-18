#include <projectunity/editor/SettingsDialog.hpp>

#include <projectunity/renderer/RendererTypes.hpp>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVariant>
#include <QVBoxLayout>

#include <algorithm>
#include <array>

namespace projectunity::editor {
namespace {

constexpr int kCustomFpsValue = -1;

[[nodiscard]] QString spanishPresetName(QualityPreset preset)
{
    switch (preset) {
    case QualityPreset::Low: return QStringLiteral("Bajo");
    case QualityPreset::Medium: return QStringLiteral("Medio");
    case QualityPreset::High: return QStringLiteral("Alto");
    case QualityPreset::Ultra: return QStringLiteral("Ultra");
    case QualityPreset::Custom: return QStringLiteral("Personalizado");
    }
    return QStringLiteral("Personalizado");
}

[[nodiscard]] QString spanishTextureQuality(renderer::RenderTextureQuality quality)
{
    switch (quality) {
    case renderer::RenderTextureQuality::Low: return QStringLiteral("Bajo");
    case renderer::RenderTextureQuality::Medium: return QStringLiteral("Medio");
    case renderer::RenderTextureQuality::High: return QStringLiteral("Alto");
    case renderer::RenderTextureQuality::Ultra: return QStringLiteral("Ultra");
    }
    return QStringLiteral("Alto");
}

[[nodiscard]] QString fpsText(int fps)
{
    return fps <= 0 ? QStringLiteral("Sin límite") : QStringLiteral("%1 FPS").arg(fps);
}

[[nodiscard]] QString anisotropyText(int level)
{
    if (level < 0) {
        return QStringLiteral("Automático / máximo soportado");
    }
    return level <= 1 ? QStringLiteral("Off") : QStringLiteral("%1x").arg(level);
}

[[nodiscard]] QLabel* makeTitle(const QString& text)
{
    auto* label = new QLabel(text);
    label->setObjectName(QStringLiteral("SettingsCardTitle"));
    return label;
}

[[nodiscard]] QLabel* makeHint(const QString& text)
{
    auto* label = new QLabel(text);
    label->setObjectName(QStringLiteral("SettingsHint"));
    label->setWordWrap(true);
    return label;
}

[[nodiscard]] QLabel* makePill(const QString& text, const QString& state = QStringLiteral("neutral"))
{
    auto* label = new QLabel(text);
    label->setObjectName(QStringLiteral("SettingsPill"));
    label->setProperty("state", state);
    label->setAlignment(Qt::AlignCenter);
    return label;
}

[[nodiscard]] QFrame* makeCard(const QString& title, const QString& hint = {})
{
    auto* card = new QFrame;
    card->setObjectName(QStringLiteral("SettingsCard"));
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(12);
    layout->addWidget(makeTitle(title));
    if (!hint.isEmpty()) {
        layout->addWidget(makeHint(hint));
    }
    return card;
}

[[nodiscard]] QGridLayout* cardGrid(QFrame* card)
{
    auto* layout = qobject_cast<QVBoxLayout*>(card->layout());
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(10);
    grid->setColumnStretch(1, 1);
    layout->addLayout(grid);
    return grid;
}

void addRow(QGridLayout* grid, int row, const QString& label, QWidget* control, QWidget* status = nullptr)
{
    auto* labelWidget = new QLabel(label);
    labelWidget->setObjectName(QStringLiteral("SettingsRowLabel"));
    grid->addWidget(labelWidget, row, 0);
    grid->addWidget(control, row, 1);
    if (status != nullptr) {
        grid->addWidget(status, row, 2);
    }
}

[[nodiscard]] QWidget* disabledPendingControl(QWidget* control, const QString& tooltip = QStringLiteral("No disponible todavía"))
{
    control->setEnabled(false);
    control->setToolTip(tooltip);
    return control;
}

template <typename Enum>
void addEnumItem(QComboBox* combo, const QString& label, Enum value)
{
    combo->addItem(label, static_cast<int>(value));
}

void setComboData(QComboBox* combo, int value)
{
    if (combo == nullptr) {
        return;
    }
    const auto index = combo->findData(value);
    combo->setCurrentIndex(index >= 0 ? index : 0);
}

[[nodiscard]] int comboData(const QComboBox* combo, int fallback)
{
    if (combo == nullptr || combo->currentIndex() < 0) {
        return fallback;
    }
    return combo->currentData().toInt();
}

[[nodiscard]] QComboBox* makePendingCombo(const QStringList& values)
{
    auto* combo = new QComboBox;
    combo->addItems(values);
    return qobject_cast<QComboBox*>(disabledPendingControl(combo));
}

[[nodiscard]] QWidget* sliderWithValue(QSlider*& slider, QLabel*& value, int minimum, int maximum)
{
    auto* row = new QWidget;
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    slider = new QSlider(Qt::Horizontal);
    slider->setRange(minimum, maximum);
    value = new QLabel;
    value->setObjectName(QStringLiteral("SettingsValueLabel"));
    value->setMinimumWidth(58);
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(slider, 1);
    layout->addWidget(value);
    return row;
}

[[nodiscard]] QWidget* createPageContainer()
{
    auto* container = new QWidget;
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(14);
    layout->addStretch();
    return container;
}

void addCard(QWidget* page, QFrame* card)
{
    auto* layout = qobject_cast<QVBoxLayout*>(page->layout());
    layout->insertWidget(layout->count() - 1, card);
}

} // namespace

SettingsDialog::SettingsDialog(
    EditorQualitySettings settings,
    const renderer::RendererStats* rendererStats,
    QWidget* parent)
    : QDialog(parent)
    , settings_(settings)
{
    if (rendererStats != nullptr) {
        rendererStats_ = *rendererStats;
        hasRendererStats_ = true;
    }

    setObjectName(QStringLiteral("SettingsDialog"));
    setWindowTitle(QStringLiteral("Ajustes de ProjectUnity"));
    setMinimumSize(980, 680);
    resize(1120, 760);
    setModal(false);
    setStyleSheet(QStringLiteral(R"(
#SettingsDialog {
    background-color: #15181d;
}
#SettingsHeader {
    font-size: 22px;
    font-weight: 700;
    color: #f3f6fb;
}
#SettingsSubheader {
    color: #8d99a8;
}
#SettingsSidebar {
    background-color: #101318;
    border: 1px solid #252b34;
    border-radius: 8px;
    padding: 8px;
}
#SettingsSidebar::item {
    min-height: 38px;
    padding: 8px 10px;
    border-radius: 6px;
    color: #b8c2d0;
}
#SettingsSidebar::item:hover {
    background-color: #202733;
    color: #f3f6fb;
}
#SettingsSidebar::item:selected {
    background-color: #2c5f8f;
    color: #ffffff;
}
#SettingsCard {
    background-color: #1c2129;
    border: 1px solid #2b333f;
    border-radius: 8px;
}
#SettingsCardTitle {
    font-size: 15px;
    font-weight: 700;
    color: #f2f5f9;
}
#SettingsHint {
    color: #94a0af;
}
#SettingsRowLabel {
    color: #c6cfdb;
}
#SettingsValueLabel {
    color: #dce4ef;
}
#SettingsPill {
    background-color: #252d38;
    border: 1px solid #354151;
    border-radius: 6px;
    padding: 4px 8px;
    color: #b8c2d0;
}
#SettingsPill[state="ok"] {
    background-color: #20382f;
    border-color: #33664e;
    color: #a6e7c1;
}
#SettingsPill[state="pending"] {
    background-color: #332f24;
    border-color: #665637;
    color: #e4c784;
}
#SettingsPill[state="bad"] {
    background-color: #3a2428;
    border-color: #7a3d48;
    color: #ef9aa7;
}
#SettingsDialog QComboBox,
#SettingsDialog QSpinBox {
    min-height: 30px;
}
#SettingsDialog QCheckBox {
    spacing: 8px;
}
#SettingsDialog QPushButton {
    min-height: 32px;
    padding-left: 14px;
    padding-right: 14px;
}
)"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 16);
    root->setSpacing(14);

    auto* headerRow = new QHBoxLayout;
    auto* titleColumn = new QVBoxLayout;
    auto* title = new QLabel(QStringLiteral("Ajustes de ProjectUnity"));
    title->setObjectName(QStringLiteral("SettingsHeader"));
    auto* subtitle = new QLabel(QStringLiteral("Gráficos, rendimiento, texturas, sombras, LOD/HLOD, editor y debug."));
    subtitle->setObjectName(QStringLiteral("SettingsSubheader"));
    titleColumn->addWidget(title);
    titleColumn->addWidget(subtitle);
    headerRow->addLayout(titleColumn, 1);
    headerRow->addWidget(makePill(QStringLiteral("Qt Widgets / Vulkan"), QStringLiteral("ok")));
    root->addLayout(headerRow);

    auto* body = new QHBoxLayout;
    body->setSpacing(14);
    sidebar_ = new QListWidget;
    sidebar_->setObjectName(QStringLiteral("SettingsSidebar"));
    sidebar_->setFixedWidth(210);
    sidebar_->setIconSize(QSize(18, 18));
    sidebar_->setFrameShape(QFrame::NoFrame);

    const std::array<std::pair<QString, QString>, 8> sections {{
        {QStringLiteral(":/icons/graphics.svg"), QStringLiteral("Gráficos")},
        {QStringLiteral(":/icons/performance.svg"), QStringLiteral("Rendimiento")},
        {QStringLiteral(":/icons/textures.svg"), QStringLiteral("Texturas")},
        {QStringLiteral(":/icons/shadows.svg"), QStringLiteral("Sombras")},
        {QStringLiteral(":/icons/terrain.svg"), QStringLiteral("Terreno")},
        {QStringLiteral(":/icons/lod.svg"), QStringLiteral("LOD / HLOD")},
        {QStringLiteral(":/icons/editor.svg"), QStringLiteral("Editor")},
        {QStringLiteral(":/icons/debug.svg"), QStringLiteral("Debug")},
    }};
    for (const auto& [icon, text] : sections) {
        sidebar_->addItem(new QListWidgetItem(QIcon(icon), text));
    }

    pages_ = new QStackedWidget;
    pages_->addWidget(createGraphicsPage());
    pages_->addWidget(createPerformancePage());
    pages_->addWidget(createTexturesPage());
    pages_->addWidget(createShadowsPage());
    pages_->addWidget(createTerrainPage());
    pages_->addWidget(createLodPage());
    pages_->addWidget(createEditorPage());
    pages_->addWidget(createDebugPage());
    body->addWidget(sidebar_);
    body->addWidget(pages_, 1);
    root->addLayout(body, 1);

    connect(sidebar_, &QListWidget::currentRowChanged, pages_, &QStackedWidget::setCurrentIndex);
    sidebar_->setCurrentRow(0);

    auto* footer = new QHBoxLayout;
    footerStatus_ = new QLabel(QStringLiteral("Los cambios seguros se aplican al viewport activo; algunos requieren reiniciar el renderer."));
    footerStatus_->setObjectName(QStringLiteral("SettingsSubheader"));
    footer->addWidget(footerStatus_, 1);
    auto* restoreRecommended = new QPushButton(QIcon(QStringLiteral(":/icons/restore.svg")), QStringLiteral("Restaurar recomendados"));
    auto* restoreDefaults = new QPushButton(QStringLiteral("Restaurar por defecto"));
    auto* apply = new QPushButton(QIcon(QStringLiteral(":/icons/apply.svg")), QStringLiteral("Aplicar"));
    auto* close = new QPushButton(QStringLiteral("Cerrar"));
    footer->addWidget(restoreRecommended);
    footer->addWidget(restoreDefaults);
    footer->addWidget(apply);
    footer->addWidget(close);
    root->addLayout(footer);

    connect(restoreRecommended, &QPushButton::clicked, this, [this]() {
        applyQualityPreset(QualityPreset::High, settings_);
        refreshControls();
    });
    connect(restoreDefaults, &QPushButton::clicked, this, [this]() {
        settings_ = EditorQualitySettings {};
        refreshControls();
    });
    connect(apply, &QPushButton::clicked, this, [this]() {
        emitApply();
    });
    connect(close, &QPushButton::clicked, this, &QDialog::reject);

    refreshControls();
}

const EditorQualitySettings& SettingsDialog::settings() const noexcept
{
    return settings_;
}

QWidget* SettingsDialog::createGraphicsPage()
{
    auto* page = createPageContainer();

    auto* quality = makeCard(
        QStringLiteral("Calidad general"),
        QStringLiteral("El preset ajusta valores conectados y deja como Personalizado cualquier cambio manual."));
    auto* grid = cardGrid(quality);
    presetCombo_ = new QComboBox;
    addEnumItem(presetCombo_, QStringLiteral("Bajo"), QualityPreset::Low);
    addEnumItem(presetCombo_, QStringLiteral("Medio"), QualityPreset::Medium);
    addEnumItem(presetCombo_, QStringLiteral("Alto"), QualityPreset::High);
    addEnumItem(presetCombo_, QStringLiteral("Ultra"), QualityPreset::Ultra);
    addEnumItem(presetCombo_, QStringLiteral("Personalizado"), QualityPreset::Custom);
    addRow(grid, 0, QStringLiteral("Preset"), presetCombo_);

    auto* renderLabel = new QLabel(hasRendererStats_ ? QStringLiteral("Render activo: Vulkan") : QStringLiteral("Render activo: no inicializado"));
    addRow(grid, 1, QStringLiteral("Modo de render"), renderLabel, makePill(QStringLiteral("Real"), hasRendererStats_ ? QStringLiteral("ok") : QStringLiteral("pending")));

    vsyncCheck_ = new QCheckBox(QStringLiteral("Activado"));
    vsyncCheck_->setToolTip(QStringLiteral("Recrea la surface Vulkan del viewport."));
    addRow(grid, 2, QStringLiteral("VSync"), vsyncCheck_, makePill(QStringLiteral("Real"), QStringLiteral("ok")));

    auto* fpsRow = new QWidget;
    auto* fpsLayout = new QHBoxLayout(fpsRow);
    fpsLayout->setContentsMargins(0, 0, 0, 0);
    fpsLayout->setSpacing(8);
    fpsCombo_ = new QComboBox;
    fpsCombo_->addItem(QStringLiteral("Sin límite"), 0);
    for (const auto fps : {30, 60, 90, 120, 144, 165, 240}) {
        fpsCombo_->addItem(fpsText(fps), fps);
    }
    fpsCombo_->addItem(QStringLiteral("Personalizado"), kCustomFpsValue);
    fpsCustomSpin_ = new QSpinBox;
    fpsCustomSpin_->setRange(15, 1000);
    fpsCustomSpin_->setSuffix(QStringLiteral(" FPS"));
    fpsLayout->addWidget(fpsCombo_, 1);
    fpsLayout->addWidget(fpsCustomSpin_);
    addRow(grid, 3, QStringLiteral("Límite de FPS"), fpsRow, makePill(QStringLiteral("Real"), QStringLiteral("ok")));

    addCard(page, quality);

    auto* pending = makeCard(QStringLiteral("Viewport"), QStringLiteral("Opciones visibles para el roadmap, sin simular backend."));
    auto* pendingGrid = cardGrid(pending);
    addRow(pendingGrid, 0, QStringLiteral("Escala de resolución"), disabledPendingControl(new QComboBox), makePill(QStringLiteral("Pendiente"), QStringLiteral("pending")));
    qobject_cast<QComboBox*>(pendingGrid->itemAtPosition(0, 1)->widget())->addItems({QStringLiteral("50%"), QStringLiteral("75%"), QStringLiteral("100%"), QStringLiteral("125%"), QStringLiteral("150%"), QStringLiteral("200%")});
    addRow(pendingGrid, 1, QStringLiteral("Anti-aliasing"), makePendingCombo({QStringLiteral("Off"), QStringLiteral("FXAA"), QStringLiteral("MSAA 2x"), QStringLiteral("MSAA 4x"), QStringLiteral("TAA")}), makePill(QStringLiteral("Pendiente"), QStringLiteral("pending")));
    addRow(pendingGrid, 2, QStringLiteral("FOV Game View"), disabledPendingControl(new QSpinBox), makePill(QStringLiteral("Pendiente"), QStringLiteral("pending")));
    qobject_cast<QSpinBox*>(pendingGrid->itemAtPosition(2, 1)->widget())->setRange(30, 120);
    addCard(page, pending);

    const auto manualChange = [this]() {
        if (!syncingControls_) {
            markCustomPreset();
        }
    };
    connect(presetCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        if (syncingControls_) {
            return;
        }
        const auto preset = static_cast<QualityPreset>(comboData(presetCombo_, static_cast<int>(QualityPreset::Custom)));
        if (preset == QualityPreset::Custom) {
            settings_.graphics.preset = QualityPreset::Custom;
        } else {
            applyQualityPreset(preset, settings_);
            refreshControls();
        }
    });
    connect(vsyncCheck_, &QCheckBox::toggled, this, manualChange);
    connect(fpsCombo_, &QComboBox::currentIndexChanged, this, manualChange);
    connect(fpsCustomSpin_, &QSpinBox::valueChanged, this, manualChange);
    return page;
}

QWidget* SettingsDialog::createPerformancePage()
{
    auto* page = createPageContainer();
    auto* metrics = makeCard(QStringLiteral("Métricas actuales"), QStringLiteral("Lectura directa de RendererStats cuando hay frame disponible."));
    auto* grid = cardGrid(metrics);
    const auto fps = hasRendererStats_ ? QStringLiteral("%1").arg(rendererStats_.FPS, 0, 'f', 1) : QStringLiteral("-");
    const auto cpu = hasRendererStats_ ? QStringLiteral("%1 ms").arg(static_cast<double>(rendererStats_.lastFrameRenderCpuTimeUs) / 1000.0, 0, 'f', 2) : QStringLiteral("-");
    const auto gpu = hasRendererStats_ && rendererStats_.lastFrameGpuTimestampsValid
        ? QStringLiteral("%1 ms").arg(static_cast<double>(rendererStats_.lastFrameGpuTimeUs) / 1000.0, 0, 'f', 2)
        : QStringLiteral("-");
    addRow(grid, 0, QStringLiteral("FPS actual"), new QLabel(fps));
    addRow(grid, 1, QStringLiteral("Frame CPU"), new QLabel(cpu));
    addRow(grid, 2, QStringLiteral("Frame GPU"), new QLabel(gpu));
    addRow(grid, 3, QStringLiteral("Draw calls"), new QLabel(hasRendererStats_ ? QString::number(static_cast<qulonglong>(rendererStats_.vkDrawIndexed)) : QStringLiteral("-")));
    addRow(grid, 4, QStringLiteral("Triángulos"), new QLabel(hasRendererStats_ ? QString::number(static_cast<qulonglong>(rendererStats_.trianglesSubmitted)) : QStringLiteral("-")));
    addRow(grid, 5, QStringLiteral("Batches"), new QLabel(hasRendererStats_ ? QString::number(static_cast<qulonglong>(rendererStats_.lastFrameMeshBatchCount)) : QStringLiteral("-")));
    addRow(grid, 6, QStringLiteral("Shadow draw calls"), new QLabel(hasRendererStats_ ? QString::number(static_cast<qulonglong>(rendererStats_.shadowBatchesSubmitted)) : QStringLiteral("-")));
    addRow(grid, 7, QStringLiteral("Uploads/frame"), new QLabel(hasRendererStats_ ? QStringLiteral("M%1 T%2").arg(static_cast<qulonglong>(rendererStats_.lastFrameMeshUploadCount)).arg(static_cast<qulonglong>(rendererStats_.lastFrameTextureUploadCount)) : QStringLiteral("-")));
    addCard(page, metrics);

    auto* options = makeCard(QStringLiteral("Modo rendimiento"));
    auto* optionsGrid = cardGrid(options);
    performanceModeCombo_ = new QComboBox;
    addEnumItem(performanceModeCombo_, QStringLiteral("Calidad"), PerformanceMode::Quality);
    addEnumItem(performanceModeCombo_, QStringLiteral("Balanceado"), PerformanceMode::Balanced);
    addEnumItem(performanceModeCombo_, QStringLiteral("Rendimiento"), PerformanceMode::Performance);
    addRow(optionsGrid, 0, QStringLiteral("Modo"), performanceModeCombo_);
    addRow(optionsGrid, 1, QStringLiteral("Overlay FPS"), disabledPendingControl(new QCheckBox(QStringLiteral("Mostrar"))), makePill(QStringLiteral("Pendiente"), QStringLiteral("pending")));
    addRow(optionsGrid, 2, QStringLiteral("Profiler overlay"), disabledPendingControl(new QCheckBox(QStringLiteral("Mostrar"))), makePill(QStringLiteral("Pendiente"), QStringLiteral("pending")));
    addRow(optionsGrid, 3, QStringLiteral("Stats avanzadas"), disabledPendingControl(new QCheckBox(QStringLiteral("Mostrar"))), makePill(QStringLiteral("Pendiente"), QStringLiteral("pending")));
    addCard(page, options);

    connect(performanceModeCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!syncingControls_) {
            markCustomPreset();
        }
    });
    return page;
}

QWidget* SettingsDialog::createTexturesPage()
{
    auto* page = createPageContainer();
    auto* quality = makeCard(
        QStringLiteral("Texturas y filtrado"),
        QStringLiteral("Anisotropía y mip bias se pasan al VkSampler; cambiar la política recrea renderer/samplers."));
    auto* grid = cardGrid(quality);
    textureQualityCombo_ = new QComboBox;
    addEnumItem(textureQualityCombo_, QStringLiteral("Bajo"), renderer::RenderTextureQuality::Low);
    addEnumItem(textureQualityCombo_, QStringLiteral("Medio"), renderer::RenderTextureQuality::Medium);
    addEnumItem(textureQualityCombo_, QStringLiteral("Alto"), renderer::RenderTextureQuality::High);
    addEnumItem(textureQualityCombo_, QStringLiteral("Ultra"), renderer::RenderTextureQuality::Ultra);
    addRow(grid, 0, QStringLiteral("Calidad"), textureQualityCombo_, makePill(QStringLiteral("Recrea renderer"), QStringLiteral("ok")));

    anisotropyCombo_ = new QComboBox;
    anisotropyCombo_->addItem(QStringLiteral("Off"), 1);
    for (const auto level : {2, 4, 8, 16}) {
        anisotropyCombo_->addItem(QStringLiteral("%1x").arg(level), level);
    }
    anisotropyCombo_->addItem(QStringLiteral("Automático / máximo soportado"), -1);
    const auto anisotropySupported = !hasRendererStats_ || rendererStats_.samplerAnisotropySupported;
    anisotropyCombo_->setEnabled(anisotropySupported);
    addRow(
        grid,
        1,
        QStringLiteral("Filtro anisotrópico"),
        anisotropyCombo_,
        makePill(anisotropySupported ? QStringLiteral("VkSampler") : QStringLiteral("No soportado por la GPU"), anisotropySupported ? QStringLiteral("ok") : QStringLiteral("bad")));

    addRow(grid, 2, QStringLiteral("Filtrado global"), makePendingCombo({QStringLiteral("Bilinear"), QStringLiteral("Trilinear"), QStringLiteral("Anisotropic")}), makePill(QStringLiteral("Pendiente"), QStringLiteral("pending")));
    addRow(grid, 3, QStringLiteral("Mipmaps"), disabledPendingControl(new QCheckBox(QStringLiteral("Activados"))), makePill(QStringLiteral("Pendiente"), QStringLiteral("pending")));
    auto* mipBias = sliderWithValue(mipBiasSlider_, mipBiasValue_, -100, 100);
    mipBiasSlider_->setToolTip(QStringLiteral("Valores negativos aumentan nitidez pero pueden causar shimmer."));
    addRow(grid, 4, QStringLiteral("Bias de mipmap"), mipBias, makePill(QStringLiteral("Recrea sampler"), QStringLiteral("ok")));
    auto* recreate = new QPushButton(QStringLiteral("Recrear samplers"));
    (void)disabledPendingControl(recreate, QStringLiteral("Los samplers se recrean al aplicar cambios de política."));
    addRow(grid, 5, QStringLiteral("Samplers"), recreate, makePill(QStringLiteral("Auto"), QStringLiteral("ok")));
    addCard(page, quality);

    auto* diagnostics = makeCard(QStringLiteral("Textura / sampler reciente"));
    auto* diag = cardGrid(diagnostics);
    addRow(diag, 0, QStringLiteral("Resolución"), new QLabel(hasRendererStats_ ? QStringLiteral("%1x%2").arg(rendererStats_.lastTextureSamplerWidth).arg(rendererStats_.lastTextureSamplerHeight) : QStringLiteral("-")));
    addRow(diag, 1, QStringLiteral("Mip levels"), new QLabel(hasRendererStats_ ? QString::number(rendererStats_.lastTextureSamplerMipLevels) : QStringLiteral("-")));
    addRow(diag, 2, QStringLiteral("Anisotropía activa"), new QLabel(hasRendererStats_ ? QStringLiteral("%1 max %2").arg(rendererStats_.lastTextureSamplerAnisotropyEnabled ? QStringLiteral("Sí") : QStringLiteral("No")).arg(rendererStats_.lastTextureSamplerMaxAnisotropy, 0, 'f', 1) : QStringLiteral("-")));
    addRow(diag, 3, QStringLiteral("minLod / maxLod"), new QLabel(hasRendererStats_ ? QStringLiteral("%1 / %2").arg(rendererStats_.lastTextureSamplerMinLod, 0, 'f', 2).arg(rendererStats_.lastTextureSamplerMaxLod, 0, 'f', 2) : QStringLiteral("-")));
    addRow(diag, 4, QStringLiteral("mipLodBias"), new QLabel(hasRendererStats_ ? QStringLiteral("%1").arg(rendererStats_.lastTextureSamplerMipLodBias, 0, 'f', 2) : QStringLiteral("-")));
    addRow(diag, 5, QStringLiteral("Rol"), new QLabel(hasRendererStats_ ? QString::fromStdString(rendererStats_.lastTextureSamplerRole) : QStringLiteral("-")));
    addCard(page, diagnostics);

    const auto manualChange = [this]() {
        if (!syncingControls_) {
            markCustomPreset();
        }
    };
    connect(textureQualityCombo_, &QComboBox::currentIndexChanged, this, manualChange);
    connect(anisotropyCombo_, &QComboBox::currentIndexChanged, this, manualChange);
    connect(mipBiasSlider_, &QSlider::valueChanged, this, [this, manualChange](int value) {
        if (mipBiasValue_ != nullptr) {
            mipBiasValue_->setText(QStringLiteral("%1").arg(static_cast<double>(value) / 100.0, 0, 'f', 2));
        }
        manualChange();
    });
    return page;
}

QWidget* SettingsDialog::createShadowsPage()
{
    auto* page = createPageContainer();
    auto* card = makeCard(QStringLiteral("Sombras"), QStringLiteral("El modo de actualización está conectado; resolución y filtros siguen pendientes."));
    auto* grid = cardGrid(card);
    shadowModeCombo_ = new QComboBox;
    shadowModeCombo_->addItem(QStringLiteral("Off"), static_cast<int>(renderer::RenderShadowUpdateMode::Off));
    shadowModeCombo_->addItem(QStringLiteral("Live"), static_cast<int>(renderer::RenderShadowUpdateMode::Live));
    shadowModeCombo_->addItem(QStringLiteral("Frozen"), static_cast<int>(renderer::RenderShadowUpdateMode::Frozen));
    addRow(grid, 0, QStringLiteral("Modo"), shadowModeCombo_, makePill(QStringLiteral("Real"), QStringLiteral("ok")));
    addRow(grid, 1, QStringLiteral("Calidad"), makePendingCombo({QStringLiteral("Off"), QStringLiteral("Bajo"), QStringLiteral("Medio"), QStringLiteral("Alto"), QStringLiteral("Ultra")}), makePill(QStringLiteral("Pendiente"), QStringLiteral("pending")));
    addRow(grid, 2, QStringLiteral("Resolución"), makePendingCombo({QStringLiteral("512"), QStringLiteral("1024"), QStringLiteral("2048"), QStringLiteral("4096"), QStringLiteral("8192")}), makePill(QStringLiteral("Pendiente"), QStringLiteral("pending")));
    addRow(grid, 3, QStringLiteral("Cascadas"), makePendingCombo({QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"), QStringLiteral("4")}), makePill(QStringLiteral("Pendiente"), QStringLiteral("pending")));
    addRow(grid, 4, QStringLiteral("Distancia"), disabledPendingControl(new QSlider(Qt::Horizontal)), makePill(QStringLiteral("Pendiente"), QStringLiteral("pending")));
    addRow(grid, 5, QStringLiteral("Soft shadows / PCF"), makePendingCombo({QStringLiteral("Hard"), QStringLiteral("Soft bajo"), QStringLiteral("Soft medio"), QStringLiteral("Soft alto")}), makePill(QStringLiteral("Pendiente"), QStringLiteral("pending")));
    addRow(grid, 6, QStringLiteral("Shadow caster budget"), disabledPendingControl(new QSpinBox), makePill(QStringLiteral("Usa LOD/HLOD"), QStringLiteral("ok")));
    addCard(page, card);

    auto* stats = makeCard(QStringLiteral("Contadores"));
    auto* statsGrid = cardGrid(stats);
    addRow(statsGrid, 0, QStringLiteral("Shadow casters"), new QLabel(hasRendererStats_ ? QString::number(static_cast<qulonglong>(rendererStats_.lastFrameShadowCasterCount)) : QStringLiteral("-")));
    addRow(statsGrid, 1, QStringLiteral("Shadow draw calls"), new QLabel(hasRendererStats_ ? QString::number(static_cast<qulonglong>(rendererStats_.shadowBatchesSubmitted)) : QStringLiteral("-")));
    addRow(statsGrid, 2, QStringLiteral("Shadow GPU ms"), new QLabel(hasRendererStats_ && rendererStats_.lastFrameGpuTimestampsValid ? QStringLiteral("%1").arg(rendererStats_.shadowGpuMs, 0, 'f', 3) : QStringLiteral("-")));
    addCard(page, stats);

    connect(shadowModeCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!syncingControls_) {
            markCustomPreset();
        }
    });
    return page;
}

QWidget* SettingsDialog::createTerrainPage()
{
    auto* page = createPageContainer();
    auto* card = makeCard(QStringLiteral("Terreno"), QStringLiteral("La generación por entidad vive en el panel Terreno; aquí se exponen defaults globales cuando exista backend."));
    auto* grid = cardGrid(card);
    terrainQualityCombo_ = new QComboBox;
    addEnumItem(terrainQualityCombo_, QStringLiteral("Bajo"), TerrainQuality::Low);
    addEnumItem(terrainQualityCombo_, QStringLiteral("Medio"), TerrainQuality::Medium);
    addEnumItem(terrainQualityCombo_, QStringLiteral("Alto"), TerrainQuality::High);
    addEnumItem(terrainQualityCombo_, QStringLiteral("Ultra"), TerrainQuality::Ultra);
    addRow(grid, 0, QStringLiteral("Calidad de terreno"), terrainQualityCombo_);
    addRow(grid, 1, QStringLiteral("Resolución preview"), makePendingCombo({QStringLiteral("Bajo"), QStringLiteral("Medio"), QStringLiteral("Alto")}), makePill(QStringLiteral("Pendiente"), QStringLiteral("pending")));
    addRow(grid, 2, QStringLiteral("Distancia LOD"), disabledPendingControl(new QSlider(Qt::Horizontal)), makePill(QStringLiteral("Pendiente"), QStringLiteral("pending")));
    addRow(grid, 3, QStringLiteral("Normales"), disabledPendingControl(new QCheckBox(QStringLiteral("Generar"))), makePill(QStringLiteral("Por entidad"), QStringLiteral("pending")));
    addRow(grid, 4, QStringLiteral("Tangentes"), disabledPendingControl(new QCheckBox(QStringLiteral("Generar"))), makePill(QStringLiteral("Por entidad"), QStringLiteral("pending")));
    addRow(grid, 5, QStringLiteral("Pintura de textura"), new QLabel(QStringLiteral("Pendiente")), makePill(QStringLiteral("Pendiente"), QStringLiteral("pending")));
    addRow(grid, 6, QStringLiteral("Follaje / vegetación"), makePendingCombo({QStringLiteral("Off"), QStringLiteral("Bajo"), QStringLiteral("Medio"), QStringLiteral("Alto"), QStringLiteral("Ultra")}), makePill(QStringLiteral("Pendiente"), QStringLiteral("pending")));
    addRow(grid, 7, QStringLiteral("Colliders"), new QLabel(QStringLiteral("Básico / parcial")), makePill(QStringLiteral("Real parcial"), QStringLiteral("pending")));
    addCard(page, card);
    connect(terrainQualityCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!syncingControls_) {
            markCustomPreset();
        }
    });
    return page;
}

QWidget* SettingsDialog::createLodPage()
{
    auto* page = createPageContainer();
    auto* card = makeCard(QStringLiteral("LOD / HLOD"), QStringLiteral("Estos valores alimentan RenderWorld sin reiniciar y sustituyen los env vars como fuente viva."));
    auto* grid = cardGrid(card);
    addRow(grid, 0, QStringLiteral("LOD automático"), disabledPendingControl(new QCheckBox(QStringLiteral("Activado"))), makePill(QStringLiteral("Pendiente"), QStringLiteral("pending")));
    lodQualityCombo_ = new QComboBox;
    addEnumItem(lodQualityCombo_, QStringLiteral("Rendimiento"), LodQuality::Performance);
    addEnumItem(lodQualityCombo_, QStringLiteral("Balanceado"), LodQuality::Balanced);
    addEnumItem(lodQualityCombo_, QStringLiteral("Calidad"), LodQuality::Quality);
    addEnumItem(lodQualityCombo_, QStringLiteral("Ultra"), LodQuality::Ultra);
    addRow(grid, 1, QStringLiteral("Calidad LOD"), lodQualityCombo_, makePill(QStringLiteral("Real"), QStringLiteral("ok")));
    auto* distance = sliderWithValue(lodDistanceSlider_, lodDistanceValue_, 0, 100);
    addRow(grid, 2, QStringLiteral("Distancia HLOD"), distance, makePill(QStringLiteral("Real"), QStringLiteral("ok")));
    hlodCheck_ = new QCheckBox(QStringLiteral("Activado"));
    addRow(grid, 3, QStringLiteral("HLOD"), hlodCheck_, makePill(QStringLiteral("Real"), QStringLiteral("ok")));
    hlodAggressivenessCombo_ = new QComboBox;
    addEnumItem(hlodAggressivenessCombo_, QStringLiteral("Baja"), HlodAggressiveness::Low);
    addEnumItem(hlodAggressivenessCombo_, QStringLiteral("Media"), HlodAggressiveness::Medium);
    addEnumItem(hlodAggressivenessCombo_, QStringLiteral("Alta"), HlodAggressiveness::High);
    addRow(grid, 4, QStringLiteral("Agresividad HLOD"), hlodAggressivenessCombo_, makePill(QStringLiteral("Real"), QStringLiteral("ok")));
    auto* screen = sliderWithValue(screenErrorSlider_, screenErrorValue_, 5, 400);
    addRow(grid, 5, QStringLiteral("Screen error"), screen, makePill(QStringLiteral("Real"), QStringLiteral("ok")));
    auto* hysteresis = sliderWithValue(hysteresisSlider_, hysteresisValue_, 0, 45);
    hysteresisSlider_->setToolTip(QStringLiteral("Reduce popping al cambiar de LOD."));
    addRow(grid, 6, QStringLiteral("Hysteresis"), hysteresis, makePill(QStringLiteral("Real"), QStringLiteral("ok")));
    chunkBudgetSpin_ = new QSpinBox;
    chunkBudgetSpin_->setRange(1, 1000000);
    addRow(grid, 7, QStringLiteral("Chunk budget"), chunkBudgetSpin_, makePill(QStringLiteral("Real"), QStringLiteral("ok")));
    drawPacketBudgetSpin_ = new QSpinBox;
    drawPacketBudgetSpin_->setRange(1, 1000000);
    addRow(grid, 8, QStringLiteral("Draw packet budget"), drawPacketBudgetSpin_, makePill(QStringLiteral("Real"), QStringLiteral("ok")));
    shadowCasterBudgetSpin_ = new QSpinBox;
    shadowCasterBudgetSpin_->setRange(1, 1000000);
    addRow(grid, 9, QStringLiteral("Shadow caster budget"), shadowCasterBudgetSpin_, makePill(QStringLiteral("Real"), QStringLiteral("ok")));
    lodDebugColorsCheck_ = new QCheckBox(QStringLiteral("Rojo LOD0, amarillo medio, morado HLOD"));
    addRow(grid, 10, QStringLiteral("Debug LOD colors"), lodDebugColorsCheck_, makePill(QStringLiteral("Real"), QStringLiteral("ok")));
    hlodOverrideCombo_ = new QComboBox;
    hlodOverrideCombo_->addItem(QStringLiteral("Automático"), static_cast<int>(ViewportHlodDebugOverride::Automatic));
    hlodOverrideCombo_->addItem(QStringLiteral("Detallado"), static_cast<int>(ViewportHlodDebugOverride::ForceDetailed));
    hlodOverrideCombo_->addItem(QStringLiteral("HLOD forzado"), static_cast<int>(ViewportHlodDebugOverride::ForceHlod));
    addRow(grid, 11, QStringLiteral("Override debug"), hlodOverrideCombo_, makePill(QStringLiteral("Real"), QStringLiteral("ok")));
    terrainNearHighQualityCheck_ = new QCheckBox(QStringLiteral("Forzar LOD0 cerca del jugador"));
    addRow(grid, 12, QStringLiteral("Terrain near HQ"), terrainNearHighQualityCheck_, makePill(QStringLiteral("Debug"), QStringLiteral("ok")));
    terrainNearHighQualityRadiusSpin_ = new QSpinBox;
    terrainNearHighQualityRadiusSpin_->setRange(0, 1000000);
    terrainNearHighQualityRadiusSpin_->setSuffix(QStringLiteral(" m"));
    addRow(grid, 13, QStringLiteral("Terrain HQ radius"), terrainNearHighQualityRadiusSpin_, makePill(QStringLiteral("Real"), QStringLiteral("ok")));
    debugDisableTerrainHlodCheck_ = new QCheckBox(QStringLiteral("No usar HLOD para terrain"));
    addRow(grid, 14, QStringLiteral("Disable terrain HLOD"), debugDisableTerrainHlodCheck_, makePill(QStringLiteral("Debug"), QStringLiteral("ok")));
    debugDisableTerrainChunkLodCheck_ = new QCheckBox(QStringLiteral("No degradar chunks de terrain"));
    addRow(grid, 15, QStringLiteral("Disable terrain chunk LOD"), debugDisableTerrainChunkLodCheck_, makePill(QStringLiteral("Debug"), QStringLiteral("ok")));
    addCard(page, card);

    const auto manualChange = [this]() {
        if (!syncingControls_) {
            markCustomPreset();
        }
    };
    connect(lodQualityCombo_, &QComboBox::currentIndexChanged, this, manualChange);
    connect(hlodCheck_, &QCheckBox::toggled, this, manualChange);
    connect(hlodAggressivenessCombo_, &QComboBox::currentIndexChanged, this, manualChange);
    connect(lodDistanceSlider_, &QSlider::valueChanged, this, [this, manualChange](int value) {
        if (lodDistanceValue_ != nullptr) {
            lodDistanceValue_->setText(QStringLiteral("%1 m").arg(value));
        }
        manualChange();
    });
    connect(screenErrorSlider_, &QSlider::valueChanged, this, [this, manualChange](int value) {
        if (screenErrorValue_ != nullptr) {
            screenErrorValue_->setText(QStringLiteral("%1").arg(static_cast<double>(value) / 100.0, 0, 'f', 2));
        }
        manualChange();
    });
    connect(hysteresisSlider_, &QSlider::valueChanged, this, [this, manualChange](int value) {
        if (hysteresisValue_ != nullptr) {
            hysteresisValue_->setText(QStringLiteral("%1").arg(static_cast<double>(value) / 100.0, 0, 'f', 2));
        }
        manualChange();
    });
    connect(chunkBudgetSpin_, &QSpinBox::valueChanged, this, manualChange);
    connect(drawPacketBudgetSpin_, &QSpinBox::valueChanged, this, manualChange);
    connect(shadowCasterBudgetSpin_, &QSpinBox::valueChanged, this, manualChange);
    connect(terrainNearHighQualityCheck_, &QCheckBox::toggled, this, manualChange);
    connect(terrainNearHighQualityRadiusSpin_, &QSpinBox::valueChanged, this, manualChange);
    connect(debugDisableTerrainHlodCheck_, &QCheckBox::toggled, this, manualChange);
    connect(debugDisableTerrainChunkLodCheck_, &QCheckBox::toggled, this, manualChange);
    connect(lodDebugColorsCheck_, &QCheckBox::toggled, this, manualChange);
    connect(hlodOverrideCombo_, &QComboBox::currentIndexChanged, this, manualChange);
    return page;
}

QWidget* SettingsDialog::createEditorPage()
{
    auto* page = createPageContainer();
    auto* card = makeCard(QStringLiteral("Editor"), QStringLiteral("Preferencias globales guardadas en QSettings."));
    auto* grid = cardGrid(card);
    addRow(grid, 0, QStringLiteral("Idioma"), makePendingCombo({QStringLiteral("Español"), QStringLiteral("Inglés")}), makePill(QStringLiteral("Español"), QStringLiteral("ok")));
    addRow(grid, 1, QStringLiteral("Tema"), makePendingCombo({QStringLiteral("Oscuro"), QStringLiteral("Claro"), QStringLiteral("Sistema")}), makePill(QStringLiteral("Oscuro actual"), QStringLiteral("ok")));
    addRow(grid, 2, QStringLiteral("Tamaño de UI"), makePendingCombo({QStringLiteral("Compacto"), QStringLiteral("Normal"), QStringLiteral("Grande")}), makePill(QStringLiteral("Pendiente"), QStringLiteral("pending")));
    addRow(grid, 3, QStringLiteral("Tamaño de iconos"), makePendingCombo({QStringLiteral("Pequeño"), QStringLiteral("Normal"), QStringLiteral("Grande")}), makePill(QStringLiteral("Pendiente"), QStringLiteral("pending")));
    addRow(grid, 4, QStringLiteral("Tooltips"), disabledPendingControl(new QCheckBox(QStringLiteral("Mostrar"))), makePill(QStringLiteral("Pendiente"), QStringLiteral("pending")));
    addRow(grid, 5, QStringLiteral("Toolbar iconos"), disabledPendingControl(new QCheckBox(QStringLiteral("Mostrar"))), makePill(QStringLiteral("Pendiente"), QStringLiteral("pending")));
    addRow(grid, 6, QStringLiteral("Toolbar texto"), disabledPendingControl(new QCheckBox(QStringLiteral("Mostrar"))), makePill(QStringLiteral("Pendiente"), QStringLiteral("pending")));
    addCard(page, card);
    return page;
}

QWidget* SettingsDialog::createDebugPage()
{
    auto* page = createPageContainer();
    auto* card = makeCard(QStringLiteral("Debug / Perfilador"), QStringLiteral("Solo quedan habilitados overlays ya conectados al viewport."));
    auto* grid = cardGrid(card);
    debugWireframeCheck_ = new QCheckBox(QStringLiteral("Mostrar wireframe"));
    addRow(grid, 0, QStringLiteral("Wireframe"), debugWireframeCheck_, makePill(QStringLiteral("Real"), QStringLiteral("ok")));
    debugBoundsCheck_ = new QCheckBox(QStringLiteral("Mostrar bounds / XRay"));
    addRow(grid, 1, QStringLiteral("Bounds"), debugBoundsCheck_, makePill(QStringLiteral("Real"), QStringLiteral("ok")));
    addRow(grid, 2, QStringLiteral("Colliders"), disabledPendingControl(new QCheckBox(QStringLiteral("Mostrar"))), makePill(QStringLiteral("Pendiente"), QStringLiteral("pending")));
    addRow(grid, 3, QStringLiteral("Terrain brush"), disabledPendingControl(new QCheckBox(QStringLiteral("Mostrar"))), makePill(QStringLiteral("Panel Terreno"), QStringLiteral("pending")));
    debugLodColorsCheck_ = new QCheckBox(QStringLiteral("Mostrar colores LOD"));
    addRow(grid, 4, QStringLiteral("LOD colors"), debugLodColorsCheck_, makePill(QStringLiteral("Real"), QStringLiteral("ok")));
    debugShadowCastersCheck_ = new QCheckBox(QStringLiteral("Mostrar shadow casters"));
    addRow(grid, 5, QStringLiteral("Shadow casters"), debugShadowCastersCheck_, makePill(QStringLiteral("Real"), QStringLiteral("ok")));
    debugSourceObjectsCheck_ = new QCheckBox(QStringLiteral("Mostrar source objects"));
    addRow(grid, 6, QStringLiteral("Source objects"), debugSourceObjectsCheck_, makePill(QStringLiteral("Real"), QStringLiteral("ok")));
    debugSunDirectionCheck_ = new QCheckBox(QStringLiteral("Mostrar dirección del sol"));
    addRow(grid, 7, QStringLiteral("Sun direction"), debugSunDirectionCheck_, makePill(QStringLiteral("Real"), QStringLiteral("ok")));
    addRow(grid, 8, QStringLiteral("Normals / tangents"), disabledPendingControl(new QCheckBox(QStringLiteral("Mostrar"))), makePill(QStringLiteral("Pendiente"), QStringLiteral("pending")));
    addRow(grid, 9, QStringLiteral("GPU profiler"), disabledPendingControl(new QCheckBox(QStringLiteral("Mostrar"))), makePill(QStringLiteral("Pendiente"), QStringLiteral("pending")));
    auto* snapshot = new QPushButton(QStringLiteral("Exportar snapshot JSON"));
    addRow(grid, 10, QStringLiteral("Snapshot"), disabledPendingControl(snapshot), makePill(QStringLiteral("Pendiente"), QStringLiteral("pending")));
    textureForceMaxLodZeroCheck_ = new QCheckBox(QStringLiteral("Forzar maxLod = 0"));
    addRow(grid, 11, QStringLiteral("Texture maxLod0"), textureForceMaxLodZeroCheck_, makePill(QStringLiteral("Debug"), QStringLiteral("ok")));
    textureAnisotropyOverrideCombo_ = new QComboBox;
    textureAnisotropyOverrideCombo_->addItem(QStringLiteral("Automático"), static_cast<int>(renderer::RenderTextureDebugAnisotropyOverride::Automatic));
    textureAnisotropyOverrideCombo_->addItem(QStringLiteral("Forzar off"), static_cast<int>(renderer::RenderTextureDebugAnisotropyOverride::ForceOff));
    textureAnisotropyOverrideCombo_->addItem(QStringLiteral("Forzar on"), static_cast<int>(renderer::RenderTextureDebugAnisotropyOverride::ForceOn));
    addRow(grid, 12, QStringLiteral("Anisotropía debug"), textureAnisotropyOverrideCombo_, makePill(QStringLiteral("Debug"), QStringLiteral("ok")));
    textureOverrideMipBiasCheck_ = new QCheckBox(QStringLiteral("Usar mip bias debug"));
    addRow(grid, 13, QStringLiteral("Mip bias override"), textureOverrideMipBiasCheck_, makePill(QStringLiteral("Debug"), QStringLiteral("ok")));
    auto* textureMipBias = sliderWithValue(textureDebugMipBiasSlider_, textureDebugMipBiasValue_, -100, 100);
    addRow(grid, 14, QStringLiteral("Mip bias debug"), textureMipBias, makePill(QStringLiteral("Debug"), QStringLiteral("ok")));
    addCard(page, card);

    const auto manualChange = [this]() {
        if (!syncingControls_) {
            markCustomPreset();
        }
    };
    connect(debugWireframeCheck_, &QCheckBox::toggled, this, manualChange);
    connect(debugBoundsCheck_, &QCheckBox::toggled, this, manualChange);
    connect(debugLodColorsCheck_, &QCheckBox::toggled, this, manualChange);
    connect(debugShadowCastersCheck_, &QCheckBox::toggled, this, manualChange);
    connect(debugSourceObjectsCheck_, &QCheckBox::toggled, this, manualChange);
    connect(debugSunDirectionCheck_, &QCheckBox::toggled, this, manualChange);
    connect(textureForceMaxLodZeroCheck_, &QCheckBox::toggled, this, manualChange);
    connect(textureAnisotropyOverrideCombo_, &QComboBox::currentIndexChanged, this, manualChange);
    connect(textureOverrideMipBiasCheck_, &QCheckBox::toggled, this, manualChange);
    connect(textureDebugMipBiasSlider_, &QSlider::valueChanged, this, [this, manualChange](int value) {
        if (textureDebugMipBiasValue_ != nullptr) {
            textureDebugMipBiasValue_->setText(QStringLiteral("%1").arg(static_cast<double>(value) / 100.0, 0, 'f', 2));
        }
        manualChange();
    });
    return page;
}

void SettingsDialog::captureSettingsFromControls()
{
    settings_.graphics.preset = presetCombo_ == nullptr
        ? settings_.graphics.preset
        : static_cast<QualityPreset>(comboData(presetCombo_, static_cast<int>(settings_.graphics.preset)));
    if (vsyncCheck_ != nullptr) {
        settings_.graphics.vsync = vsyncCheck_->isChecked();
    }
    if (fpsCombo_ != nullptr) {
        const auto fps = comboData(fpsCombo_, settings_.graphics.fpsLimit);
        settings_.graphics.fpsLimit = fps == kCustomFpsValue && fpsCustomSpin_ != nullptr ? fpsCustomSpin_->value() : fps;
    }
    if (textureQualityCombo_ != nullptr) {
        settings_.texture.quality = static_cast<renderer::RenderTextureQuality>(
            comboData(textureQualityCombo_, static_cast<int>(settings_.texture.quality)));
    }
    if (anisotropyCombo_ != nullptr && anisotropyCombo_->isEnabled()) {
        settings_.texture.anisotropyLevel = comboData(anisotropyCombo_, settings_.texture.anisotropyLevel);
    }
    if (mipBiasSlider_ != nullptr) {
        settings_.texture.mipLodBias = static_cast<float>(mipBiasSlider_->value()) / 100.0F;
    }
    if (shadowModeCombo_ != nullptr) {
        settings_.shadow.updateMode = static_cast<renderer::RenderShadowUpdateMode>(
            comboData(shadowModeCombo_, static_cast<int>(settings_.shadow.updateMode)));
        settings_.shadow.quality = settings_.shadow.updateMode == renderer::RenderShadowUpdateMode::Off
            ? ShadowQuality::Off
            : (settings_.shadow.quality == ShadowQuality::Off ? ShadowQuality::High : settings_.shadow.quality);
    }
    if (performanceModeCombo_ != nullptr) {
        settings_.performance.mode = static_cast<PerformanceMode>(
            comboData(performanceModeCombo_, static_cast<int>(settings_.performance.mode)));
    }
    if (terrainQualityCombo_ != nullptr) {
        settings_.terrain.quality = static_cast<TerrainQuality>(
            comboData(terrainQualityCombo_, static_cast<int>(settings_.terrain.quality)));
    }
    if (lodQualityCombo_ != nullptr) {
        settings_.lod.quality = static_cast<LodQuality>(
            comboData(lodQualityCombo_, static_cast<int>(settings_.lod.quality)));
    }
    if (hlodCheck_ != nullptr) {
        settings_.lod.hlodEnabled = hlodCheck_->isChecked();
    }
    if (hlodAggressivenessCombo_ != nullptr) {
        settings_.lod.aggressiveness = static_cast<HlodAggressiveness>(
            comboData(hlodAggressivenessCombo_, static_cast<int>(settings_.lod.aggressiveness)));
    }
    if (lodDistanceSlider_ != nullptr) {
        settings_.lod.lodDistance = static_cast<float>(lodDistanceSlider_->value());
    }
    if (screenErrorSlider_ != nullptr) {
        settings_.lod.screenError = static_cast<float>(screenErrorSlider_->value()) / 100.0F;
    }
    if (hysteresisSlider_ != nullptr) {
        settings_.lod.hysteresis = static_cast<float>(hysteresisSlider_->value()) / 100.0F;
    }
    if (chunkBudgetSpin_ != nullptr) {
        settings_.lod.chunkBudget = chunkBudgetSpin_->value();
    }
    if (drawPacketBudgetSpin_ != nullptr) {
        settings_.lod.drawPacketBudget = drawPacketBudgetSpin_->value();
    }
    if (shadowCasterBudgetSpin_ != nullptr) {
        settings_.lod.shadowCasterBudget = shadowCasterBudgetSpin_->value();
    }
    if (terrainNearHighQualityCheck_ != nullptr) {
        settings_.lod.terrainNearHighQualityEnabled = terrainNearHighQualityCheck_->isChecked();
    }
    if (terrainNearHighQualityRadiusSpin_ != nullptr) {
        settings_.lod.terrainNearHighQualityRadius = static_cast<float>(terrainNearHighQualityRadiusSpin_->value());
    }
    if (debugDisableTerrainHlodCheck_ != nullptr) {
        settings_.lod.debugDisableTerrainHlod = debugDisableTerrainHlodCheck_->isChecked();
    }
    if (debugDisableTerrainChunkLodCheck_ != nullptr) {
        settings_.lod.debugDisableTerrainChunkLod = debugDisableTerrainChunkLodCheck_->isChecked();
    }
    if (lodDebugColorsCheck_ != nullptr) {
        settings_.lod.debugColors = lodDebugColorsCheck_->isChecked();
        settings_.debug.lodColors = settings_.lod.debugColors;
    }
    if (hlodOverrideCombo_ != nullptr) {
        settings_.lod.debugOverride = static_cast<ViewportHlodDebugOverride>(
            comboData(hlodOverrideCombo_, static_cast<int>(settings_.lod.debugOverride)));
    }
    if (debugWireframeCheck_ != nullptr) {
        settings_.debug.wireframe = debugWireframeCheck_->isChecked();
    }
    if (debugBoundsCheck_ != nullptr) {
        settings_.debug.boundsXray = debugBoundsCheck_->isChecked();
    }
    if (debugLodColorsCheck_ != nullptr) {
        settings_.debug.lodColors = debugLodColorsCheck_->isChecked();
        settings_.lod.debugColors = settings_.debug.lodColors;
    }
    if (debugShadowCastersCheck_ != nullptr) {
        settings_.debug.shadowCasters = debugShadowCastersCheck_->isChecked();
    }
    if (debugSourceObjectsCheck_ != nullptr) {
        settings_.debug.sourceObjects = debugSourceObjectsCheck_->isChecked();
    }
    if (debugSunDirectionCheck_ != nullptr) {
        settings_.debug.sunDirection = debugSunDirectionCheck_->isChecked();
    }
    if (textureForceMaxLodZeroCheck_ != nullptr) {
        settings_.debug.textureForceMaxLodZero = textureForceMaxLodZeroCheck_->isChecked();
    }
    if (textureAnisotropyOverrideCombo_ != nullptr) {
        settings_.debug.textureAnisotropyOverride = static_cast<renderer::RenderTextureDebugAnisotropyOverride>(
            comboData(textureAnisotropyOverrideCombo_, static_cast<int>(settings_.debug.textureAnisotropyOverride)));
    }
    if (textureOverrideMipBiasCheck_ != nullptr) {
        settings_.debug.textureOverrideMipLodBias = textureOverrideMipBiasCheck_->isChecked();
    }
    if (textureDebugMipBiasSlider_ != nullptr) {
        settings_.debug.textureDebugMipLodBias = static_cast<float>(textureDebugMipBiasSlider_->value()) / 100.0F;
    }
}

void SettingsDialog::refreshControls()
{
    syncingControls_ = true;

    if (presetCombo_ != nullptr) {
        setComboData(presetCombo_, static_cast<int>(settings_.graphics.preset));
    }
    if (vsyncCheck_ != nullptr) {
        vsyncCheck_->setChecked(settings_.graphics.vsync);
    }
    if (fpsCombo_ != nullptr) {
        const auto knownIndex = fpsCombo_->findData(settings_.graphics.fpsLimit);
        fpsCombo_->setCurrentIndex(knownIndex >= 0 ? knownIndex : fpsCombo_->findData(kCustomFpsValue));
    }
    if (fpsCustomSpin_ != nullptr) {
        fpsCustomSpin_->setValue(std::clamp(settings_.graphics.fpsLimit <= 0 ? 60 : settings_.graphics.fpsLimit, 15, 1000));
        fpsCustomSpin_->setEnabled(fpsCombo_ != nullptr && comboData(fpsCombo_, 0) == kCustomFpsValue);
    }
    if (textureQualityCombo_ != nullptr) {
        setComboData(textureQualityCombo_, static_cast<int>(settings_.texture.quality));
    }
    if (anisotropyCombo_ != nullptr) {
        setComboData(anisotropyCombo_, settings_.texture.anisotropyLevel);
    }
    if (mipBiasSlider_ != nullptr) {
        mipBiasSlider_->setValue(static_cast<int>(std::clamp(settings_.texture.mipLodBias, -1.0F, 1.0F) * 100.0F));
    }
    if (mipBiasValue_ != nullptr) {
        mipBiasValue_->setText(QStringLiteral("%1").arg(settings_.texture.mipLodBias, 0, 'f', 2));
    }
    if (shadowModeCombo_ != nullptr) {
        setComboData(shadowModeCombo_, static_cast<int>(settings_.shadow.updateMode));
    }
    if (performanceModeCombo_ != nullptr) {
        setComboData(performanceModeCombo_, static_cast<int>(settings_.performance.mode));
    }
    if (terrainQualityCombo_ != nullptr) {
        setComboData(terrainQualityCombo_, static_cast<int>(settings_.terrain.quality));
    }
    if (lodQualityCombo_ != nullptr) {
        setComboData(lodQualityCombo_, static_cast<int>(settings_.lod.quality));
    }
    if (hlodCheck_ != nullptr) {
        hlodCheck_->setChecked(settings_.lod.hlodEnabled);
    }
    if (hlodAggressivenessCombo_ != nullptr) {
        setComboData(hlodAggressivenessCombo_, static_cast<int>(settings_.lod.aggressiveness));
    }
    if (lodDistanceSlider_ != nullptr) {
        lodDistanceSlider_->setValue(std::clamp(static_cast<int>(settings_.lod.lodDistance), 0, 100));
    }
    if (lodDistanceValue_ != nullptr) {
        lodDistanceValue_->setText(QStringLiteral("%1 m").arg(lodDistanceSlider_ == nullptr ? 0 : lodDistanceSlider_->value()));
    }
    if (screenErrorSlider_ != nullptr) {
        screenErrorSlider_->setValue(std::clamp(static_cast<int>(settings_.lod.screenError * 100.0F), 5, 400));
    }
    if (screenErrorValue_ != nullptr) {
        screenErrorValue_->setText(QStringLiteral("%1").arg(settings_.lod.screenError, 0, 'f', 2));
    }
    if (hysteresisSlider_ != nullptr) {
        hysteresisSlider_->setValue(std::clamp(static_cast<int>(settings_.lod.hysteresis * 100.0F), 0, 45));
    }
    if (hysteresisValue_ != nullptr) {
        hysteresisValue_->setText(QStringLiteral("%1").arg(settings_.lod.hysteresis, 0, 'f', 2));
    }
    if (chunkBudgetSpin_ != nullptr) {
        chunkBudgetSpin_->setValue(settings_.lod.chunkBudget);
    }
    if (drawPacketBudgetSpin_ != nullptr) {
        drawPacketBudgetSpin_->setValue(settings_.lod.drawPacketBudget);
    }
    if (shadowCasterBudgetSpin_ != nullptr) {
        shadowCasterBudgetSpin_->setValue(settings_.lod.shadowCasterBudget);
    }
    if (terrainNearHighQualityCheck_ != nullptr) {
        terrainNearHighQualityCheck_->setChecked(settings_.lod.terrainNearHighQualityEnabled);
    }
    if (terrainNearHighQualityRadiusSpin_ != nullptr) {
        terrainNearHighQualityRadiusSpin_->setValue(std::clamp(
            static_cast<int>(settings_.lod.terrainNearHighQualityRadius),
            0,
            1000000));
    }
    if (debugDisableTerrainHlodCheck_ != nullptr) {
        debugDisableTerrainHlodCheck_->setChecked(settings_.lod.debugDisableTerrainHlod);
    }
    if (debugDisableTerrainChunkLodCheck_ != nullptr) {
        debugDisableTerrainChunkLodCheck_->setChecked(settings_.lod.debugDisableTerrainChunkLod);
    }
    if (lodDebugColorsCheck_ != nullptr) {
        lodDebugColorsCheck_->setChecked(settings_.lod.debugColors);
    }
    if (hlodOverrideCombo_ != nullptr) {
        setComboData(hlodOverrideCombo_, static_cast<int>(settings_.lod.debugOverride));
    }
    if (debugWireframeCheck_ != nullptr) {
        debugWireframeCheck_->setChecked(settings_.debug.wireframe);
    }
    if (debugBoundsCheck_ != nullptr) {
        debugBoundsCheck_->setChecked(settings_.debug.boundsXray);
    }
    if (debugLodColorsCheck_ != nullptr) {
        debugLodColorsCheck_->setChecked(settings_.debug.lodColors);
    }
    if (debugShadowCastersCheck_ != nullptr) {
        debugShadowCastersCheck_->setChecked(settings_.debug.shadowCasters);
    }
    if (debugSourceObjectsCheck_ != nullptr) {
        debugSourceObjectsCheck_->setChecked(settings_.debug.sourceObjects);
    }
    if (debugSunDirectionCheck_ != nullptr) {
        debugSunDirectionCheck_->setChecked(settings_.debug.sunDirection);
    }
    if (textureForceMaxLodZeroCheck_ != nullptr) {
        textureForceMaxLodZeroCheck_->setChecked(settings_.debug.textureForceMaxLodZero);
    }
    if (textureAnisotropyOverrideCombo_ != nullptr) {
        setComboData(textureAnisotropyOverrideCombo_, static_cast<int>(settings_.debug.textureAnisotropyOverride));
    }
    if (textureOverrideMipBiasCheck_ != nullptr) {
        textureOverrideMipBiasCheck_->setChecked(settings_.debug.textureOverrideMipLodBias);
    }
    if (textureDebugMipBiasSlider_ != nullptr) {
        textureDebugMipBiasSlider_->setValue(static_cast<int>(std::clamp(
            settings_.debug.textureDebugMipLodBias,
            -1.0F,
            1.0F) * 100.0F));
    }
    if (textureDebugMipBiasValue_ != nullptr) {
        textureDebugMipBiasValue_->setText(QStringLiteral("%1").arg(settings_.debug.textureDebugMipLodBias, 0, 'f', 2));
    }

    syncingControls_ = false;
}

void SettingsDialog::markCustomPreset()
{
    if (syncingControls_) {
        return;
    }
    captureSettingsFromControls();
    settings_.graphics.preset = QualityPreset::Custom;
    if (presetCombo_ != nullptr) {
        const QSignalBlocker blocker(presetCombo_);
        setComboData(presetCombo_, static_cast<int>(QualityPreset::Custom));
    }
    if (fpsCustomSpin_ != nullptr && fpsCombo_ != nullptr) {
        fpsCustomSpin_->setEnabled(comboData(fpsCombo_, 0) == kCustomFpsValue);
    }
}

void SettingsDialog::emitApply()
{
    captureSettingsFromControls();
    emit settingsApplied(settings_);
    if (footerStatus_ != nullptr) {
        footerStatus_->setText(QStringLiteral("Ajustes aplicados. La política de texturas recrea renderer/samplers si cambia."));
    }
}

} // namespace projectunity::editor
