#include <projectunity/editor/MainWindow.hpp>

#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLabel>
#include <QMetaObject>
#include <QPointer>
#include <QProgressBar>
#include <QStatusBar>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QtConcurrentRun>

#include <algorithm>
#include <filesystem>

namespace projectunity::editor {
namespace {

[[nodiscard]] std::filesystem::path pathFromQString(const QString& path)
{
    return std::filesystem::path(path.toStdWString());
}

} // namespace

void MainWindow::importAsset()
{
    const auto path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Import Asset"),
        QString(),
        QStringLiteral("Models (*.glb *.gltf);;Textures (*.png *.jpg *.jpeg *.ktx *.ktx2)"));
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
            : QStringLiteral("Failed %1").arg(displayName);
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
    if (assetTable_ != nullptr) {
        const auto records = assetManager_.records();
        for (int row = 0; row < static_cast<int>(records.size()); ++row) {
            if (records[static_cast<std::size_t>(row)].id == result.record.id) {
                assetTable_->setCurrentCell(row, 0);
                break;
            }
        }
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
    if (assetTable_ == nullptr) {
        return;
    }
    const auto records = assetManager_.records();
    assetTable_->setRowCount(static_cast<int>(records.size()));
    for (int row = 0; row < static_cast<int>(records.size()); ++row) {
        const auto& record = records[static_cast<std::size_t>(row)];
        assetTable_->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(record.displayName)));
        assetTable_->setItem(row, 1, new QTableWidgetItem(QString::fromUtf8(assets::toString(record.type))));
        assetTable_->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(record.sourceName)));
        assetTable_->setItem(row, 3, new QTableWidgetItem(QString::fromStdString(record.cacheFile)));
        assetTable_->setItem(row, 4, new QTableWidgetItem(QString::number(static_cast<qulonglong>(record.vertexCount))));
    }
}

void MainWindow::createImportedModelEntity(const assets::AssetRecord& record)
{
    auto& entity = scene_.createEntity(record.displayName);
    const auto importedId = entity.id;
    if (!scene_.setMeshRenderer(importedId, scene::MeshRendererComponent {record.id})) {
        core::logError(core::LogCategory::Assets, "Editor failed to attach imported model to a scene entity");
        return;
    }
    rebuildHierarchy();
    selectEntity(importedId);
    core::logInfo(core::LogCategory::Assets, "Imported model added to scene");
}

} // namespace projectunity::editor
