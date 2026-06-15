#include <projectunity/editor/MainWindow.hpp>

#include <projectunity/editor/ProjectBrowserWidget.hpp>

#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLabel>
#include <QMetaObject>
#include <QPointer>
#include <QProgressBar>
#include <QStatusBar>
#include <QtConcurrentRun>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

namespace projectunity::editor {
namespace {

[[nodiscard]] std::filesystem::path pathFromQString(const QString& path)
{
    return std::filesystem::path(path.toStdWString());
}

[[nodiscard]] scene::LightComponentType sceneLightType(assets::ImportedLightType type)
{
    switch (type) {
    case assets::ImportedLightType::Directional:
        return scene::LightComponentType::Directional;
    case assets::ImportedLightType::Point:
        return scene::LightComponentType::Point;
    case assets::ImportedLightType::Spot:
        return scene::LightComponentType::Spot;
    }
    return scene::LightComponentType::Directional;
}

[[nodiscard]] float calibratedLightIntensity(const assets::ImportedLightAsset& light)
{
    const auto intensity = std::isfinite(light.intensity) ? std::max(light.intensity, 0.0F) : 0.0F;
    if (light.type == assets::ImportedLightType::Directional && intensity > 100.0F) {
        return std::clamp(intensity * 0.0003F, 0.0F, 8.0F);
    }
    return std::clamp(intensity, 0.0F, light.type == assets::ImportedLightType::Directional ? 8.0F : 256.0F);
}

[[nodiscard]] scene::LightComponent sceneLightComponent(const assets::ImportedLightAsset& imported)
{
    scene::LightComponent light;
    light.type = sceneLightType(imported.type);
    light.direction = imported.direction;
    light.color = imported.color;
    light.intensity = calibratedLightIntensity(imported);
    light.range = imported.range;
    light.innerConeAngle = imported.innerConeAngle;
    light.outerConeAngle = imported.outerConeAngle;
    return light;
}

[[nodiscard]] scene::CameraComponentProjection sceneCameraProjection(assets::ImportedCameraProjection projection)
{
    switch (projection) {
    case assets::ImportedCameraProjection::Perspective:
        return scene::CameraComponentProjection::Perspective;
    case assets::ImportedCameraProjection::Orthographic:
        return scene::CameraComponentProjection::Orthographic;
    }
    return scene::CameraComponentProjection::Perspective;
}

[[nodiscard]] scene::CameraComponent sceneCameraComponent(const assets::ImportedCameraAsset& imported)
{
    scene::CameraComponent camera;
    camera.projection = sceneCameraProjection(imported.projection);
    camera.direction = imported.direction;
    camera.right = imported.right;
    camera.up = imported.up;
    camera.verticalFovRadians = imported.verticalFovRadians;
    camera.aspectRatio = imported.aspectRatio;
    camera.xMagnitude = imported.xMagnitude;
    camera.yMagnitude = imported.yMagnitude;
    camera.nearPlane = imported.nearPlane;
    camera.farPlane = imported.farPlane;
    return camera;
}

} // namespace

void MainWindow::importAsset()
{
    const auto path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Import Asset"),
        QString(),
        QStringLiteral("ProjectUnity Assets (*.ffult);;Models (*.glb *.gltf);;Textures (*.png *.jpg *.jpeg *.ktx *.ktx2)"));
    if (!path.isEmpty()) {
        importAssetAsync(path);
    }
}

void MainWindow::importAssetAsync(const QString& path)
{
    auto* watcher = new QFutureWatcher<assets::AssetImportResult>(this);
    ++activeAssetImports_;
    const auto displayName = QFileInfo(path).fileName();
    updateAssetImportPanel(QStringLiteral("Importing %1").arg(displayName), true, 1);
    connect(watcher, &QFutureWatcher<assets::AssetImportResult>::finished, this, [this, watcher, displayName]() {
        const auto result = watcher->result();
        watcher->deleteLater();
        activeAssetImports_ = std::max(0, activeAssetImports_ - 1);
        (void)handleAssetImportResult(result, true);
        const auto message = result.success
            ? QStringLiteral("Imported %1").arg(displayName)
            : (result.error.empty()
                ? QStringLiteral("Failed %1").arg(displayName)
                : QStringLiteral("Failed %1: %2").arg(displayName, QString::fromStdString(result.error)));
        updateAssetImportPanel(message, activeAssetImports_ > 0, 100);
    });
    QPointer<MainWindow> window(this);
    watcher->setFuture(QtConcurrent::run([this, path, window, displayName]() {
        const assets::AssetImportProgressCallback progress = [window, displayName](const assets::AssetImportProgress& update) {
            if (window == nullptr) {
                return;
            }
            const auto stage = QString::fromStdString(update.stage);
            QMetaObject::invokeMethod(
                window,
                [window, displayName, stage, percent = update.percent]() {
                    if (window != nullptr) {
                        window->updateAssetImportPanel(
                            QStringLiteral("%1: %2").arg(displayName, stage),
                            true,
                            percent);
                    }
                },
                Qt::QueuedConnection);
        };
        return assetManager_.importAsset(pathFromQString(path), progress);
    }));
    statusBar()->showMessage(QStringLiteral("Importing asset"));
    core::logInfo(core::LogCategory::Assets, "Editor asset import queued");
}

void MainWindow::updateAssetImportPanel(const QString& message, bool busy, int percent)
{
    if (assetImportStatus_ != nullptr) {
        assetImportStatus_->setText(message);
    }
    if (assetImportProgress_ == nullptr) {
        return;
    }
    if (busy) {
        const auto value = std::clamp(percent < 0 ? 1 : percent, 1, 99);
        assetImportProgress_->setRange(0, 100);
        assetImportProgress_->setValue(value);
        assetImportProgress_->setFormat(QStringLiteral("%p%"));
    } else {
        assetImportProgress_->setRange(0, 100);
        assetImportProgress_->setValue(percent > 0 ? std::clamp(percent, 0, 100) : 0);
        assetImportProgress_->setFormat(message);
    }
}

bool MainWindow::handleAssetImportResult(const assets::AssetImportResult& result, bool createModelEntity)
{
    if (!result.success) {
        statusBar()->showMessage(QStringLiteral("Asset import failed"));
        core::logError(core::LogCategory::Assets, result.error);
        appendPendingLogs();
        return false;
    }
    rebuildAssetBrowser();
    if (projectBrowser_ != nullptr) {
        (void)projectBrowser_->selectAsset(result.record.id);
    }
    if (result.record.type == assets::AssetType::Texture2D && result.record.id == environmentTextureId_) {
        environmentTexture_ = assetManager_.texture(result.record.id);
        refreshEnvironmentTextureLabel();
        pushLightingSettingsToViewports();
    }
    if (createModelEntity && result.record.type == assets::AssetType::Model) {
        createImportedModelEntity(result.record);
    } else {
        refreshViewports();
    }
    statusBar()->showMessage(QStringLiteral("Asset imported"));
    core::logInfo(
        core::LogCategory::Assets,
        "Asset imported id=" + std::to_string(result.record.id.value())
            + " type=" + assets::toString(result.record.type)
            + " name=" + result.record.displayName);
    appendPendingLogs();
    return true;
}

assets::AssetImportResult MainWindow::importAssetFromPath(const QString& path, bool createModelEntity)
{
    const auto result = assetManager_.importAsset(pathFromQString(path));
    (void)handleAssetImportResult(result, createModelEntity);
    return result;
}

void MainWindow::rebuildAssetBrowser()
{
    ensureFlyPlayerScriptAsset();
    if (projectBrowser_ != nullptr) {
        projectBrowser_->setAssetManager(&assetManager_);
        projectBrowser_->setProjectRoot(std::filesystem::path(PROJECTUNITY_SOURCE_DIR) / "Project" / "Assets");
        projectBrowser_->rebuild();
    }
}

void MainWindow::createImportedModelEntity(const assets::AssetRecord& record)
{
    auto& entity = scene_.createEntity(record.displayName);
    const auto importedId = entity.id;
    bool attachedMeshRenderer = false;
    std::size_t hiddenInternalNodeCount = 0U;
    if (const auto model = assetManager_.model(record.id)) {
        if (scene_.setMeshRenderer(importedId, scene::MeshRendererComponent {record.id})) {
            attachedMeshRenderer = true;
        }
        hiddenInternalNodeCount = model->primitiveInstances.empty()
            ? model->primitives.size()
            : model->primitiveInstances.size();
        for (const auto& importedLight : model->lights) {
            auto& lightEntity = scene_.createEntity(importedLight.name, importedId);
            scene::TransformComponent transform;
            transform.position = importedLight.position;
            (void)scene_.setTransform(lightEntity.id, transform);
            (void)scene_.setLight(lightEntity.id, sceneLightComponent(importedLight));
        }
        for (const auto& importedCamera : model->cameras) {
            auto& cameraEntity = scene_.createEntity(importedCamera.name, importedId);
            scene::TransformComponent transform;
            transform.position = importedCamera.position;
            (void)scene_.setTransform(cameraEntity.id, transform);
            (void)scene_.setCamera(cameraEntity.id, sceneCameraComponent(importedCamera));
        }
    } else if (scene_.setMeshRenderer(importedId, scene::MeshRendererComponent {record.id})) {
        attachedMeshRenderer = true;
    }
    if (!attachedMeshRenderer) {
        core::logError(core::LogCategory::Assets, "Editor failed to attach imported model to a scene entity");
        return;
    }
    rebuildHierarchy();
    selectEntity(importedId);
    core::logInfo(
        core::LogCategory::Assets,
        "Scene object created from imported asset objectId=" + std::to_string(importedId.value())
            + " assetId=" + std::to_string(record.id.value())
            + " hiddenInternalNodes=" + std::to_string(hiddenInternalNodeCount));
}

} // namespace projectunity::editor
