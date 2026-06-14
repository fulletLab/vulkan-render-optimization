#pragma once

#include <projectunity/assets/AssetManager.hpp>
#include <projectunity/core/Log.hpp>
#include <projectunity/renderer/RendererTypes.hpp>
#include <projectunity/scene/Scene.hpp>

#include <filesystem>
#include <memory>
#include <vector>

#include <QMainWindow>
#include <QByteArray>
#include <QString>
#include <QStringList>

class QAction;
class QActionGroup;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QMenu;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

namespace ads {
class CDockAreaWidget;
class CDockManager;
class CDockWidget;
} // namespace ads

namespace projectunity::renderer {
class IRenderer;
} // namespace projectunity::renderer

namespace projectunity::editor {

class ViewportWidget;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    [[nodiscard]] bool runSmokeChecks(QString* errorMessage);
    [[nodiscard]] bool runPhase6VisualChecks(QString* errorMessage);
    [[nodiscard]] bool runPhase6CullingProfile(QString* errorMessage);

private:
    void createMenus();
    void createToolbar();
    void createDockLayout();
    void restoreEditorLayout();
    void resetEditorLayout();
    void saveEditorLayout();
    void appendPendingLogs();
    void importAsset();
    void importAssetAsync(const QString& path);
    [[nodiscard]] bool handleAssetImportResult(const assets::AssetImportResult& result, bool createModelEntity);
    [[nodiscard]] assets::AssetImportResult importAssetFromPath(const QString& path, bool createModelEntity);
    void rebuildAssetBrowser();
    void createImportedModelEntity(const assets::AssetRecord& record);
    void newScene();
    void openScene();
    bool saveScene();
    bool saveSceneAs();
    bool saveSceneToPath(const QString& path);
    bool loadSceneFromPath(const QString& path);
    projectunity::scene::EntityId createEmptyEntity(const QString& name);
    projectunity::scene::EntityId createPlayerEntity();
    void deleteSelectedEntity();
    void duplicateSelectedEntity();
    void selectEntity(projectunity::scene::EntityId id);
    void clearSelection();
    void startPlayMode();
    void stopPlayMode();
    [[nodiscard]] projectunity::scene::EntityId findPlayableCameraEntity() const;
    [[nodiscard]] bool buildPlayRuntimeSnapshot(projectunity::scene::EntityId sourceCameraEntityId);
    void showPlayRuntimeWindow();
    void attachFlyPlayerControllerToSelection();
    void removeScriptFromSelection();
    void ensureFlyPlayerScriptAsset();
    void rebuildHierarchy();
    void addEntityToHierarchy(QTreeWidgetItem* parentItem, projectunity::scene::EntityId id);
    void updateInspector();
    void applyInspectorToSelection();
    void refreshViewports();

    [[nodiscard]] ads::CDockWidget* createDockWidget(const QString& title, QWidget* content);
    [[nodiscard]] QWidget* createSceneViewPanel();
    [[nodiscard]] QWidget* createGameViewPanel();
    [[nodiscard]] QWidget* createHierarchyPanel();
    [[nodiscard]] QWidget* createInspectorPanel();
    [[nodiscard]] QWidget* createProjectPanel();
    [[nodiscard]] QWidget* createAssetImportPanel();
    [[nodiscard]] QWidget* createBottomPanel();
    [[nodiscard]] QWidget* createProfilerPanel();
    [[nodiscard]] QWidget* createLightingPanel();
    [[nodiscard]] QWidget* createTextPanel(const QString& title, const QStringList& lines) const;
    [[nodiscard]] QDoubleSpinBox* createTransformSpinBox();
    void updateProfilerPanel();
    void applyLightingSettings();
    void scheduleLightingSettingsApply();
    void pushLightingSettingsToViewports();
    void restoreLightingSettings();
    void saveLightingSettings();
    void updateLightingPanelControls();
    void updateAssetImportPanel(const QString& message, bool busy, int percent = -1);
    void useSelectedTextureAsEnvironment();
    void clearEnvironmentTexture();
    void resetLightingDefaults();
    void refreshEnvironmentTextureLabel();
    void updateEditorSunFromControls();

    ads::CDockManager* dockManager_ {nullptr};
    ads::CDockWidget* gameDock_ {nullptr};
    QMenu* windowMenu_ {nullptr};
    QTreeWidget* hierarchyTree_ {nullptr};
    QLineEdit* sceneNameEdit_ {nullptr};
    QLineEdit* entityNameEdit_ {nullptr};
    QDoubleSpinBox* positionX_ {nullptr};
    QDoubleSpinBox* positionY_ {nullptr};
    QDoubleSpinBox* positionZ_ {nullptr};
    QDoubleSpinBox* rotationX_ {nullptr};
    QDoubleSpinBox* rotationY_ {nullptr};
    QDoubleSpinBox* rotationZ_ {nullptr};
    QDoubleSpinBox* scaleX_ {nullptr};
    QDoubleSpinBox* scaleY_ {nullptr};
    QDoubleSpinBox* scaleZ_ {nullptr};
    QLabel* componentSummary_ {nullptr};
    QPushButton* deleteEntityButton_ {nullptr};
    QPushButton* duplicateEntityButton_ {nullptr};
    QPushButton* addComponentButton_ {nullptr};
    QPlainTextEdit* consoleView_ {nullptr};
    QTableWidget* assetTable_ {nullptr};
    QLabel* assetImportStatus_ {nullptr};
    QLabel* performanceStatus_ {nullptr};
    QProgressBar* assetImportProgress_ {nullptr};
    QTableWidget* profilerTable_ {nullptr};
    QComboBox* shadowModeCombo_ {nullptr};
    QDoubleSpinBox* skyColorR_ {nullptr};
    QDoubleSpinBox* skyColorG_ {nullptr};
    QDoubleSpinBox* skyColorB_ {nullptr};
    QDoubleSpinBox* groundColorR_ {nullptr};
    QDoubleSpinBox* groundColorG_ {nullptr};
    QDoubleSpinBox* groundColorB_ {nullptr};
    QDoubleSpinBox* environmentIntensity_ {nullptr};
    QDoubleSpinBox* sunAzimuth_ {nullptr};
    QDoubleSpinBox* sunElevation_ {nullptr};
    QDoubleSpinBox* sunColorR_ {nullptr};
    QDoubleSpinBox* sunColorG_ {nullptr};
    QDoubleSpinBox* sunColorB_ {nullptr};
    QDoubleSpinBox* sunIntensity_ {nullptr};
    QLineEdit* environmentTextureEdit_ {nullptr};
    QPushButton* useEnvironmentTextureButton_ {nullptr};
    QPushButton* clearEnvironmentTextureButton_ {nullptr};
    QPushButton* resetLightingButton_ {nullptr};
    ViewportWidget* sceneViewport_ {nullptr};
    ViewportWidget* gameViewport_ {nullptr};
    ViewportWidget* playRuntimeViewport_ {nullptr};
    std::shared_ptr<core::MemoryLogSink> logSink_;
    QTimer* logFlushTimer_ {nullptr};
    QTimer* lightingApplyTimer_ {nullptr};
    scene::Scene scene_;
    scene::Scene playRuntimeScene_;
    assets::AssetManager assetManager_;
    std::unique_ptr<renderer::IRenderer> renderer_;
    renderer::RenderEnvironmentSettings environmentSettings_;
    renderer::RenderLight editorSunLight_;
    float editorSunAzimuthDegrees_ {38.0F};
    float editorSunElevationDegrees_ {55.0F};
    renderer::RenderShadowUpdateMode shadowUpdateMode_ {renderer::RenderShadowUpdateMode::Off};
    std::shared_ptr<const assets::TextureAsset> environmentTexture_;
    assets::AssetId environmentTextureId_;
    scene::EntityId selectedEntityId_;
    scene::EntityId playRuntimeCameraEntityId_;
    std::filesystem::path currentScenePath_;
    QByteArray defaultDockState_;
    int activeAssetImports_ {0};
    bool inspectorUpdating_ {false};
    bool playModeActive_ {false};
};

} // namespace projectunity::editor
