#include <projectunity/editor/MainWindow.hpp>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <array>
#include <utility>

namespace projectunity::editor {
namespace {

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

[[nodiscard]] QDoubleSpinBox* makeLightingRangeSpinBox(double minimum, double maximum, double step)
{
    auto* spinBox = new QDoubleSpinBox;
    spinBox->setDecimals(3);
    spinBox->setRange(minimum, maximum);
    spinBox->setSingleStep(step);
    spinBox->setMinimumWidth(72);
    return spinBox;
}

} // namespace

QWidget* MainWindow::createLightingPanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 8, 8, 8);

    auto* header = new QLabel(QStringLiteral("Lighting / Bake"));
    header->setObjectName(QStringLiteral("PanelHeader"));
    layout->addWidget(header);

    auto* form = new QFormLayout;
    const auto makeRgbRow = [](QDoubleSpinBox*& r, QDoubleSpinBox*& g, QDoubleSpinBox*& b, const std::array<float, 3>& values) {
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
    sunAzimuth_ = makeLightingRangeSpinBox(-180.0, 180.0, 1.0);
    sunAzimuth_->setValue(editorSunAzimuthDegrees_);
    form->addRow(QStringLiteral("Sun Azimuth"), sunAzimuth_);
    sunElevation_ = makeLightingRangeSpinBox(-89.0, 89.0, 1.0);
    sunElevation_->setValue(editorSunElevationDegrees_);
    form->addRow(QStringLiteral("Sun Elevation"), sunElevation_);
    form->addRow(
        QStringLiteral("Sun RGB"),
        makeRgbRow(sunColorR_, sunColorG_, sunColorB_, editorSunLight_.color));
    sunIntensity_ = makeLightingSpinBox(32.0);
    sunIntensity_->setValue(editorSunLight_.intensity);
    form->addRow(QStringLiteral("Sun Intensity"), sunIntensity_);
    shadowModeCombo_ = new QComboBox;
    shadowModeCombo_->addItem(QStringLiteral("Live"), static_cast<int>(renderer::RenderShadowUpdateMode::Live));
    shadowModeCombo_->addItem(QStringLiteral("Frozen"), static_cast<int>(renderer::RenderShadowUpdateMode::Frozen));
    shadowModeCombo_->addItem(QStringLiteral("Off"), static_cast<int>(renderer::RenderShadowUpdateMode::Off));
    form->addRow(QStringLiteral("Dynamic Shadows"), shadowModeCombo_);
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

    const std::array<QDoubleSpinBox*, 13> spinBoxes {
        skyColorR_, skyColorG_, skyColorB_,
        groundColorR_, groundColorG_, groundColorB_,
        environmentIntensity_,
        sunAzimuth_, sunElevation_,
        sunColorR_, sunColorG_, sunColorB_,
        sunIntensity_,
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
    editorSunAzimuthDegrees_ = settings.value(
        QStringLiteral("editor/lighting/sunAzimuth"),
        editorSunAzimuthDegrees_).toFloat();
    editorSunElevationDegrees_ = settings.value(
        QStringLiteral("editor/lighting/sunElevation"),
        editorSunElevationDegrees_).toFloat();
    editorSunLight_.color = {
        settings.value(QStringLiteral("editor/lighting/sunR"), editorSunLight_.color[0]).toFloat(),
        settings.value(QStringLiteral("editor/lighting/sunG"), editorSunLight_.color[1]).toFloat(),
        settings.value(QStringLiteral("editor/lighting/sunB"), editorSunLight_.color[2]).toFloat(),
    };
    editorSunLight_.intensity = settings.value(
        QStringLiteral("editor/lighting/sunIntensity"),
        editorSunLight_.intensity).toFloat();
    const auto shadowModeValue = settings.value(
        QStringLiteral("editor/lighting/dynamicShadowMode"),
        static_cast<int>(renderer::RenderShadowUpdateMode::Off)).toInt();
    shadowUpdateMode_ = shadowModeValue >= static_cast<int>(renderer::RenderShadowUpdateMode::Live)
            && shadowModeValue <= static_cast<int>(renderer::RenderShadowUpdateMode::Off)
        ? static_cast<renderer::RenderShadowUpdateMode>(shadowModeValue)
        : renderer::RenderShadowUpdateMode::Off;
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
    settings.setValue(QStringLiteral("editor/lighting/sunAzimuth"), editorSunAzimuthDegrees_);
    settings.setValue(QStringLiteral("editor/lighting/sunElevation"), editorSunElevationDegrees_);
    settings.setValue(QStringLiteral("editor/lighting/sunR"), editorSunLight_.color[0]);
    settings.setValue(QStringLiteral("editor/lighting/sunG"), editorSunLight_.color[1]);
    settings.setValue(QStringLiteral("editor/lighting/sunB"), editorSunLight_.color[2]);
    settings.setValue(QStringLiteral("editor/lighting/sunIntensity"), editorSunLight_.intensity);
    settings.setValue(QStringLiteral("editor/lighting/dynamicShadowMode"), static_cast<int>(shadowUpdateMode_));
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
    setValue(sunAzimuth_, editorSunAzimuthDegrees_);
    setValue(sunElevation_, editorSunElevationDegrees_);
    setValue(sunColorR_, editorSunLight_.color[0]);
    setValue(sunColorG_, editorSunLight_.color[1]);
    setValue(sunColorB_, editorSunLight_.color[2]);
    setValue(sunIntensity_, editorSunLight_.intensity);
    updateEditorSunFromControls();
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
    editorSunLight_ = renderer::RenderLight {};
    editorSunAzimuthDegrees_ = 38.0F;
    editorSunElevationDegrees_ = 55.0F;
    shadowUpdateMode_ = renderer::RenderShadowUpdateMode::Off;
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

} // namespace projectunity::editor
