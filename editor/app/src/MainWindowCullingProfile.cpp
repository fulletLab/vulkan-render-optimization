#include <projectunity/editor/MainWindow.hpp>

#include <projectunity/editor/ViewportWidget.hpp>
#include <projectunity/renderer/IRenderer.hpp>

#include <QApplication>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>

namespace projectunity::editor {
namespace {

struct ProfileBounds {
    math::Vec3 center {};
    math::Vec3 extent {};
    float radius {1.0F};
};

[[nodiscard]] QString pathToQString(const std::filesystem::path& path)
{
    return QString::fromStdWString(path.wstring());
}

[[nodiscard]] std::optional<ProfileBounds> modelBounds(const assets::ModelAsset& model)
{
    bool valid = false;
    math::Vec3 minimum;
    math::Vec3 maximum;
    const auto include = [&](math::Vec3 candidateMinimum, math::Vec3 candidateMaximum) {
        if (!valid) {
            minimum = candidateMinimum;
            maximum = candidateMaximum;
            valid = true;
            return;
        }
        minimum.x = std::min(minimum.x, candidateMinimum.x);
        minimum.y = std::min(minimum.y, candidateMinimum.y);
        minimum.z = std::min(minimum.z, candidateMinimum.z);
        maximum.x = std::max(maximum.x, candidateMaximum.x);
        maximum.y = std::max(maximum.y, candidateMaximum.y);
        maximum.z = std::max(maximum.z, candidateMaximum.z);
    };
    for (const auto& instance : model.primitiveInstances) {
        include(instance.bounds.minimum, instance.bounds.maximum);
    }
    if (!valid) {
        for (const auto& primitive : model.primitives) {
            include(primitive.bounds.minimum, primitive.bounds.maximum);
        }
    }
    if (!valid) {
        return std::nullopt;
    }
    ProfileBounds bounds;
    bounds.center = (minimum + maximum) * 0.5F;
    bounds.extent = maximum - minimum;
    bounds.radius = std::max((maximum - bounds.center).length(), 1.0F);
    return bounds;
}

void printFrameCounters(const char* label, const renderer::RendererStats& stats)
{
    std::ostringstream line;
    line << label
        << ": drawCalls=" << stats.lastFrameMeshDrawCount
        << " objectsConsidered=" << stats.objectsConsidered
        << " passedFrustum=" << stats.passedFrustum
        << " visibleBatches=" << stats.visibleBatches
        << " resourcePrepared=" << stats.resourcePrepared
        << " resourcePrepareMs=" << stats.resourcePrepareMs
        << " shadowCastersSubmitted=" << stats.shadowCastersSubmitted
        << " shadowCandidateInstances=" << stats.shadowCandidateInstances
        << " shadowPolicyRejectedInstances=" << stats.shadowPolicyRejectedInstances
        << " shadowBatchesSubmitted=" << stats.shadowBatchesSubmitted
        << " shadowInstancesSubmitted=" << stats.shadowInstancesSubmitted
        << " shadowTrianglesSubmitted=" << stats.shadowTrianglesSubmitted
        << " shadowGpuUs=" << stats.lastFrameShadowGpuTimeUs
        << " vkBindVertex=" << stats.vkBindVertex
        << " vkBindIndex=" << stats.vkBindIndex
        << " vkBindDescriptors=" << stats.vkBindDescriptors
        << " vkDrawIndexed=" << stats.vkDrawIndexed
        << " trianglesSubmitted=" << stats.trianglesSubmitted
        << " commandRecordingMs=" << stats.commandRecordingMs
        << " editorBuildUs=" << stats.lastFrameEditorBuildCpuTimeUs
        << " renderWorldBuildUs=" << stats.lastFrameRenderWorldBuildCpuTimeUs
        << " FPS=" << stats.FPS
        << '\n';
    std::cerr << line.str();
    const auto logPath = qEnvironmentVariable("PROJECTUNITY_COUNTER_LOG");
    if (!logPath.isEmpty()) {
        std::ofstream log(logPath.toStdWString(), std::ios::app);
        log << line.str();
    }
}

[[nodiscard]] bool shouldAllowSmallProfileAsset() noexcept
{
    return qEnvironmentVariableIntValue("PROJECTUNITY_CULLING_PROFILE_ALLOW_SMALL") != 0;
}

} // namespace

bool MainWindow::runPhase6CullingProfile(QString* errorMessage)
{
    auto fail = [errorMessage](const QString& message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };
    if (QApplication::platformName() == QStringLiteral("offscreen")) {
        return fail(QStringLiteral("Phase 6 culling profile requires a visible Qt platform"));
    }
    if (renderer_ == nullptr || !renderer_->isReady() || sceneViewport_ == nullptr) {
        return fail(QStringLiteral("Phase 6 culling profile requires a ready Vulkan renderer and Scene View"));
    }

    auto assetPathText = qEnvironmentVariable("PROJECTUNITY_CULLING_PROFILE_ASSET");
    if (assetPathText.isEmpty()) {
        assetPathText = qEnvironmentVariable("PROJECTUNITY_PHASE6_EXTERNAL_ASSET");
    }
    if (assetPathText.isEmpty()) {
        return fail(QStringLiteral("Set PROJECTUNITY_CULLING_PROFILE_ASSET to the 400MB map before running --phase6-culling-profile"));
    }
    const auto assetPath = std::filesystem::path(assetPathText.toStdWString());
    if (!std::filesystem::exists(assetPath)) {
        return fail(QStringLiteral("Phase 6 culling profile asset does not exist: %1").arg(assetPathText));
    }
    const auto assetSizeBytes = std::filesystem::file_size(assetPath);
    if (assetSizeBytes < 300ULL * 1024ULL * 1024ULL && !shouldAllowSmallProfileAsset()) {
        return fail(QStringLiteral("Phase 6 culling profile asset is %1 MB, expected the 400MB map. Set PROJECTUNITY_CULLING_PROFILE_ALLOW_SMALL=1 only for harness smoke.")
            .arg(static_cast<qulonglong>(assetSizeBytes / (1024ULL * 1024ULL))));
    }

    newScene();
    const auto imported = importAssetFromPath(pathToQString(assetPath), true);
    if (!imported.success) {
        return fail(QStringLiteral("Phase 6 culling profile import failed: %1").arg(QString::fromStdString(imported.error)));
    }
    const auto rootId = selectedEntityId_;
    auto* root = scene_.findEntity(rootId);
    const auto model = assetManager_.model(imported.record.id);
    const auto bounds = model == nullptr ? std::optional<ProfileBounds> {} : modelBounds(*model);
    if (root == nullptr || model == nullptr || !bounds.has_value()) {
        return fail(QStringLiteral("Phase 6 culling profile could not resolve imported map bounds"));
    }

    auto* copyA = scene_.duplicateEntity(rootId);
    const auto copyAId = copyA == nullptr ? scene::EntityId {} : copyA->id;
    auto* copyB = scene_.duplicateEntity(rootId);
    const auto copyBId = copyB == nullptr ? scene::EntityId {} : copyB->id;
    root = scene_.findEntity(rootId);
    copyA = scene_.findEntity(copyAId);
    copyB = scene_.findEntity(copyBId);
    if (root == nullptr || copyA == nullptr || copyB == nullptr) {
        return fail(QStringLiteral("Phase 6 culling profile failed to duplicate the imported map twice"));
    }
    const auto spacing = std::max({bounds->extent.x, bounds->radius * 1.35F, 8.0F});
    auto transform = root->transform;
    transform.position.x -= spacing;
    (void)scene_.setTransform(rootId, transform);
    transform = copyA->transform;
    transform.position.x += spacing;
    (void)scene_.setTransform(copyA->id, transform);
    transform = copyB->transform;
    transform.position.x += spacing * 3.0F;
    (void)scene_.setTransform(copyB->id, transform);
    rebuildHierarchy();
    clearSelection();
    refreshViewports();

    QApplication::processEvents();
    const auto renderFrames = [&]() {
        for (int frame = 0; frame < 4; ++frame) {
            sceneViewport_->repaint();
            QApplication::processEvents();
        }
        return renderer_->stats();
    };
    auto waitForStaticUploadsIdle = [&]() {
        std::uint64_t previousStaticBytes = renderer_->stats().totalStaticUploadBytes;
        for (int frame = 0; frame < 60; ++frame) {
            const auto stats = renderFrames();
            if (stats.lastFrameStaticUploadBytes == 0
                && stats.totalStaticUploadBytes == previousStaticBytes) {
                return true;
            }
            previousStaticBytes = stats.totalStaticUploadBytes;
        }
        return false;
    };

    const auto center = bounds->center + root->transform.position + math::Vec3 {spacing * 2.0F, 0.0F, 0.0F};
    const auto profileRadius = bounds->radius + spacing * 2.4F;
    sceneViewport_->setCameraForTesting(center, std::clamp(profileRadius * 1.4F, 20.0F, 480.0F), 0.65F, -0.35F);
    if (!waitForStaticUploadsIdle()) {
        return fail(QStringLiteral("Phase 6 culling profile static uploads did not become idle before measurement"));
    }
    const auto facingStats = renderFrames();
    printFrameCounters("400MB duplicated map facing map", facingStats);

    sceneViewport_->setCameraForTesting(center + math::Vec3 {profileRadius * 0.72F, 0.0F, 0.0F}, std::clamp(profileRadius * 1.4F, 20.0F, 480.0F), 0.65F, -0.35F);
    const auto partialStats = renderFrames();
    printFrameCounters("400MB duplicated map partially facing map", partialStats);

    const auto emptyTarget = center + math::Vec3 {0.0F, profileRadius * 3.0F + 5000.0F, profileRadius * 3.0F + 5000.0F};
    sceneViewport_->setCameraForTesting(emptyTarget, 480.0F, 0.65F, -0.35F);
    const auto emptyStats = renderFrames();
    printFrameCounters("400MB duplicated map facing empty space", emptyStats);

    if (facingStats.lastFrameMeshDrawCount == 0 || facingStats.trianglesSubmitted == 0) {
        return fail(QStringLiteral("Phase 6 culling profile did not render the duplicated map while facing it"));
    }
    if (emptyStats.lastFrameMeshDrawCount != 0
        || emptyStats.visibleBatches != 0
        || emptyStats.resourcePrepared != 0
        || emptyStats.shadowCastersSubmitted != 0
        || emptyStats.vkBindDescriptors != 0
        || emptyStats.passedFrustum != 0) {
        return fail(QStringLiteral(
            "Facing empty space still submitted scene work: draws=%1 batches=%2 resources=%3 shadows=%4 descriptors=%5 passed=%6")
            .arg(static_cast<qulonglong>(emptyStats.lastFrameMeshDrawCount))
            .arg(static_cast<qulonglong>(emptyStats.visibleBatches))
            .arg(static_cast<qulonglong>(emptyStats.resourcePrepared))
            .arg(static_cast<qulonglong>(emptyStats.shadowCastersSubmitted))
            .arg(static_cast<qulonglong>(emptyStats.vkBindDescriptors))
            .arg(static_cast<qulonglong>(emptyStats.passedFrustum)));
    }
    if (emptyStats.trianglesSubmitted >= facingStats.trianglesSubmitted
        || emptyStats.commandRecordingMs >= facingStats.commandRecordingMs
        || emptyStats.FPS <= facingStats.FPS) {
        return fail(QStringLiteral("Facing empty space did not reduce triangles/command recording and increase FPS"));
    }
    if (partialStats.lastFrameMeshDrawCount == 0 || partialStats.lastFrameMeshDrawCount > facingStats.lastFrameMeshDrawCount) {
        return fail(QStringLiteral("Partial-facing camera did not produce a bounded partial workload"));
    }
    return true;
}

} // namespace projectunity::editor
