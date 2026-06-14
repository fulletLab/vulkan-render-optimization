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

constexpr int kProfilerRowCount = 120;

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

[[nodiscard]] QString lodBreakdownText(const renderer::RenderLodBreakdown& breakdown)
{
    return QStringLiteral("L0 %1/%2 max%3 L1 %4/%5 max%6 L2+ %7/%8 max%9 H %10/%11 max%12")
        .arg(compactCounter(breakdown.draws[0]))
        .arg(compactCounter(breakdown.triangles[0]))
        .arg(compactCounter(breakdown.maxDrawTriangles[0]))
        .arg(compactCounter(breakdown.draws[1]))
        .arg(compactCounter(breakdown.triangles[1]))
        .arg(compactCounter(breakdown.maxDrawTriangles[1]))
        .arg(compactCounter(breakdown.draws[2]))
        .arg(compactCounter(breakdown.triangles[2]))
        .arg(compactCounter(breakdown.maxDrawTriangles[2]))
        .arg(compactCounter(breakdown.draws[3]))
        .arg(compactCounter(breakdown.triangles[3]))
        .arg(compactCounter(breakdown.maxDrawTriangles[3]));
}

[[nodiscard]] QString materialDrawText(const renderer::RenderMaterialBreakdown& breakdown)
{
    return QStringLiteral("O %1 M %2 B %3 DBL %4")
        .arg(compactCounter(breakdown.opaqueDraws))
        .arg(compactCounter(breakdown.alphaMaskDraws))
        .arg(compactCounter(breakdown.blendDraws))
        .arg(compactCounter(breakdown.doubleSidedDraws));
}

[[nodiscard]] QString materialTriangleText(const renderer::RenderMaterialBreakdown& breakdown)
{
    return QStringLiteral("O %1 M %2 B %3 DBL %4")
        .arg(compactCounter(breakdown.opaqueTriangles))
        .arg(compactCounter(breakdown.alphaMaskTriangles))
        .arg(compactCounter(breakdown.blendTriangles))
        .arg(compactCounter(breakdown.doubleSidedTriangles));
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
        QStringLiteral("shadowVisibleInstances"),
        QStringLiteral("shadowOnlyCandidateInstances"),
        QStringLiteral("shadowOnlyRejectedInstances"),
        QStringLiteral("recentMaxFrameCpuUs"),
        QStringLiteral("recentMaxFrameGpuUs"),
        QStringLiteral("hitchFrameCount"),
        QStringLiteral("lastHitchFrameIndex"),
        QStringLiteral("lastHitchCpuUs"),
        QStringLiteral("lastHitchGpuUs"),
        QStringLiteral("lastHitchEditorBuildUs"),
        QStringLiteral("lastHitchRenderWorldBuildUs"),
        QStringLiteral("lastHitchResourcePrepareUs"),
        QStringLiteral("lastHitchCommandRecordUs"),
        QStringLiteral("lastHitchMeshDraws"),
        QStringLiteral("lastHitchVkDrawIndexed"),
        QStringLiteral("lastHitchMeshUploadBytes"),
        QStringLiteral("lastHitchTextureUploadBytes"),
        QStringLiteral("lastHitchStaticUploadBytes"),
        QStringLiteral("shadowCandidates"),
        QStringLiteral("shadowSubmitted"),
        QStringLiteral("shadowTriangles"),
        QStringLiteral("shadowCpuMs"),
        QStringLiteral("shadowGpuMs"),
        QStringLiteral("shadowRejectedByPolicy"),
        QStringLiteral("shadowRejectedByCasterCull"),
        QStringLiteral("lastFrameMeshUploadBytes"),
        QStringLiteral("lastFrameTextureUploadBytes"),
        QStringLiteral("lodChain RenderWorld"),
        QStringLiteral("lodChain resourcePrepared"),
        QStringLiteral("lodChain VulkanBatch"),
        QStringLiteral("lodChain vkDrawIndexed"),
        QStringLiteral("mainMaterialDraws"),
        QStringLiteral("mainMaterialTriangles"),
        QStringLiteral("shadowMaterialDraws"),
        QStringLiteral("shadowMaterialTriangles"),
        QStringLiteral("HLOD reason"),
        QStringLiteral("spatialCells"),
        QStringLiteral("spatialCellTests"),
        QStringLiteral("spatialCellRejected"),
        QStringLiteral("spatialCellCandidateChunks"),
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
        const auto hitchText = QStringLiteral("H %1 MAX %2ms")
            .arg(compactCounter(stats.hitchFrameCount))
            .arg(static_cast<double>(stats.recentMaxFrameCpuTimeUs) / 1000.0, 0, 'f', 1);
        const auto uploadText = QStringLiteral("UP M%1 T%2 %3")
            .arg(compactCounter(stats.lastFrameMeshUploadCount))
            .arg(compactCounter(stats.lastFrameTextureUploadCount))
            .arg(compactCounter(stats.lastFrameStaticUploadBytes));
        const auto statusText = QStringLiteral("%1 FPS %2 %3 | %4 %5 %6 %7 | D %8/%9 B %10 | VKD %11 BIND %12 | T %13 SUB %14 | LOD %15 -%16 HLOD %17/%18 -%19 | OCC %20/%21/%22 | SH %23 %24/%25 | %26 %27")
            .arg(sceneStatsAvailable ? QStringLiteral("Scene") : QStringLiteral("Renderer"))
            .arg(stats.FPS, 0, 'f', 1)
            .arg(hitchText)
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
            .arg(gpuShadowText);
        performanceStatus_->setText(statusText + QStringLiteral(" | %1").arg(uploadText));
        performanceStatus_->setToolTip(QStringLiteral("%1 | max CPU %2ms | last hitch CPU %3ms GPU %4ms RES %5ms CMD %6ms upload M%7 T%8 total %9 | editor %10ms | color %11us | cull %12 | HLOD %13 | RW %14 | VK %15 | MAT %16")
            .arg(gpuFrameText)
            .arg(static_cast<double>(stats.recentMaxFrameCpuTimeUs) / 1000.0, 0, 'f', 1)
            .arg(static_cast<double>(stats.lastHitchCpuTimeUs) / 1000.0, 0, 'f', 1)
            .arg(static_cast<double>(stats.lastHitchGpuTimeUs) / 1000.0, 0, 'f', 1)
            .arg(static_cast<double>(stats.lastHitchResourcePrepareCpuTimeUs) / 1000.0, 0, 'f', 1)
            .arg(static_cast<double>(stats.lastHitchCommandRecordCpuTimeUs) / 1000.0, 0, 'f', 1)
            .arg(compactCounter(stats.lastHitchMeshUploadBytes))
            .arg(compactCounter(stats.lastHitchTextureUploadBytes))
            .arg(compactCounter(stats.lastHitchStaticUploadBytes))
            .arg(static_cast<double>(stats.lastFrameEditorBuildCpuTimeUs) / 1000.0, 0, 'f', 1)
            .arg(static_cast<qulonglong>(stats.lastFrameColorRecordCpuTimeUs))
            .arg(compactCounter(stats.lastFrameCulledMeshDrawCount))
            .arg(QString::fromStdString(stats.lastFrameHlodReason))
            .arg(lodBreakdownText(stats.renderWorldSelectedLod))
            .arg(lodBreakdownText(stats.vkDrawIndexedLod))
            .arg(materialTriangleText(stats.mainMaterialDraws)));
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
    setTableValue(profilerTable_, 80, QString::number(static_cast<qulonglong>(stats.shadowVisibleInstances)));
    setTableValue(profilerTable_, 81, QString::number(static_cast<qulonglong>(stats.shadowOnlyCandidateInstances)));
    setTableValue(profilerTable_, 82, QString::number(static_cast<qulonglong>(stats.shadowOnlyRejectedInstances)));
    setTableValue(profilerTable_, 83, QString::number(static_cast<qulonglong>(stats.recentMaxFrameCpuTimeUs)));
    setTableValue(profilerTable_, 84, stats.lastFrameGpuTimestampsValid ? QString::number(static_cast<qulonglong>(stats.recentMaxFrameGpuTimeUs)) : QStringLiteral("-"));
    setTableValue(profilerTable_, 85, QString::number(static_cast<qulonglong>(stats.hitchFrameCount)));
    setTableValue(profilerTable_, 86, QString::number(static_cast<qulonglong>(stats.lastHitchFrameIndex)));
    setTableValue(profilerTable_, 87, QString::number(static_cast<qulonglong>(stats.lastHitchCpuTimeUs)));
    setTableValue(profilerTable_, 88, stats.lastHitchGpuTimeUs > 0U ? QString::number(static_cast<qulonglong>(stats.lastHitchGpuTimeUs)) : QStringLiteral("-"));
    setTableValue(profilerTable_, 89, QString::number(static_cast<qulonglong>(stats.lastHitchEditorBuildCpuTimeUs)));
    setTableValue(profilerTable_, 90, QString::number(static_cast<qulonglong>(stats.lastHitchRenderWorldBuildCpuTimeUs)));
    setTableValue(profilerTable_, 91, QString::number(static_cast<qulonglong>(stats.lastHitchResourcePrepareCpuTimeUs)));
    setTableValue(profilerTable_, 92, QString::number(static_cast<qulonglong>(stats.lastHitchCommandRecordCpuTimeUs)));
    setTableValue(profilerTable_, 93, QString::number(static_cast<qulonglong>(stats.lastHitchMeshDrawCount)));
    setTableValue(profilerTable_, 94, QString::number(static_cast<qulonglong>(stats.lastHitchVkDrawIndexed)));
    setTableValue(profilerTable_, 95, QString::number(static_cast<qulonglong>(stats.lastHitchMeshUploadBytes)));
    setTableValue(profilerTable_, 96, QString::number(static_cast<qulonglong>(stats.lastHitchTextureUploadBytes)));
    setTableValue(profilerTable_, 97, QString::number(static_cast<qulonglong>(stats.lastHitchStaticUploadBytes)));
    setTableValue(profilerTable_, 98, QString::number(static_cast<qulonglong>(stats.shadowCandidates)));
    setTableValue(profilerTable_, 99, QString::number(static_cast<qulonglong>(stats.shadowSubmitted)));
    setTableValue(profilerTable_, 100, QString::number(static_cast<qulonglong>(stats.shadowTriangles)));
    setTableValue(profilerTable_, 101, QStringLiteral("%1").arg(stats.shadowCpuMs, 0, 'f', 3));
    setTableValue(profilerTable_, 102, stats.lastFrameGpuTimestampsValid ? QStringLiteral("%1").arg(stats.shadowGpuMs, 0, 'f', 3) : QStringLiteral("-"));
    setTableValue(profilerTable_, 103, QString::number(static_cast<qulonglong>(stats.shadowRejectedByPolicy)));
    setTableValue(profilerTable_, 104, QString::number(static_cast<qulonglong>(stats.shadowRejectedByCasterCull)));
    setTableValue(profilerTable_, 105, QString::number(static_cast<qulonglong>(stats.lastFrameMeshUploadBytes)));
    setTableValue(profilerTable_, 106, QString::number(static_cast<qulonglong>(stats.lastFrameTextureUploadBytes)));
    setTableValue(profilerTable_, 107, lodBreakdownText(stats.renderWorldSelectedLod));
    setTableValue(profilerTable_, 108, lodBreakdownText(stats.resourcePreparedLod));
    setTableValue(profilerTable_, 109, lodBreakdownText(stats.vulkanBatchLod));
    setTableValue(profilerTable_, 110, lodBreakdownText(stats.vkDrawIndexedLod));
    setTableValue(profilerTable_, 111, materialDrawText(stats.mainMaterialDraws));
    setTableValue(profilerTable_, 112, materialTriangleText(stats.mainMaterialDraws));
    setTableValue(profilerTable_, 113, materialDrawText(stats.shadowMaterialDraws));
    setTableValue(profilerTable_, 114, materialTriangleText(stats.shadowMaterialDraws));
    setTableValue(
        profilerTable_,
        115,
        QStringLiteral("%1 noOverview=%2 visibleWork=%3 coverage=%4 screen=%5 clusterScreen=%6")
            .arg(QString::fromStdString(stats.lastFrameHlodReason))
            .arg(static_cast<qulonglong>(stats.lastFrameHlodRejectedNoOverviewCount))
            .arg(static_cast<qulonglong>(stats.lastFrameHlodRejectedVisibleWorkCount))
            .arg(static_cast<qulonglong>(stats.lastFrameHlodRejectedCoverageCount))
            .arg(static_cast<qulonglong>(stats.lastFrameHlodRejectedScreenCount))
            .arg(static_cast<qulonglong>(stats.lastFrameHlodRejectedClusterScreenCount)));
    setTableValue(profilerTable_, 116, QString::number(static_cast<qulonglong>(stats.lastFrameSpatialCellCount)));
    setTableValue(profilerTable_, 117, QString::number(static_cast<qulonglong>(stats.lastFrameSpatialCellTestCount)));
    setTableValue(profilerTable_, 118, QString::number(static_cast<qulonglong>(stats.lastFrameSpatialCellRejectedCount)));
    setTableValue(profilerTable_, 119, QString::number(static_cast<qulonglong>(stats.lastFrameSpatialCellCandidateChunkCount)));
}

} // namespace projectunity::editor
