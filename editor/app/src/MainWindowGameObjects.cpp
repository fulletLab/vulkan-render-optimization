#include <projectunity/editor/MainWindow.hpp>

#include <projectunity/assets/GeneratedModelBuilder.hpp>
#include <projectunity/terrain/TerrainGenerator.hpp>

#include <QCursor>
#include <QMenu>
#include <QMessageBox>
#include <QPoint>
#include <QStatusBar>
#include <QWidget>

#include <filesystem>
#include <sstream>
#include <vector>

namespace projectunity::editor {
namespace {

[[nodiscard]] scene::TransformComponent cameraTransform()
{
    scene::TransformComponent transform;
    transform.position = {0.0F, 2.0F, -8.0F};
    return transform;
}

[[nodiscard]] scene::LightComponent namedLight(scene::LightComponentType type)
{
    scene::LightComponent light;
    light.type = type;
    if (type == scene::LightComponentType::Point) {
        light.range = 10.0F;
        light.intensity = 4.0F;
    } else if (type == scene::LightComponentType::Spot) {
        light.range = 12.0F;
        light.innerConeAngle = 0.35F;
        light.outerConeAngle = 0.8F;
    }
    return light;
}

[[nodiscard]] const char* lightName(scene::LightComponentType type) noexcept
{
    switch (type) {
    case scene::LightComponentType::Directional:
        return "Directional Light";
    case scene::LightComponentType::Point:
        return "Point Light";
    case scene::LightComponentType::Spot:
        return "Spot Light";
    }
    return "Light";
}

[[nodiscard]] scene::Entity* selected(scene::Scene& scene, scene::EntityId id)
{
    return id.isValid() ? scene.findEntity(id) : nullptr;
}

} // namespace

scene::EntityId MainWindow::createPrimitiveEntity(const QString& name, assets::ModelAsset model)
{
    const auto record = assetManager_.registerGeneratedModel(std::move(model));
    auto& entity = scene_.createEntity(name.toStdString());
    scene::MeshRendererComponent meshRenderer;
    meshRenderer.modelAssetId = record.id;
    (void)scene_.setMeshRenderer(entity.id, meshRenderer);
    const auto id = entity.id;
    rebuildHierarchy();
    selectEntity(id);
    refreshViewports();
    statusBar()->showMessage(QStringLiteral("%1 created").arg(name));
    core::logInfo(core::LogCategory::Editor, QStringLiteral("GameObject created: %1").arg(name).toStdString());
    return id;
}

scene::EntityId MainWindow::createCubeEntity()
{
    return createPrimitiveEntity(QStringLiteral("Cube"), assets::makeCubeModel("Cube"));
}

scene::EntityId MainWindow::createSphereEntity()
{
    return createPrimitiveEntity(QStringLiteral("Sphere"), assets::makeSphereModel("Sphere"));
}

scene::EntityId MainWindow::createPlaneEntity()
{
    return createPrimitiveEntity(QStringLiteral("Plane"), assets::makePlaneModel("Plane"));
}

scene::EntityId MainWindow::createCameraEntity()
{
    auto& camera = scene_.createEntity("Camera");
    (void)scene_.setTransform(camera.id, cameraTransform());
    scene::CameraComponent component;
    component.direction = {0.0F, 0.0F, 1.0F};
    component.nearPlane = 0.05F;
    component.farPlane = 4000.0F;
    (void)scene_.setCamera(camera.id, component);
    const auto id = camera.id;
    rebuildHierarchy();
    selectEntity(id);
    refreshViewports();
    statusBar()->showMessage(QStringLiteral("Camera created"));
    return id;
}

scene::EntityId MainWindow::createLightEntity(scene::LightComponentType type)
{
    auto& light = scene_.createEntity(lightName(type));
    (void)scene_.setLight(light.id, namedLight(type));
    const auto id = light.id;
    rebuildHierarchy();
    selectEntity(id);
    refreshViewports();
    statusBar()->showMessage(QStringLiteral("%1 created").arg(QString::fromUtf8(lightName(type))));
    return id;
}

scene::EntityId MainWindow::createTerrainEntity()
{
    scene::TerrainComponent terrain;
    auto generated = terrain::TerrainGenerator::generate(terrain.settings);
    if (!generated.succeeded()) {
        core::logError(core::LogCategory::Editor, "Terrain generation failed: " + generated.error);
        statusBar()->showMessage(QStringLiteral("Terrain generation failed"));
        return {};
    }

    auto model = assets::makeTerrainModel(generated, terrain.materialLayers, &assetManager_, "Terrain");
    const auto record = assetManager_.registerGeneratedModel(std::move(model));
    terrain.generatedModelAssetId = record.id;
    terrain.heightmap = generated.heightmap;

    auto& entity = scene_.createEntity("Terrain");
    (void)scene_.setTerrain(entity.id, terrain);
    scene::MeshRendererComponent meshRenderer;
    meshRenderer.modelAssetId = record.id;
    (void)scene_.setMeshRenderer(entity.id, meshRenderer);
    if (terrain.settings.generateCollider) {
        scene::ColliderComponent collider;
        collider.shape = scene::ColliderShape::Terrain;
        collider.size = {terrain.settings.width, terrain.settings.heightScale, terrain.settings.length};
        (void)scene_.setCollider(entity.id, collider);
    }

    std::ostringstream message;
    message << "Terrain generated"
            << " width=" << terrain.settings.width
            << " length=" << terrain.settings.length
            << " resolution=" << terrain.settings.resolution
            << " chunks=" << generated.stats.chunkCount
            << " vertices=" << generated.stats.vertexCount
            << " indices=" << generated.stats.indexCount
            << " generationMs=" << generated.stats.generationMilliseconds
            << " uploadMs=deferred"
            << " materialLayers=" << terrain.materialLayers.size()
            << " collider=" << (terrain.settings.generateCollider ? "generated-partial" : "skipped");
    core::logInfo(core::LogCategory::Editor, message.str());

    const auto id = entity.id;
    rebuildHierarchy();
    selectEntity(id);
    syncTerrainPanelFromSelection();
    refreshViewports();
    statusBar()->showMessage(QStringLiteral("Terrain created"));
    return id;
}

void MainWindow::ensureBuiltInGeneratedModels()
{
    (void)assetManager_.registerGeneratedModel(assets::makeCubeModel("Cube"));
    (void)assetManager_.registerGeneratedModel(assets::makeSphereModel("Sphere"));
    (void)assetManager_.registerGeneratedModel(assets::makePlaneModel("Plane"));
}

void MainWindow::rebuildGeneratedSceneAssets()
{
    std::vector<scene::EntityId> terrainEntities;
    for (const auto& entity : scene_.entities()) {
        if (entity.terrain.has_value()) {
            terrainEntities.push_back(entity.id);
        }
    }
    for (const auto id : terrainEntities) {
        auto* entity = scene_.findEntity(id);
        if (entity == nullptr || !entity->terrain.has_value()) {
            continue;
        }
        auto terrainComponent = *entity->terrain;
        auto generated = terrainComponent.heightmap.empty()
            ? terrain::TerrainGenerator::generate(terrainComponent.settings)
            : terrain::TerrainGenerator::build(terrainComponent.settings, terrainComponent.heightmap);
        if (!generated.succeeded()) {
            core::logError(core::LogCategory::Editor, "Loaded terrain regeneration failed: " + generated.error);
            continue;
        }
        auto model = assets::makeTerrainModel(generated, terrainComponent.materialLayers, &assetManager_, entity->name);
        model.id = terrainComponent.generatedModelAssetId;
        const auto record = assetManager_.registerGeneratedModel(std::move(model));
        terrainComponent.generatedModelAssetId = record.id;
        terrainComponent.heightmap = generated.heightmap;
        (void)scene_.setTerrain(id, terrainComponent);
        scene::MeshRendererComponent meshRenderer;
        if (entity->meshRenderer.has_value()) {
            meshRenderer = *entity->meshRenderer;
        }
        meshRenderer.modelAssetId = record.id;
        (void)scene_.setMeshRenderer(id, meshRenderer);
    }
}

void MainWindow::showAddComponentMenu(QWidget* anchor)
{
    auto* entity = selected(scene_, selectedEntityId_);
    if (entity == nullptr) {
        return;
    }

    QMenu menu(anchor);
    auto* addMenu = menu.addMenu(QStringLiteral("Add Component"));
    addMenu->addAction(QStringLiteral("Mesh Renderer"), this, [this] { addMeshRendererToSelection(); });
    addMenu->addAction(QStringLiteral("Camera"), this, [this] { addCameraToSelection(); });
    auto* lightMenu = addMenu->addMenu(QStringLiteral("Light"));
    lightMenu->addAction(QStringLiteral("Directional Light"), this, [this] {
        addLightToSelection(scene::LightComponentType::Directional);
    });
    lightMenu->addAction(QStringLiteral("Point Light"), this, [this] {
        addLightToSelection(scene::LightComponentType::Point);
    });
    lightMenu->addAction(QStringLiteral("Spot Light"), this, [this] {
        addLightToSelection(scene::LightComponentType::Spot);
    });
    addMenu->addAction(QStringLiteral("Terrain"), this, [this] { addTerrainToSelection(); });
    addMenu->addAction(QStringLiteral("Rigidbody (PARCIAL)"), this, [this] { addRigidbodyToSelection(); });
    auto* colliderMenu = addMenu->addMenu(QStringLiteral("Collider (PARCIAL)"));
    colliderMenu->addAction(QStringLiteral("Box Collider"), this, [this] { addColliderToSelection(scene::ColliderShape::Box); });
    colliderMenu->addAction(QStringLiteral("Sphere Collider"), this, [this] { addColliderToSelection(scene::ColliderShape::Sphere); });
    colliderMenu->addAction(QStringLiteral("Mesh Collider"), this, [this] { addColliderToSelection(scene::ColliderShape::Mesh); });
    auto* scriptMenu = addMenu->addMenu(QStringLiteral("Script"));
    ensureFlyPlayerScriptAsset();
    const auto scriptsDir = std::filesystem::path(PROJECTUNITY_SOURCE_DIR) / "Project" / "Assets" / "Scripts";
    std::error_code errorCode;
    if (std::filesystem::exists(scriptsDir, errorCode)) {
        for (const auto& entry : std::filesystem::directory_iterator(scriptsDir, errorCode)) {
            if (!entry.is_regular_file(errorCode) || entry.path().extension() != ".cpp") {
                continue;
            }
            const auto scriptName = entry.path().stem().string();
            const auto scriptAsset = "Assets/Scripts/" + scriptName + ".cpp";
            scriptMenu->addAction(QString::fromStdString(scriptName), this, [this, scriptAsset] {
                auto* selectedEntity = selected(scene_, selectedEntityId_);
                if (selectedEntity == nullptr) {
                    return;
                }
                std::string error;
                auto script = scriptRegistry_.createComponentFromAsset(scriptAsset, &error);
                if (!script.has_value()) {
                    QMessageBox::warning(this, QStringLiteral("Add Script"), QString::fromStdString(error));
                    statusBar()->showMessage(QString::fromStdString(error));
                    return;
                }
                const auto instanceId = scene_.addScript(selectedEntityId_, std::move(*script));
                if (!instanceId.has_value()) {
                    return;
                }
                inspectedScriptInstanceId_ = *instanceId;
                updateInspector();
                refreshViewports();
                statusBar()->showMessage(QStringLiteral("Script attached"));
            });
        }
    }
    if (scriptMenu->actions().isEmpty()) {
        scriptMenu->addAction(QStringLiteral("FlyPlayerController"), this, [this] {
            attachFlyPlayerControllerToSelection();
        });
    }

    auto* removeMenu = menu.addMenu(QStringLiteral("Remove Component"));
    removeMenu->addAction(QStringLiteral("Mesh Renderer"), this, [this] { removeMeshRendererFromSelection(); });
    removeMenu->addAction(QStringLiteral("Camera"), this, [this] { removeCameraFromSelection(); });
    removeMenu->addAction(QStringLiteral("Light"), this, [this] { removeLightFromSelection(); });
    removeMenu->addAction(QStringLiteral("Terrain"), this, [this] { removeTerrainFromSelection(); });
    removeMenu->addAction(QStringLiteral("Rigidbody"), this, [this] { removeRigidbodyFromSelection(); });
    removeMenu->addAction(QStringLiteral("Collider"), this, [this] { removeColliderFromSelection(); });
    removeMenu->addAction(QStringLiteral("Script"), this, [this] { removeScriptFromSelection(); });

    const auto point = anchor == nullptr ? QCursor::pos() : anchor->mapToGlobal(QPoint(0, anchor->height()));
    menu.exec(point);
}

void MainWindow::addMeshRendererToSelection()
{
    auto* entity = selected(scene_, selectedEntityId_);
    if (entity == nullptr) {
        return;
    }
    const auto record = assetManager_.registerGeneratedModel(assets::makeCubeModel("Cube"));
    scene::MeshRendererComponent meshRenderer;
    if (entity->meshRenderer.has_value()) {
        meshRenderer = *entity->meshRenderer;
    }
    meshRenderer.modelAssetId = record.id;
    (void)scene_.setMeshRenderer(selectedEntityId_, meshRenderer);
    updateInspector();
    refreshViewports();
}

void MainWindow::addCameraToSelection()
{
    if (selected(scene_, selectedEntityId_) == nullptr) {
        return;
    }
    (void)scene_.setCamera(selectedEntityId_, scene::CameraComponent {});
    updateInspector();
    refreshViewports();
}

void MainWindow::addLightToSelection(scene::LightComponentType type)
{
    if (selected(scene_, selectedEntityId_) == nullptr) {
        return;
    }
    (void)scene_.setLight(selectedEntityId_, namedLight(type));
    updateInspector();
    refreshViewports();
}

void MainWindow::addTerrainToSelection()
{
    auto* entity = selected(scene_, selectedEntityId_);
    if (entity == nullptr) {
        return;
    }
    scene::TerrainComponent terrain;
    (void)scene_.setTerrain(selectedEntityId_, terrain);
    regenerateSelectedTerrain();
}

void MainWindow::addRigidbodyToSelection()
{
    if (selected(scene_, selectedEntityId_) == nullptr) {
        return;
    }
    (void)scene_.setRigidbody(selectedEntityId_, scene::RigidbodyComponent {});
    core::logWarning(core::LogCategory::Physics, "Rigidbody component is PARCIAL: serialized but not connected to a physics simulation yet");
    updateInspector();
}

void MainWindow::addColliderToSelection(scene::ColliderShape shape)
{
    if (selected(scene_, selectedEntityId_) == nullptr) {
        return;
    }
    scene::ColliderComponent collider;
    collider.shape = shape;
    (void)scene_.setCollider(selectedEntityId_, collider);
    core::logWarning(core::LogCategory::Physics, "Collider component is PARCIAL: serialized but not connected to physics queries yet");
    updateInspector();
}

void MainWindow::removeMeshRendererFromSelection()
{
    (void)scene_.setMeshRenderer(selectedEntityId_, std::nullopt);
    updateInspector();
    refreshViewports();
}

void MainWindow::removeCameraFromSelection()
{
    (void)scene_.setCamera(selectedEntityId_, std::nullopt);
    updateInspector();
    refreshViewports();
}

void MainWindow::removeLightFromSelection()
{
    (void)scene_.setLight(selectedEntityId_, std::nullopt);
    updateInspector();
    refreshViewports();
}

void MainWindow::removeTerrainFromSelection()
{
    (void)scene_.setTerrain(selectedEntityId_, std::nullopt);
    updateInspector();
    syncTerrainPanelFromSelection();
}

void MainWindow::removeRigidbodyFromSelection()
{
    (void)scene_.setRigidbody(selectedEntityId_, std::nullopt);
    updateInspector();
}

void MainWindow::removeColliderFromSelection()
{
    (void)scene_.setCollider(selectedEntityId_, std::nullopt);
    updateInspector();
}

} // namespace projectunity::editor
