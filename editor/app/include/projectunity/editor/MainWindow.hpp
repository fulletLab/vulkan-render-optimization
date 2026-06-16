#pragma once

#include <projectunity/assets/AssetManager.hpp>
#include <projectunity/core/Log.hpp>
#include <projectunity/renderer/RendererTypes.hpp>
#include <projectunity/scene/Scene.hpp>
#include <projectunity/scripting/ScriptRuntime.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include <QMainWindow>
#include <QByteArray>
#include <QString>
#include <QStringList>

class QAction;
class QActionGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
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
class ProjectBrowserWidget;
struct ViewportTerrainBrushEvent;

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
    projectunity::scene::EntityId createPrimitiveEntity(const QString& name, assets::ModelAsset model);
    projectunity::scene::EntityId createCubeEntity();
    projectunity::scene::EntityId createSphereEntity();
    projectunity::scene::EntityId createPlaneEntity();
    projectunity::scene::EntityId createCameraEntity();
    projectunity::scene::EntityId createLightEntity(projectunity::scene::LightComponentType type);
    projectunity::scene::EntityId createTerrainEntity();
    projectunity::scene::EntityId createPlayerEntity();
    void deleteSelectedEntity();
    void duplicateSelectedEntity();
    void selectEntity(projectunity::scene::EntityId id);
    void clearSelection();
    void ensureBuiltInGeneratedModels();
    void rebuildGeneratedSceneAssets();
    void startPlayMode();
    void stopPlayMode();
    [[nodiscard]] projectunity::scene::EntityId findPlayableCameraEntity() const;
    [[nodiscard]] bool cookPlayRuntimeScene(projectunity::scene::EntityId sourceCameraEntityId);
    void showPlayRuntimeWindow();
    void attachFlyPlayerControllerToSelection();
    void removeScriptFromSelection();
    void ensureFlyPlayerScriptAsset();
    [[nodiscard]] std::filesystem::path projectScriptsModulePath() const;
    [[nodiscard]] bool loadProjectScriptsModule(bool showUserMessage);
    [[nodiscard]] bool buildProjectScriptsModule();
    [[nodiscard]] bool reloadProjectScriptsModule();
    void showAddComponentMenu(QWidget* anchor);
    void addMeshRendererToSelection();
    void addCameraToSelection();
    void addLightToSelection(projectunity::scene::LightComponentType type);
    void addTerrainToSelection();
    void addRigidbodyToSelection();
    void addColliderToSelection(projectunity::scene::ColliderShape shape);
    void removeMeshRendererFromSelection();
    void removeCameraFromSelection();
    void removeLightFromSelection();
    void removeTerrainFromSelection();
    void removeRigidbodyFromSelection();
    void removeColliderFromSelection();
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
    [[nodiscard]] QWidget* createTerrainPanel();
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
    void syncTerrainPanelFromSelection();
    void regenerateSelectedTerrain();
    void clearSelectedTerrain();
    void saveSelectedTerrainAsset();
    void addTerrainLayer();
    void removeSelectedTerrainLayer();
    [[nodiscard]] std::optional<math::Vec3> handleTerrainBrushEvent(
        const ViewportTerrainBrushEvent& event);
    [[nodiscard]] bool uploadTerrainMesh(
        scene::EntityId id,
        scene::TerrainComponent& component,
        const terrain::TerrainGenerationResult& generated,
        bool logDetails);

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
    QComboBox* scriptComponentCombo_ {nullptr};
    QComboBox* scriptAssetCombo_ {nullptr};
    QCheckBox* scriptEnabledCheck_ {nullptr};
    QTableWidget* scriptFieldsTable_ {nullptr};
    QLabel* scriptStatusLabel_ {nullptr};
    QPushButton* removeScriptButton_ {nullptr};
    QCheckBox* rigidbodyEnabledCheck_ {nullptr};
    QComboBox* rigidbodyBodyTypeCombo_ {nullptr};
    QDoubleSpinBox* rigidbodyMass_ {nullptr};
    QCheckBox* rigidbodyGravityCheck_ {nullptr};
    QDoubleSpinBox* rigidbodyLinearDrag_ {nullptr};
    QDoubleSpinBox* rigidbodyAngularDrag_ {nullptr};
    QCheckBox* colliderEnabledCheck_ {nullptr};
    QComboBox* colliderShapeCombo_ {nullptr};
    QDoubleSpinBox* colliderSizeX_ {nullptr};
    QDoubleSpinBox* colliderSizeY_ {nullptr};
    QDoubleSpinBox* colliderSizeZ_ {nullptr};
    QDoubleSpinBox* colliderOffsetX_ {nullptr};
    QDoubleSpinBox* colliderOffsetY_ {nullptr};
    QDoubleSpinBox* colliderOffsetZ_ {nullptr};
    QDoubleSpinBox* colliderRadius_ {nullptr};
    QDoubleSpinBox* colliderHeight_ {nullptr};
    QCheckBox* colliderTriggerCheck_ {nullptr};
    QPushButton* deleteEntityButton_ {nullptr};
    QPushButton* duplicateEntityButton_ {nullptr};
    QPushButton* addComponentButton_ {nullptr};
    QPlainTextEdit* consoleView_ {nullptr};
    ProjectBrowserWidget* projectBrowser_ {nullptr};
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
    QDoubleSpinBox* terrainWidth_ {nullptr};
    QDoubleSpinBox* terrainLength_ {nullptr};
    QDoubleSpinBox* terrainHeightScale_ {nullptr};
    QSpinBox* terrainResolution_ {nullptr};
    QSpinBox* terrainChunkSize_ {nullptr};
    QSpinBox* terrainSeed_ {nullptr};
    QComboBox* terrainNoiseType_ {nullptr};
    QDoubleSpinBox* terrainFrequency_ {nullptr};
    QSpinBox* terrainOctaves_ {nullptr};
    QDoubleSpinBox* terrainPersistence_ {nullptr};
    QDoubleSpinBox* terrainLacunarity_ {nullptr};
    QSpinBox* terrainLodLevels_ {nullptr};
    QCheckBox* terrainGenerateNormals_ {nullptr};
    QCheckBox* terrainGenerateTangents_ {nullptr};
    QCheckBox* terrainGenerateCollider_ {nullptr};
    QListWidget* terrainLayerList_ {nullptr};
    QCheckBox* terrainBrushEnabled_ {nullptr};
    QComboBox* terrainBrushMode_ {nullptr};
    QDoubleSpinBox* terrainBrushSize_ {nullptr};
    QDoubleSpinBox* terrainBrushStrength_ {nullptr};
    QDoubleSpinBox* terrainBrushFalloff_ {nullptr};
    QDoubleSpinBox* terrainFlattenHeight_ {nullptr};
    QLabel* terrainStatus_ {nullptr};
    QPushButton* terrainRegenerateButton_ {nullptr};
    QPushButton* terrainClearButton_ {nullptr};
    std::optional<math::Vec3> terrainLastBrushPoint_;
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
    scripting::ScriptRegistry scriptRegistry_;
    scripting::ScriptModuleLoader scriptModuleLoader_;
    scripting::ScriptRuntime scriptRuntime_;
    renderer::RenderEnvironmentSettings environmentSettings_;
    renderer::RenderLight editorSunLight_;
    float editorSunAzimuthDegrees_ {38.0F};
    float editorSunElevationDegrees_ {55.0F};
    renderer::RenderShadowUpdateMode shadowUpdateMode_ {renderer::RenderShadowUpdateMode::Off};
    std::shared_ptr<const assets::TextureAsset> environmentTexture_;
    assets::AssetId environmentTextureId_;
    scene::EntityId selectedEntityId_;
    scene::ScriptInstanceId inspectedScriptInstanceId_;
    scene::EntityId playRuntimeCameraEntityId_;
    std::filesystem::path currentScenePath_;
    QByteArray defaultDockState_;
    int activeAssetImports_ {0};
    bool inspectorUpdating_ {false};
    bool playModeActive_ {false};
};

} // namespace projectunity::editor
