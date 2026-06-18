#pragma once

#include <projectunity/editor/EditorQualitySettings.hpp>

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;

namespace projectunity::renderer {
struct RendererStats;
} // namespace projectunity::renderer

namespace projectunity::editor {

class SettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(
        EditorQualitySettings settings,
        const renderer::RendererStats* rendererStats,
        QWidget* parent = nullptr);

    [[nodiscard]] const EditorQualitySettings& settings() const noexcept;

signals:
    void settingsApplied(const EditorQualitySettings& settings);

private:
    [[nodiscard]] QWidget* createGraphicsPage();
    [[nodiscard]] QWidget* createPerformancePage();
    [[nodiscard]] QWidget* createTexturesPage();
    [[nodiscard]] QWidget* createShadowsPage();
    [[nodiscard]] QWidget* createTerrainPage();
    [[nodiscard]] QWidget* createLodPage();
    [[nodiscard]] QWidget* createEditorPage();
    [[nodiscard]] QWidget* createDebugPage();

    void captureSettingsFromControls();
    void refreshControls();
    void markCustomPreset();
    void emitApply();

    EditorQualitySettings settings_;
    renderer::RendererStats rendererStats_;
    bool hasRendererStats_ {false};
    bool syncingControls_ {false};

    QListWidget* sidebar_ {nullptr};
    QStackedWidget* pages_ {nullptr};
    QLabel* footerStatus_ {nullptr};

    QComboBox* presetCombo_ {nullptr};
    QCheckBox* vsyncCheck_ {nullptr};
    QComboBox* fpsCombo_ {nullptr};
    QSpinBox* fpsCustomSpin_ {nullptr};

    QComboBox* textureQualityCombo_ {nullptr};
    QComboBox* anisotropyCombo_ {nullptr};
    QSlider* mipBiasSlider_ {nullptr};
    QLabel* mipBiasValue_ {nullptr};

    QComboBox* shadowModeCombo_ {nullptr};
    QComboBox* lodQualityCombo_ {nullptr};
    QCheckBox* hlodCheck_ {nullptr};
    QComboBox* hlodAggressivenessCombo_ {nullptr};
    QSlider* lodDistanceSlider_ {nullptr};
    QLabel* lodDistanceValue_ {nullptr};
    QSlider* screenErrorSlider_ {nullptr};
    QLabel* screenErrorValue_ {nullptr};
    QSlider* hysteresisSlider_ {nullptr};
    QLabel* hysteresisValue_ {nullptr};
    QSpinBox* chunkBudgetSpin_ {nullptr};
    QSpinBox* drawPacketBudgetSpin_ {nullptr};
    QSpinBox* shadowCasterBudgetSpin_ {nullptr};
    QCheckBox* terrainNearHighQualityCheck_ {nullptr};
    QSpinBox* terrainNearHighQualityRadiusSpin_ {nullptr};
    QCheckBox* debugDisableTerrainHlodCheck_ {nullptr};
    QCheckBox* debugDisableTerrainChunkLodCheck_ {nullptr};
    QCheckBox* lodDebugColorsCheck_ {nullptr};
    QComboBox* hlodOverrideCombo_ {nullptr};

    QComboBox* terrainQualityCombo_ {nullptr};
    QComboBox* performanceModeCombo_ {nullptr};

    QCheckBox* debugWireframeCheck_ {nullptr};
    QCheckBox* debugBoundsCheck_ {nullptr};
    QCheckBox* debugLodColorsCheck_ {nullptr};
    QCheckBox* debugShadowCastersCheck_ {nullptr};
    QCheckBox* debugSourceObjectsCheck_ {nullptr};
    QCheckBox* debugSunDirectionCheck_ {nullptr};
    QCheckBox* textureForceMaxLodZeroCheck_ {nullptr};
    QComboBox* textureAnisotropyOverrideCombo_ {nullptr};
    QCheckBox* textureOverrideMipBiasCheck_ {nullptr};
    QSlider* textureDebugMipBiasSlider_ {nullptr};
    QLabel* textureDebugMipBiasValue_ {nullptr};
};

} // namespace projectunity::editor
