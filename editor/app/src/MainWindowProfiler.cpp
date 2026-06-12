#include <projectunity/editor/MainWindow.hpp>

#include <projectunity/editor/ViewportWidget.hpp>
#include <projectunity/renderer/IRenderer.hpp>

#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QString>
#include <QStringList>

#include <cstdint>

namespace projectunity::editor {
namespace {

constexpr int kProfilerRowCount = 80;

[[nodiscard]] QString shadowUpdateModeName(renderer::RenderShadowUpdateMode mode)
{
    switch (mode) {
    case renderer::RenderShadowUpdateMode::Live:
        return QStringLiteral("Live");
    case renderer::RenderShadowUpdateMode::Frozen:
        return QStringLiteral("Frozen");
    case renderer::RenderShadowUpdateMode::Off:
        return QStringLiteral("Off");
    }
    return QStringLiteral("Unknown");
}

void setTableValue(QTableWidget* table, int row, const QString& value)
{
    if (table == nullptr || row < 0 || row >= table->rowCount()) {
        return;
    }
    auto* item = table->item(row, 1);
    if (item == nullptr) {
        item = new QTableWidgetItem;
        table->setItem(row, 1, item);
    }
    if (item->text() == value) {
        return;
    }
    item->setText(value);
}

[[nodiscard]] QString formatSkippedValue(std::uint64_t skipped, std::uint64_t candidates)
{
    const auto percent = candidates == 0U ? 0.0 : (static_cast<double>(skipped) * 100.0) / static_cast<double>(candidates);
    return QStringLiteral("%1 / %2 (%3%)")
        .arg(static_cast<qulonglong>(skipped))
        .arg(static_cast<qulonglong>(candidates))
        .arg(percent, 0, 'f', 1);
}

void setRatioValue(QTableWidget* table, int row, std::uint64_t visible, std::uint64_t total)
{
    setTableValue(table, row, QStringLiteral("%1 / %2").arg(static_cast<qulonglong>(visible)).arg(static_cast<qulonglong>(total)));
}

[[nodiscard]] QString compactCounter(std::uint64_t value)
{
    if (value >= 1'000'000'000ULL) {
        return QStringLiteral("%1B").arg(static_cast<double>(value) / 1'000'000'000.0, 0, 'f', 1);
    }
    if (value >= 1'000'000ULL) {
        return QStringLiteral("%1M").arg(static_cast<double>(value) / 1'000'000.0, 0, 'f', 1);
    }
    if (value >= 1'000ULL) {
        return QStringLiteral("%1K").arg(static_cast<double>(value) / 1'000.0, 0, 'f', 1);
    }
    return QString::number(static_cast<qulonglong>(value));
}

void ensureProfilerRows(QTableWidget* table)
{
    if (table == nullptr) {
        return;
    }
    if (table->rowCount() < kProfilerRowCount) {
        table->setRowCount(kProfilerRowCount);
    }
    const QStringList rows {
        QStringLiteral("objectsConsidered"),
        QStringLiteral("passedFrustum"),
        QStringLiteral("visibleBatches"),
        QStringLiteral("resourcePrepared"),
        QStringLiteral("shadowCastersSubmitted"),
        QStringLiteral("vkBindVertex"),
        QStringLiteral("vkBindIndex"),
        QStringLiteral("vkBindDescriptors"),
        QStringLiteral("vkDrawIndexed"),
        QStringLiteral("trianglesSubmitted"),
        QStringLiteral("commandRecordingMs"),
        QStringLiteral("resourcePrepareMs"),
        QStringLiteral("FPS"),
        QStringLiteral("shadowUpdateMode"),
        QStringLiteral("shadowMapUpdated"),
        QStringLiteral("shadowCandidateInstances"),
        QStringLiteral("shadowPolicyRejectedInstances"),
        QStringLiteral("shadowBatchesSubmitted"),
        QStringLiteral("shadowInstancesSubmitted"),
        QStringLiteral("shadowTrianglesSubmitted"),
        QStringLiteral("occlusionTestedChunks"),
        QStringLiteral("occlusionRejectedChunks"),
        QStringLiteral("occlusionOccluderChunks"),
        QStringLiteral("occlusionRejectedInstances"),
        QStringLiteral("occlusionRejectedTriangles"),
    };
    for (int index = 0; index < rows.size(); ++index) {
        const auto row = 55 + index;
        auto* item = table->item(row, 0);
        if (item == nullptr) {
            item = new QTableWidgetItem;
            table->setItem(row, 0, item);
        }
        if (item->text() != rows.at(index)) {
            item->setText(rows.at(index));
        }
        if (table->item(row, 1) == nullptr) {
            table->setItem(row, 1, new QTableWidgetItem(QStringLiteral("-")));
        }
    }
}

} // namespace

void MainWindow::updateProfilerPanel()
{
    if (profilerTable_ == nullptr) {
        if (performanceStatus_ != nullptr) {
            performanceStatus_->setText(QStringLiteral("FPS -"));
        }
        return;
    }
    ensureProfilerRows(profilerTable_);
    if (renderer_ == nullptr || !renderer_->isReady()) {
        if (performanceStatus_ != nullptr) {
            performanceStatus_->setText(QStringLiteral("FPS -"));
        }
        setTableValue(profilerTable_, 0, QStringLiteral("Unavailable"));
        for (int row = 1; row < profilerTable_->rowCount(); ++row) {
            setTableValue(profilerTable_, row, QStringLiteral("-"));
        }
        return;
    }

    auto* statsSource = sceneViewport_ != nullptr ? sceneViewport_->lastRendererStats() : nullptr;
    const auto sceneStatsAvailable = statsSource != nullptr;
    if (statsSource == nullptr) {
        statsSource = &renderer_->stats();
    }
    const auto& stats = *statsSource;
    if (performanceStatus_ != nullptr) {
        const auto gpuFrameText = stats.lastFrameGpuTimestampsValid
            ? QStringLiteral("GPU %1ms").arg(static_cast<double>(stats.lastFrameGpuTimeUs) / 1000.0, 0, 'f', 1)
            : QStringLiteral("GPU -");
        const auto gpuMeshText = stats.lastFrameGpuTimestampsValid
            ? QStringLiteral("MGPU %1ms").arg(static_cast<double>(stats.lastFrameMeshGpuTimeUs) / 1000.0, 0, 'f', 1)
            : QStringLiteral("MGPU -");
        const auto gpuShadowText = stats.lastFrameGpuTimestampsValid
            ? QStringLiteral("SGPU %1ms").arg(static_cast<double>(stats.lastFrameShadowGpuTimeUs) / 1000.0, 0, 'f', 1)
            : QStringLiteral("SGPU -");
        const auto cpuText = QStringLiteral("CPU %1ms").arg(static_cast<double>(stats.lastFrameRenderCpuTimeUs) / 1000.0, 0, 'f', 1);
        const auto worldText = QStringLiteral("WORLD %1ms").arg(static_cast<double>(stats.lastFrameRenderWorldBuildCpuTimeUs) / 1000.0, 0, 'f', 1);
        const auto cmdText = QStringLiteral("CMD %1ms").arg(stats.commandRecordingMs, 0, 'f', 1);
        const auto resText = QStringLiteral("RES %1ms").arg(stats.resourcePrepareMs, 0, 'f', 1);
        performanceStatus_->setText(QStringLiteral("%1 FPS %2 | %3 %4 %5 %6 | D %7/%8 B %9 | VKD %10 BIND %11 | T %12 SUB %13 | LOD %14 -%15 HLOD %16/%17 -%18 | OCC %19/%20/%21 | SH %22 %23/%24 | %25 %26")
            .arg(sceneStatsAvailable ? QStringLiteral("Scene") : QStringLiteral("Renderer"))
            .arg(stats.FPS, 0, 'f', 1)
            .arg(cpuText)
            .arg(worldText)
            .arg(cmdText)
            .arg(resText)
            .arg(compactCounter(stats.lastFrameMeshDrawCount))
            .arg(compactCounter(stats.lastFrameCandidateMeshDrawCount))
            .arg(compactCounter(stats.lastFrameMeshBatchCount))
            .arg(compactCounter(stats.vkDrawIndexed))
            .arg(compactCounter(stats.vkBindVertex + stats.vkBindIndex + stats.vkBindDescriptors))
            .arg(compactCounter(stats.lastFrameVisibleTriangleCount))
            .arg(compactCounter(stats.trianglesSubmitted))
            .arg(compactCounter(stats.lastFrameLodMeshDrawCount))
            .arg(compactCounter(stats.lastFrameLodTriangleReductionCount))
            .arg(compactCounter(stats.lastFrameHlodMeshDrawCount))
            .arg(compactCounter(stats.lastFrameHlodCandidateDrawCount))
            .arg(compactCounter(stats.lastFrameHlodTriangleReductionCount))
            .arg(compactCounter(stats.lastFrameOcclusionTestedChunkCount))
            .arg(compactCounter(stats.lastFrameOcclusionRejectedChunkCount))
            .arg(compactCounter(stats.lastFrameOcclusionOccluderChunkCount))
            .arg(shadowUpdateModeName(stats.lastFrameShadowUpdateMode))
            .arg(compactCounter(stats.lastFrameShadowViewCount))
            .arg(compactCounter(stats.shadowBatchesSubmitted))
            .arg(gpuMeshText)
            .arg(gpuShadowText));
        performanceStatus_->setToolTip(QStringLiteral("%1 | editor %2ms | color %3us | cull %4")
            .arg(gpuFrameText)
            .arg(static_cast<double>(stats.lastFrameEditorBuildCpuTimeUs) / 1000.0, 0, 'f', 1)
            .arg(static_cast<qulonglong>(stats.lastFrameColorRecordCpuTimeUs))
            .arg(compactCounter(stats.lastFrameCulledMeshDrawCount)));
    }
    setTableValue(
        profilerTable_,
        0,
        QStringLiteral("%1 (%2)")
            .arg(QString::fromStdString(stats.gpuName))
            .arg(sceneStatsAvailable ? QStringLiteral("Scene View") : QStringLiteral("Renderer")));
    setTableValue(
        profilerTable_,
        1,
        QStringLiteral("%1.%2.%3")
            .arg(stats.apiVersionMajor)
            .arg(stats.apiVersionMinor)
            .arg(stats.apiVersionPatch));
    setTableValue(profilerTable_, 2, QStringLiteral("%1 us").arg(static_cast<qulonglong>(stats.lastFrameRenderCpuTimeUs)));
    setTableValue(profilerTable_, 3, QStringLiteral("%1 us").arg(static_cast<qulonglong>(stats.averageRenderCpuTimeUs)));
    setTableValue(profilerTable_, 4, QStringLiteral("%1 us").arg(static_cast<qulonglong>(stats.lastFrameResourcePrepareCpuTimeUs)));
    setTableValue(profilerTable_, 5, QStringLiteral("%1 us").arg(static_cast<qulonglong>(stats.lastFrameCommandRecordCpuTimeUs)));
    setTableValue(profilerTable_, 6, QStringLiteral("%1 us").arg(static_cast<qulonglong>(stats.lastFrameShadowRecordCpuTimeUs)));
    setTableValue(profilerTable_, 7, QStringLiteral("%1 us").arg(static_cast<qulonglong>(stats.lastFrameMeshRecordCpuTimeUs)));
    setTableValue(profilerTable_, 8, QStringLiteral("%1 us").arg(static_cast<qulonglong>(stats.lastFrameColorRecordCpuTimeUs)));
    setTableValue(profilerTable_, 9, QString::number(static_cast<qulonglong>(stats.viewportFramesPresented)));
    setTableValue(profilerTable_, 10, QString::number(static_cast<qulonglong>(stats.lastFrameSceneNodeCount)));
    setTableValue(profilerTable_, 11, QString::number(static_cast<qulonglong>(stats.lastFrameRenderChunkCount)));
    setRatioValue(profilerTable_, 12, stats.lastFrameVisibleRenderChunkCount, stats.lastFrameRenderChunkCount);
    setTableValue(profilerTable_, 13, QString::number(static_cast<qulonglong>(stats.lastFrameRenderInstanceCount)));
    setRatioValue(profilerTable_, 14, stats.lastFrameVisibleRenderInstanceCount, stats.lastFrameRenderInstanceCount);
    setTableValue(profilerTable_, 15, QString::number(static_cast<qulonglong>(stats.lastFrameLargeRenderChunkCount)));
    setTableValue(profilerTable_, 16, QStringLiteral("%1").arg(stats.lastFrameMaxRenderChunkExtent, 0, 'f', 2));
    setTableValue(profilerTable_, 17, QString::number(static_cast<qulonglong>(stats.lastFrameLargestRenderChunkTriangleCount)));
    setTableValue(profilerTable_, 18, QString::number(static_cast<qulonglong>(stats.lastFrameLargestRenderChunkInstanceCount)));
    setTableValue(profilerTable_, 19, QString::number(static_cast<qulonglong>(stats.lastFrameCandidateMeshDrawCount)));
    setTableValue(profilerTable_, 20, formatSkippedValue(stats.lastFrameCulledMeshDrawCount, stats.lastFrameCandidateMeshDrawCount));
    setTableValue(profilerTable_, 21, QString::number(static_cast<qulonglong>(stats.lastFrameMeshDrawCount)));
    setTableValue(profilerTable_, 22, QString::number(static_cast<qulonglong>(stats.lastFrameMeshBatchCount)));
    setRatioValue(profilerTable_, 23, stats.lastFrameVisibleTriangleCount, stats.lastFrameCandidateTriangleCount);
    setTableValue(profilerTable_, 24, formatSkippedValue(stats.lastFrameCulledTriangleCount, stats.lastFrameCandidateTriangleCount));
    setTableValue(profilerTable_, 25, QString::number(static_cast<qulonglong>(stats.lastFrameLodMeshDrawCount)));
    setTableValue(profilerTable_, 26, QString::number(static_cast<qulonglong>(stats.lastFrameLodTriangleReductionCount)));
    setRatioValue(profilerTable_, 27, stats.lastFrameHlodMeshDrawCount, stats.lastFrameHlodCandidateDrawCount);
    setTableValue(profilerTable_, 28, QString::number(static_cast<qulonglong>(stats.lastFrameHlodTriangleReductionCount)));
    setTableValue(profilerTable_, 29, QString::number(static_cast<qulonglong>(stats.meshDrawsPresented)));
    setTableValue(profilerTable_, 30, QString::number(static_cast<qulonglong>(stats.texturedMeshDrawsPresented)));
    setTableValue(profilerTable_, 31, QString::number(static_cast<qulonglong>(stats.lastFrameColorMeshDrawCount)));
    setTableValue(profilerTable_, 32, QStringLiteral("%1 bytes").arg(static_cast<qulonglong>(stats.lastFrameColorUploadBytes)));
    setTableValue(profilerTable_, 33, QStringLiteral("%1 bytes").arg(static_cast<qulonglong>(stats.totalColorUploadBytes)));
    setTableValue(profilerTable_, 34, QStringLiteral("%1 / %2").arg(static_cast<qulonglong>(stats.lastFrameMeshUploadCount)).arg(static_cast<qulonglong>(stats.totalMeshUploadCount)));
    setTableValue(profilerTable_, 35, QStringLiteral("%1 / %2").arg(static_cast<qulonglong>(stats.lastFrameTextureUploadCount)).arg(static_cast<qulonglong>(stats.totalTextureUploadCount)));
    setTableValue(profilerTable_, 36, QStringLiteral("%1 bytes").arg(static_cast<qulonglong>(stats.lastFrameStaticUploadBytes)));
    setTableValue(profilerTable_, 37, QStringLiteral("%1 bytes").arg(static_cast<qulonglong>(stats.totalStaticUploadBytes)));
    setTableValue(profilerTable_, 38, QStringLiteral("%1 / %2").arg(static_cast<qulonglong>(stats.residentMeshCount)).arg(static_cast<qulonglong>(stats.residentTextureCount)));
    setTableValue(profilerTable_, 39, QString::number(static_cast<qulonglong>(stats.lastFrameLightCount)));
    setTableValue(profilerTable_, 40, QString::number(static_cast<qulonglong>(stats.shadowFramesPresented)));
    setTableValue(profilerTable_, 41, QString::number(static_cast<qulonglong>(stats.lastFrameShadowViewCount)));
    setTableValue(profilerTable_, 42, QString::number(static_cast<qulonglong>(stats.lastFrameShadowBatchCount)));
    setTableValue(profilerTable_, 43, QString::number(static_cast<qulonglong>(stats.lastFrameShadowCulledBatchCount)));
    setTableValue(profilerTable_, 44, QStringLiteral("%1 / %2").arg(static_cast<qulonglong>(stats.lastFrameShadowCasterCount)).arg(static_cast<qulonglong>(stats.shadowCasterDrawsPresented)));
    setTableValue(profilerTable_, 45, stats.gpuTimestampsSupported ? QStringLiteral("yes") : QStringLiteral("no"));
    setTableValue(profilerTable_, 46, stats.lastFrameGpuTimestampsValid ? QStringLiteral("%1 us").arg(static_cast<qulonglong>(stats.lastFrameGpuTimeUs)) : QStringLiteral("-"));
    setTableValue(profilerTable_, 47, stats.lastFrameGpuTimestampsValid ? QStringLiteral("%1 us").arg(static_cast<qulonglong>(stats.lastFrameShadowGpuTimeUs)) : QStringLiteral("-"));
    setTableValue(profilerTable_, 48, stats.lastFrameGpuTimestampsValid ? QStringLiteral("%1 us").arg(static_cast<qulonglong>(stats.lastFrameMeshGpuTimeUs)) : QStringLiteral("-"));
    setTableValue(profilerTable_, 49, stats.lastFrameGpuTimestampsValid ? QStringLiteral("%1 us").arg(static_cast<qulonglong>(stats.lastFrameColorGpuTimeUs)) : QStringLiteral("-"));
    setTableValue(profilerTable_, 50, QString::number(static_cast<qulonglong>(stats.viewportSurfacePrepareCount)));
    setTableValue(profilerTable_, 51, QStringLiteral("%1 us").arg(static_cast<qulonglong>(stats.lastFrameEditorBuildCpuTimeUs)));
    setTableValue(profilerTable_, 52, QStringLiteral("%1 us").arg(static_cast<qulonglong>(stats.lastFrameRenderWorldBuildCpuTimeUs)));
    setTableValue(profilerTable_, 53, QString::number(static_cast<qulonglong>(stats.lastFrameRenderWorldRebuiltRecordCount)));
    setTableValue(profilerTable_, 54, QString::number(static_cast<qulonglong>(stats.lastFrameRenderWorldReusedRecordCount)));
    setTableValue(profilerTable_, 55, QString::number(static_cast<qulonglong>(stats.objectsConsidered)));
    setTableValue(profilerTable_, 56, QString::number(static_cast<qulonglong>(stats.passedFrustum)));
    setTableValue(profilerTable_, 57, QString::number(static_cast<qulonglong>(stats.visibleBatches)));
    setTableValue(profilerTable_, 58, QString::number(static_cast<qulonglong>(stats.resourcePrepared)));
    setTableValue(profilerTable_, 59, QString::number(static_cast<qulonglong>(stats.shadowCastersSubmitted)));
    setTableValue(profilerTable_, 60, QString::number(static_cast<qulonglong>(stats.vkBindVertex)));
    setTableValue(profilerTable_, 61, QString::number(static_cast<qulonglong>(stats.vkBindIndex)));
    setTableValue(profilerTable_, 62, QString::number(static_cast<qulonglong>(stats.vkBindDescriptors)));
    setTableValue(profilerTable_, 63, QString::number(static_cast<qulonglong>(stats.vkDrawIndexed)));
    setTableValue(profilerTable_, 64, QString::number(static_cast<qulonglong>(stats.trianglesSubmitted)));
    setTableValue(profilerTable_, 65, QStringLiteral("%1").arg(stats.commandRecordingMs, 0, 'f', 3));
    setTableValue(profilerTable_, 66, QStringLiteral("%1").arg(stats.resourcePrepareMs, 0, 'f', 3));
    setTableValue(profilerTable_, 67, QStringLiteral("%1").arg(stats.FPS, 0, 'f', 1));
    setTableValue(profilerTable_, 68, shadowUpdateModeName(stats.lastFrameShadowUpdateMode));
    setTableValue(profilerTable_, 69, stats.lastFrameShadowMapUpdated ? QStringLiteral("yes") : QStringLiteral("no"));
    setTableValue(profilerTable_, 70, QString::number(static_cast<qulonglong>(stats.shadowCandidateInstances)));
    setTableValue(profilerTable_, 71, QString::number(static_cast<qulonglong>(stats.shadowPolicyRejectedInstances)));
    setTableValue(profilerTable_, 72, QString::number(static_cast<qulonglong>(stats.shadowBatchesSubmitted)));
    setTableValue(profilerTable_, 73, QString::number(static_cast<qulonglong>(stats.shadowInstancesSubmitted)));
    setTableValue(profilerTable_, 74, QString::number(static_cast<qulonglong>(stats.shadowTrianglesSubmitted)));
    setTableValue(profilerTable_, 75, QString::number(static_cast<qulonglong>(stats.lastFrameOcclusionTestedChunkCount)));
    setTableValue(profilerTable_, 76, QString::number(static_cast<qulonglong>(stats.lastFrameOcclusionRejectedChunkCount)));
    setTableValue(profilerTable_, 77, QString::number(static_cast<qulonglong>(stats.lastFrameOcclusionOccluderChunkCount)));
    setTableValue(profilerTable_, 78, QString::number(static_cast<qulonglong>(stats.lastFrameOcclusionRejectedInstanceCount)));
    setTableValue(profilerTable_, 79, QString::number(static_cast<qulonglong>(stats.lastFrameOcclusionRejectedTriangleCount)));
}

} // namespace projectunity::editor
