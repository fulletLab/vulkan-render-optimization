#include <projectunity/editor/MainWindow.hpp>

#include <projectunity/assets/GeneratedModelBuilder.hpp>
#include <projectunity/assets/TerrainAsset.hpp>
#include <projectunity/editor/ViewportWidget.hpp>
#include <projectunity/terrain/TerrainGenerator.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QtConcurrentRun>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <utility>
#include <vector>

namespace projectunity::editor {
namespace {

[[nodiscard]] QDoubleSpinBox* makeDoubleBox(double minimum, double maximum, double value, double step)
{
    auto* box = new QDoubleSpinBox;
    box->setRange(minimum, maximum);
    box->setValue(value);
    box->setSingleStep(step);
    box->setDecimals(4);
    return box;
}

[[nodiscard]] QSpinBox* makeIntBox(int minimum, int maximum, int value)
{
    auto* box = new QSpinBox;
    box->setRange(minimum, maximum);
    box->setValue(value);
    return box;
}

[[nodiscard]] scene::TerrainComponent* selectedTerrain(scene::Scene& scene, scene::EntityId id)
{
    auto* entity = id.isValid() ? scene.findEntity(id) : nullptr;
    return entity == nullptr || !entity->terrain.has_value() ? nullptr : &*entity->terrain;
}

void addSectionTitle(QVBoxLayout& layout, const QString& title)
{
    auto* label = new QLabel(title);
    label->setObjectName(QStringLiteral("SectionTitle"));
    layout.addWidget(label);
}

[[nodiscard]] bool nearZero(float value) noexcept
{
    return std::fabs(value) <= 0.0001F;
}

[[nodiscard]] terrain::TerrainBrushMode brushMode(int index) noexcept
{
    switch (index) {
    case 1:
        return terrain::TerrainBrushMode::Lower;
    case 2:
        return terrain::TerrainBrushMode::Smooth;
    case 3:
        return terrain::TerrainBrushMode::Flatten;
    default:
        return terrain::TerrainBrushMode::Raise;
    }
}

} // namespace

QWidget* MainWindow::createTerrainPanel()
{
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    addSectionTitle(*layout, QStringLiteral("Terrain Generator"));
    auto* settingsGroup = new QGroupBox(QStringLiteral("Settings"));
    auto* settingsForm = new QFormLayout(settingsGroup);
    terrainWidth_ = makeDoubleBox(1.0, 10000.0, 128.0, 1.0);
    terrainLength_ = makeDoubleBox(1.0, 10000.0, 128.0, 1.0);
    terrainHeightScale_ = makeDoubleBox(0.0, 2000.0, 24.0, 1.0);
    terrainResolution_ = makeIntBox(2, 4097, 129);
    terrainChunkSize_ = makeIntBox(1, 512, 32);
    terrainSeed_ = makeIntBox(0, 2147483647, 1337);
    terrainNoiseType_ = new QComboBox;
    terrainNoiseType_->addItems({QStringLiteral("Value"), QStringLiteral("Ridged")});
    terrainFrequency_ = makeDoubleBox(0.0001, 10.0, 0.0125, 0.001);
    terrainOctaves_ = makeIntBox(1, 12, 5);
    terrainPersistence_ = makeDoubleBox(0.0, 1.0, 0.5, 0.05);
    terrainLacunarity_ = makeDoubleBox(1.0, 8.0, 2.0, 0.1);
    terrainLodLevels_ = makeIntBox(1, 8, 3);
    terrainGenerateNormals_ = new QCheckBox(QStringLiteral("Generate normals"));
    terrainGenerateNormals_->setChecked(true);
    terrainGenerateTangents_ = new QCheckBox(QStringLiteral("Generate tangents"));
    terrainGenerateTangents_->setChecked(true);
    terrainGenerateCollider_ = new QCheckBox(QStringLiteral("Generate terrain collider"));
    settingsForm->addRow(QStringLiteral("Width"), terrainWidth_);
    settingsForm->addRow(QStringLiteral("Length"), terrainLength_);
    settingsForm->addRow(QStringLiteral("Height Scale"), terrainHeightScale_);
    settingsForm->addRow(QStringLiteral("Resolution"), terrainResolution_);
    settingsForm->addRow(QStringLiteral("Chunk Size"), terrainChunkSize_);
    settingsForm->addRow(QStringLiteral("Seed"), terrainSeed_);
    settingsForm->addRow(QStringLiteral("Noise Type"), terrainNoiseType_);
    settingsForm->addRow(QStringLiteral("Frequency"), terrainFrequency_);
    settingsForm->addRow(QStringLiteral("Octaves"), terrainOctaves_);
    settingsForm->addRow(QStringLiteral("Persistence"), terrainPersistence_);
    settingsForm->addRow(QStringLiteral("Lacunarity"), terrainLacunarity_);
    settingsForm->addRow(QStringLiteral("LOD Levels"), terrainLodLevels_);
    settingsForm->addRow(QString(), terrainGenerateNormals_);
    settingsForm->addRow(QString(), terrainGenerateTangents_);
    settingsForm->addRow(QString(), terrainGenerateCollider_);
    layout->addWidget(settingsGroup);

    auto* brushGroup = new QGroupBox(QStringLiteral("Terrain Edit Mode"));
    auto* brushForm = new QFormLayout(brushGroup);
    terrainBrushEnabled_ = new QCheckBox(QStringLiteral("Edit Terrain in Scene View"));
    terrainBrushMode_ = new QComboBox;
    terrainBrushMode_->addItems({
        QStringLiteral("Sculpt Raise"),
        QStringLiteral("Sculpt Lower"),
        QStringLiteral("Smooth"),
        QStringLiteral("Flatten"),
    });
    terrainBrushSize_ = makeDoubleBox(0.1, 500.0, 6.0, 0.5);
    terrainBrushStrength_ = makeDoubleBox(0.01, 50.0, 1.0, 0.1);
    terrainBrushFalloff_ = makeDoubleBox(0.0, 1.0, 0.5, 0.05);
    terrainFlattenHeight_ = makeDoubleBox(-2000.0, 2000.0, 0.0, 0.5);
    auto* paintButton = new QPushButton(QStringLiteral("Paint Texture (coming next)"));
    paintButton->setEnabled(false);
    paintButton->setToolTip(QStringLiteral("Texture splat-map painting is not implemented in this phase."));
    brushForm->addRow(QString(), terrainBrushEnabled_);
    brushForm->addRow(QStringLiteral("Tool"), terrainBrushMode_);
    brushForm->addRow(QStringLiteral("Brush Size"), terrainBrushSize_);
    brushForm->addRow(QStringLiteral("Brush Strength"), terrainBrushStrength_);
    brushForm->addRow(QStringLiteral("Brush Falloff"), terrainBrushFalloff_);
    brushForm->addRow(QStringLiteral("Target Height"), terrainFlattenHeight_);
    brushForm->addRow(QString(), paintButton);
    layout->addWidget(brushGroup);

    auto* layersGroup = new QGroupBox(QStringLiteral("Material Layers"));
    auto* layersLayout = new QVBoxLayout(layersGroup);
    terrainLayerList_ = new QListWidget;
    terrainLayerList_->setMinimumHeight(130);
    layersLayout->addWidget(terrainLayerList_);
    auto* layerButtons = new QHBoxLayout;
    auto* addLayerButton = new QPushButton(QStringLiteral("Add Layer"));
    auto* removeLayerButton = new QPushButton(QStringLiteral("Remove Selected"));
    layerButtons->addWidget(addLayerButton);
    layerButtons->addWidget(removeLayerButton);
    layersLayout->addLayout(layerButtons);
    layout->addWidget(layersGroup);

    auto* buttons = new QHBoxLayout;
    terrainRegenerateButton_ = new QPushButton(QStringLiteral("Generate / Regenerate"));
    terrainClearButton_ = new QPushButton(QStringLiteral("Clear / Flat"));
    auto* saveAssetButton = new QPushButton(QStringLiteral("Save Terrain Asset"));
    buttons->addWidget(terrainRegenerateButton_);
    buttons->addWidget(terrainClearButton_);
    buttons->addWidget(saveAssetButton);
    layout->addLayout(buttons);
    terrainStatus_ = new QLabel(QStringLiteral("Select a Terrain GameObject."));
    terrainStatus_->setWordWrap(true);
    layout->addWidget(terrainStatus_);
    layout->addStretch();

    connect(terrainRegenerateButton_, &QPushButton::clicked, this, [this] { regenerateSelectedTerrain(); });
    connect(terrainClearButton_, &QPushButton::clicked, this, [this] { clearSelectedTerrain(); });
    connect(saveAssetButton, &QPushButton::clicked, this, [this] { saveSelectedTerrainAsset(); });
    connect(addLayerButton, &QPushButton::clicked, this, [this] { addTerrainLayer(); });
    connect(removeLayerButton, &QPushButton::clicked, this, [this] { removeSelectedTerrainLayer(); });
    connect(terrainBrushEnabled_, &QCheckBox::toggled, this, [this](bool enabled) {
        terrainLastBrushPoint_.reset();
        if (sceneViewport_ != nullptr) {
            sceneViewport_->setTerrainBrushEnabled(enabled && selectedTerrain(scene_, selectedEntityId_) != nullptr);
        }
    });
    connect(terrainBrushSize_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (sceneViewport_ != nullptr) {
            sceneViewport_->setTerrainBrushRadius(static_cast<float>(value));
        }
    });
    if (sceneViewport_ != nullptr) {
        sceneViewport_->setTerrainBrushCallback([this](const ViewportTerrainBrushEvent& event) {
            return handleTerrainBrushEvent(event);
        });
        sceneViewport_->setTerrainBrushRadius(static_cast<float>(terrainBrushSize_->value()));
    }

    scroll->setWidget(content);
    syncTerrainPanelFromSelection();
    return scroll;
}

void MainWindow::syncTerrainPanelFromSelection()
{
    auto* component = selectedTerrain(scene_, selectedEntityId_);
    const bool enabled = component != nullptr;
    const std::vector<QWidget*> widgets {
        terrainWidth_, terrainLength_, terrainHeightScale_, terrainResolution_, terrainChunkSize_,
        terrainSeed_, terrainNoiseType_, terrainFrequency_, terrainOctaves_, terrainPersistence_,
        terrainLacunarity_, terrainLodLevels_, terrainGenerateNormals_, terrainGenerateTangents_,
        terrainGenerateCollider_, terrainLayerList_, terrainRegenerateButton_, terrainClearButton_,
        terrainBrushEnabled_, terrainBrushMode_, terrainBrushSize_, terrainBrushStrength_,
        terrainBrushFalloff_, terrainFlattenHeight_,
    };
    for (auto* widget : widgets) {
        if (widget != nullptr) {
            widget->setEnabled(enabled);
        }
    }
    if (sceneViewport_ != nullptr) {
        const bool brushEnabled = enabled && terrainBrushEnabled_ != nullptr && terrainBrushEnabled_->isChecked();
        sceneViewport_->setTerrainBrushEnabled(brushEnabled);
    }
    if (!enabled) {
        terrainLastBrushPoint_.reset();
        if (terrainLayerList_ != nullptr) {
            terrainLayerList_->clear();
        }
        if (terrainStatus_ != nullptr) {
            terrainStatus_->setText(QStringLiteral(
                "Select a Terrain GameObject or create one from GameObject > 3D Object > Terrain."));
        }
        return;
    }

    const auto& settings = component->settings;
    terrainWidth_->setValue(settings.width);
    terrainLength_->setValue(settings.length);
    terrainHeightScale_->setValue(settings.heightScale);
    terrainResolution_->setValue(static_cast<int>(settings.resolution));
    terrainChunkSize_->setValue(static_cast<int>(settings.chunkSize));
    terrainSeed_->setValue(static_cast<int>(settings.seed));
    terrainNoiseType_->setCurrentIndex(settings.noiseType == terrain::TerrainNoiseType::Ridged ? 1 : 0);
    terrainFrequency_->setValue(settings.frequency);
    terrainOctaves_->setValue(static_cast<int>(settings.octaves));
    terrainPersistence_->setValue(settings.persistence);
    terrainLacunarity_->setValue(settings.lacunarity);
    terrainLodLevels_->setValue(static_cast<int>(settings.lodLevels));
    terrainGenerateNormals_->setChecked(settings.generateNormals);
    terrainGenerateTangents_->setChecked(settings.generateTangents);
    terrainGenerateCollider_->setChecked(settings.generateCollider);
    terrainLayerList_->clear();
    for (const auto& layer : component->materialLayers) {
        terrainLayerList_->addItem(QStringLiteral("%1 | height %2-%3 | slope %4-%5 | tiling %6")
            .arg(QString::fromStdString(layer.name))
            .arg(layer.heightRange[0], 0, 'f', 2)
            .arg(layer.heightRange[1], 0, 'f', 2)
            .arg(layer.slopeRange[0], 0, 'f', 2)
            .arg(layer.slopeRange[1], 0, 'f', 2)
            .arg(layer.tiling, 0, 'f', 2));
    }
    if (terrainStatus_ != nullptr) {
        terrainStatus_->setText(QStringLiteral(
            "Raise/Lower/Smooth/Flatten are active in Scene View. Hold Shift to lower temporarily. "
            "Texture painting and collider runtime remain PARCIAL."));
    }
}

bool MainWindow::uploadTerrainMesh(
    scene::EntityId id,
    scene::TerrainComponent& component,
    const terrain::TerrainGenerationResult& generated,
    bool logDetails)
{
    auto* entity = scene_.findEntity(id);
    if (entity == nullptr || !generated.succeeded()) {
        return false;
    }
    QElapsedTimer uploadTimer;
    uploadTimer.start();
    auto model = assets::makeTerrainModel(generated, component.materialLayers, &assetManager_, entity->name);
    const auto record = assetManager_.registerGeneratedModel(std::move(model));
    component.generatedModelAssetId = record.id;
    component.heightmap = generated.heightmap;
    (void)scene_.setTerrain(id, component);
    scene::MeshRendererComponent meshRenderer = entity->meshRenderer.value_or(scene::MeshRendererComponent {});
    meshRenderer.modelAssetId = record.id;
    (void)scene_.setMeshRenderer(id, meshRenderer);
    if (component.settings.generateCollider) {
        scene::ColliderComponent collider;
        collider.shape = scene::ColliderShape::Terrain;
        collider.size = {component.settings.width, component.settings.heightScale, component.settings.length};
        (void)scene_.setCollider(id, collider);
    }
    const auto uploadQueueMilliseconds = uploadTimer.nsecsElapsed() / 1000000.0;
    if (sceneViewport_ != nullptr) {
        sceneViewport_->setAssetManager(&assetManager_);
    }
    if (gameViewport_ != nullptr) {
        gameViewport_->setAssetManager(&assetManager_);
    }
    refreshViewports();

    if (logDetails) {
        std::ostringstream message;
        message << "Terrain generated"
                << " width=" << component.settings.width
                << " length=" << component.settings.length
                << " resolution=" << component.settings.resolution
                << " heightScale=" << component.settings.heightScale
                << " seed=" << component.settings.seed
                << " chunkSize=" << component.settings.chunkSize
                << " chunksCreated=" << generated.stats.chunkCount
                << " vertexCount=" << generated.stats.vertexCount
                << " indexCount=" << generated.stats.indexCount
                << " averageNormalY=" << generated.stats.averageNormalY
                << " winding=CCW"
                << " cullMode=Back"
                << " uvOrientation=U+X,V+Z"
                << " materialLayers=" << component.materialLayers.size()
                << " generationMs=" << generated.stats.generationMilliseconds
                << " uploadQueueMs=" << uploadQueueMilliseconds
                << " uploadMs=deferred-to-renderer";
        core::logInfo(core::LogCategory::Editor, message.str());
    }
    return true;
}

void MainWindow::regenerateSelectedTerrain()
{
    auto* entity = selectedEntityId_.isValid() ? scene_.findEntity(selectedEntityId_) : nullptr;
    if (entity == nullptr || !entity->terrain.has_value()) {
        statusBar()->showMessage(QStringLiteral("Select a Terrain GameObject"));
        return;
    }
    auto component = *entity->terrain;
    component.settings.width = static_cast<float>(terrainWidth_->value());
    component.settings.length = static_cast<float>(terrainLength_->value());
    component.settings.heightScale = static_cast<float>(terrainHeightScale_->value());
    component.settings.resolution = static_cast<std::uint32_t>(terrainResolution_->value());
    component.settings.chunkSize = static_cast<std::uint32_t>(terrainChunkSize_->value());
    component.settings.seed = static_cast<std::uint32_t>(terrainSeed_->value());
    component.settings.noiseType = terrainNoiseType_->currentIndex() == 1
        ? terrain::TerrainNoiseType::Ridged
        : terrain::TerrainNoiseType::Value;
    component.settings.frequency = static_cast<float>(terrainFrequency_->value());
    component.settings.octaves = static_cast<std::uint32_t>(terrainOctaves_->value());
    component.settings.persistence = static_cast<float>(terrainPersistence_->value());
    component.settings.lacunarity = static_cast<float>(terrainLacunarity_->value());
    component.settings.lodLevels = static_cast<std::uint32_t>(terrainLodLevels_->value());
    component.settings.generateNormals = terrainGenerateNormals_->isChecked();
    component.settings.generateTangents = terrainGenerateTangents_->isChecked();
    component.settings.generateCollider = terrainGenerateCollider_->isChecked();
    if (component.materialLayers.empty()) {
        component.materialLayers.push_back({});
    }

    const auto id = selectedEntityId_;
    terrainRegenerateButton_->setEnabled(false);
    terrainStatus_->setText(QStringLiteral("Generating terrain in background..."));
    auto* watcher = new QFutureWatcher<terrain::TerrainGenerationResult>(this);
    connect(watcher, &QFutureWatcher<terrain::TerrainGenerationResult>::finished, this,
        [this, watcher, id, component = std::move(component)]() mutable {
            auto generated = watcher->result();
            watcher->deleteLater();
            terrainRegenerateButton_->setEnabled(selectedTerrain(scene_, selectedEntityId_) != nullptr);
            if (!generated.succeeded() || !uploadTerrainMesh(id, component, generated, true)) {
                core::logError(core::LogCategory::Editor, "Terrain generation failed: " + generated.error);
                statusBar()->showMessage(QStringLiteral("Terrain generation failed"));
                syncTerrainPanelFromSelection();
                return;
            }
            statusBar()->showMessage(QStringLiteral("Terrain regenerated"));
            updateInspector();
            syncTerrainPanelFromSelection();
        });
    const auto settings = component.settings;
    watcher->setFuture(QtConcurrent::run([settings] { return terrain::TerrainGenerator::generate(settings); }));
}

void MainWindow::clearSelectedTerrain()
{
    auto* component = selectedTerrain(scene_, selectedEntityId_);
    if (component == nullptr) {
        return;
    }
    auto next = *component;
    next.heightmap.assign(
        static_cast<std::size_t>(next.settings.resolution) * next.settings.resolution,
        0.0F);
    const auto generated = terrain::TerrainGenerator::build(next.settings, next.heightmap);
    if (uploadTerrainMesh(selectedEntityId_, next, generated, true)) {
        statusBar()->showMessage(QStringLiteral("Terrain cleared to a flat heightmap"));
        updateInspector();
    }
}

std::optional<math::Vec3> MainWindow::handleTerrainBrushEvent(const ViewportTerrainBrushEvent& event)
{
    auto* entity = selectedEntityId_.isValid() ? scene_.findEntity(selectedEntityId_) : nullptr;
    if (entity == nullptr || !entity->terrain.has_value()) {
        return std::nullopt;
    }
    const auto& transform = entity->transform;
    if (!nearZero(transform.rotationEuler.x) || !nearZero(transform.rotationEuler.y)
        || !nearZero(transform.rotationEuler.z) || transform.scale.x <= 0.0F
        || transform.scale.y <= 0.0F || transform.scale.z <= 0.0F) {
        terrainStatus_->setText(QStringLiteral(
            "Terrain brush requires zero rotation and positive scale. Apply size changes in Terrain Settings."));
        return std::nullopt;
    }
    auto component = *entity->terrain;
    if (component.heightmap.empty()) {
        component.heightmap = terrain::TerrainGenerator::generate(component.settings).heightmap;
    }
    const math::Vec3 localOrigin {
        (event.ray.origin.x - transform.position.x) / transform.scale.x,
        (event.ray.origin.y - transform.position.y) / transform.scale.y,
        (event.ray.origin.z - transform.position.z) / transform.scale.z,
    };
    const math::Vec3 localDirection {
        event.ray.direction.x / transform.scale.x,
        event.ray.direction.y / transform.scale.y,
        event.ray.direction.z / transform.scale.z,
    };
    const auto localHit = terrain::TerrainGenerator::raycast(
        component.heightmap, component.settings, localOrigin, localDirection);
    if (!localHit.has_value()) {
        return std::nullopt;
    }
    const math::Vec3 worldHit {
        transform.position.x + localHit->x * transform.scale.x,
        transform.position.y + localHit->y * transform.scale.y,
        transform.position.z + localHit->z * transform.scale.z,
    };
    if (sceneViewport_ != nullptr && terrainBrushSize_ != nullptr) {
        const auto worldScale = (transform.scale.x + transform.scale.z) * 0.5F;
        sceneViewport_->setTerrainBrushRadius(static_cast<float>(terrainBrushSize_->value()) * worldScale);
    }
    if (event.phase == ViewportTerrainBrushPhase::End) {
        terrainLastBrushPoint_.reset();
        return worldHit;
    }
    if (event.phase == ViewportTerrainBrushPhase::Hover) {
        return worldHit;
    }

    terrain::TerrainBrushSettings brush;
    brush.radius = static_cast<float>(terrainBrushSize_->value());
    brush.strength = static_cast<float>(terrainBrushStrength_->value());
    brush.falloff = static_cast<float>(terrainBrushFalloff_->value());
    brush.targetHeight = static_cast<float>(terrainFlattenHeight_->value());
    const auto minimumDabDistance = std::max(
        std::min(component.settings.width, component.settings.length)
            / static_cast<float>(component.settings.resolution - 1U) * 0.5F,
        brush.radius * 0.08F);
    if (event.phase == ViewportTerrainBrushPhase::Drag && terrainLastBrushPoint_.has_value()
        && math::distanceSquared(*terrainLastBrushPoint_, *localHit)
            < minimumDabDistance * minimumDabDistance) {
        return worldHit;
    }
    auto mode = brushMode(terrainBrushMode_->currentIndex());
    if (event.lowerModifier) {
        mode = terrain::TerrainBrushMode::Lower;
    }
    if (!terrain::TerrainGenerator::applyBrush(component.heightmap, component.settings, mode, *localHit, brush)) {
        return worldHit;
    }
    const auto generated = terrain::TerrainGenerator::build(component.settings, component.heightmap);
    if (uploadTerrainMesh(selectedEntityId_, component, generated, false)) {
        terrainLastBrushPoint_ = *localHit;
        terrainStatus_->setText(QStringLiteral("Terrain sculpted; normals recalculated and mesh queued for upload."));
    }
    return worldHit;
}

void MainWindow::saveSelectedTerrainAsset()
{
    const auto* component = selectedTerrain(scene_, selectedEntityId_);
    if (component == nullptr) {
        statusBar()->showMessage(QStringLiteral("Select a Terrain GameObject"));
        return;
    }
    const auto path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Save Terrain Asset"),
        QString(),
        QStringLiteral("ProjectUnity Terrain (*.terrain.json);;JSON (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    assets::TerrainAssetData asset;
    asset.settings = component->settings;
    asset.materialLayers = component->materialLayers;
    asset.heightmap = component->heightmap;
    std::string error;
    if (!assets::saveTerrainAsset(std::filesystem::path(path.toStdWString()), asset, &error)) {
        core::logError(core::LogCategory::Editor, error);
        statusBar()->showMessage(QStringLiteral("Terrain asset save failed"));
        return;
    }
    statusBar()->showMessage(QStringLiteral("Terrain asset saved"));
}

void MainWindow::addTerrainLayer()
{
    auto* component = selectedTerrain(scene_, selectedEntityId_);
    if (component == nullptr || component->materialLayers.size() >= 8U) {
        return;
    }
    terrain::TerrainMaterialLayer layer;
    layer.name = "Layer " + std::to_string(component->materialLayers.size());
    component->materialLayers.push_back(std::move(layer));
    syncTerrainPanelFromSelection();
}

void MainWindow::removeSelectedTerrainLayer()
{
    auto* component = selectedTerrain(scene_, selectedEntityId_);
    if (component == nullptr || component->materialLayers.size() <= 1U || terrainLayerList_ == nullptr) {
        return;
    }
    const auto row = terrainLayerList_->currentRow();
    if (row < 0 || row >= static_cast<int>(component->materialLayers.size())) {
        return;
    }
    component->materialLayers.erase(component->materialLayers.begin() + row);
    syncTerrainPanelFromSelection();
}

} // namespace projectunity::editor
