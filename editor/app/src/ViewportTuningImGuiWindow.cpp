#include <projectunity/editor/MainWindow.hpp>
#include <projectunity/editor/ViewportTuningImGuiWindow.hpp>
#include <projectunity/editor/ViewportWidget.hpp>
#include <projectunity/renderer/IRenderer.hpp>

#include <imgui.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <cfloat>
#include <filesystem>
#include <utility>

#include <QGuiApplication>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>
#include <QTimer>
#include <QWheelEvent>

namespace projectunity::editor {
namespace {

[[nodiscard]] int imguiMouseButton(Qt::MouseButton button) noexcept
{
    switch (button) {
    case Qt::LeftButton: return 0;
    case Qt::RightButton: return 1;
    case Qt::MiddleButton: return 2;
    default: return -1;
    }
}

void applySoloLodPreset(EditorQualitySettings& settings)
{
    settings.graphics.preset = QualityPreset::Custom;
    settings.lod.hlodEnabled = false;
    settings.lod.debugOverride = ViewportHlodDebugOverride::ForceDetailed;
    settings.lod.quality = LodQuality::Ultra;
    settings.lod.aggressiveness = HlodAggressiveness::Low;
    settings.lod.lodDistance = std::max(settings.lod.lodDistance, 80.0F);
    settings.lod.screenError = std::min(settings.lod.screenError, 0.5F);
    settings.lod.hysteresis = std::max(settings.lod.hysteresis, 0.25F);
    settings.lod.cullingBoundsPadding = std::max(settings.lod.cullingBoundsPadding, 0.5F);
}

void applyNoCullingPreset(EditorQualitySettings& settings)
{
    applySoloLodPreset(settings);
    settings.lod.frustumCullingEnabled = false;
    settings.lod.instanceCullingEnabled = false;
    settings.lod.occlusionCullingEnabled = false;
    settings.lod.spatialCellCullingEnabled = false;
    settings.lod.cullingBoundsPadding = std::max(settings.lod.cullingBoundsPadding, 1.5F);
    settings.lod.chunkBudget = std::max(settings.lod.chunkBudget, 512);
    settings.lod.drawPacketBudget = std::max(settings.lod.drawPacketBudget, 2048);
    settings.lod.shadowCasterBudget = std::max(settings.lod.shadowCasterBudget, 2048);
    settings.debug.boundsXray = true;
    settings.debug.sourceObjects = true;
}

void applyCompleteAssetPreset(EditorQualitySettings& settings)
{
    applyNoCullingPreset(settings);
    settings.lod.forceLod0 = true;
    settings.lod.triangleBudgetEnabled = false;
    settings.lod.detailedTriangleBudget = 1'000'000'000;
    settings.lod.unlimitedStaticUploads = true;
    settings.lod.uploadBudgetMb = 16'384;
    settings.lod.uploadBatchBudget = 100'000;
}

} // namespace

ViewportTuningImGuiWindow::ViewportTuningImGuiWindow(
    EditorQualitySettings settings,
    ApplySettingsCallback applySettings,
    StatsProvider statsProvider,
    CameraSettingsCallback cameraSettings,
    QWindow* parent,
    bool embedded)
    : QOpenGLWindow(QOpenGLWindow::NoPartialUpdate, parent)
    , settings_(std::move(settings))
    , applySettings_(std::move(applySettings))
    , statsProvider_(std::move(statsProvider))
    , cameraSettings_(std::move(cameraSettings))
    , embedded_(embedded)
{
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(0);
    format.setStencilBufferSize(0);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    setFormat(format);

    if (!embedded_) {
        setFlags(Qt::Tool | Qt::Window | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
        setTitle(QStringLiteral("Viewport Tuning - Dear ImGui"));
    }
    setMinimumSize(embedded_ ? QSize(220, 480) : QSize(420, 560));
    if (!embedded_) {
        resize(500, 760);
    }

    repaintTimer_ = new QTimer(this);
    repaintTimer_->setInterval(16);
    connect(repaintTimer_, &QTimer::timeout, this, [this] { update(); });
    repaintTimer_->start();

    persistTimer_ = new QTimer(this);
    persistTimer_->setSingleShot(true);
    persistTimer_->setInterval(300);
    connect(persistTimer_, &QTimer::timeout, this, [this] {
        if (applySettings_ == nullptr) {
            return;
        }
        applyingSettings_ = true;
        applySettings_(settings_, true);
        applyingSettings_ = false;
    });
}

ViewportTuningImGuiWindow::~ViewportTuningImGuiWindow()
{
    if (imguiContext_ == nullptr || context() == nullptr) {
        return;
    }
    context()->makeCurrent(this);
    setCurrentImGuiContext();
    if (imguiReady_) {
        ImGui_ImplOpenGL3_Shutdown();
    }
    ImGui::DestroyContext(imguiContext_);
    imguiContext_ = nullptr;
    context()->doneCurrent();
}

void ViewportTuningImGuiWindow::syncSettings(const EditorQualitySettings& settings)
{
    if (!applyingSettings_) {
        settings_ = settings;
    }
}

void ViewportTuningImGuiWindow::showToolWindow()
{
    show();
    if (embedded_) {
        requestActivate();
        return;
    }
    raise();
    requestActivate();
}

void ViewportTuningImGuiWindow::initializeGL()
{
    imguiContext_ = ImGui::CreateContext();
    setCurrentImGuiContext();
    auto& io = ImGui::GetIO();
    io.BackendPlatformName = "projectunity_qt_opengl_window";
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.Fonts->AddFontDefault();

    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowRounding = 0.0F;
    style.FrameRounding = 3.0F;
    style.GrabRounding = 3.0F;
    style.FramePadding = {8.0F, 5.0F};
    style.ItemSpacing = {8.0F, 7.0F};
    style.WindowPadding = {12.0F, 12.0F};

    imguiReady_ = ImGui_ImplOpenGL3_Init("#version 330 core");
    frameTimer_.start();
}

void ViewportTuningImGuiWindow::paintGL()
{
    const auto clearWidget = [this] {
        auto* gl = context() == nullptr ? nullptr : context()->functions();
        if (gl == nullptr) {
            return;
        }
        const auto scale = static_cast<float>(devicePixelRatio());
        gl->glViewport(0, 0, static_cast<int>(static_cast<float>(width()) * scale), static_cast<int>(static_cast<float>(height()) * scale));
        gl->glClearColor(0.055F, 0.060F, 0.070F, 1.0F);
        gl->glClear(GL_COLOR_BUFFER_BIT);
    };
    if (!imguiReady_ || imguiContext_ == nullptr) {
        clearWidget();
        return;
    }
    setCurrentImGuiContext();
    auto& io = ImGui::GetIO();
    const auto scale = static_cast<float>(devicePixelRatio());
    io.DisplaySize = {static_cast<float>(width()), static_cast<float>(height())};
    io.DisplayFramebufferScale = {scale, scale};
    const auto elapsedNanoseconds = frameTimer_.nsecsElapsed();
    frameTimer_.restart();
    io.DeltaTime = std::max(static_cast<float>(elapsedNanoseconds) / 1'000'000'000.0F, 1.0F / 1000.0F);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    const auto changed = drawTuningUi();
    ImGui::Render();

    auto* gl = context()->functions();
    gl->glViewport(0, 0, static_cast<int>(static_cast<float>(width()) * scale), static_cast<int>(static_cast<float>(height()) * scale));
    gl->glClearColor(0.065F, 0.075F, 0.09F, 1.0F);
    gl->glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    if (changed) {
        applyLiveSettings();
    }
    if (importJsonRequested_) {
        importJsonRequested_ = false;
        QTimer::singleShot(0, this, [this] { importSettingsFromJson(); });
    }
}

bool ViewportTuningImGuiWindow::drawTuningUi()
{
    auto changed = false;
    ImGui::SetNextWindowPos({0.0F, 0.0F});
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    constexpr auto flags = ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("Viewport Tuning", nullptr, flags);
    const auto contentWidth = ImGui::GetContentRegionAvail().x;
    const auto narrowLayout = contentWidth < 300.0F;
    const auto controlWidth = narrowLayout
        ? std::max(118.0F, contentWidth * 0.54F)
        : std::min(260.0F, contentWidth * 0.58F);
    ImGui::PushItemWidth(controlWidth);

    ImGui::TextUnformatted("Optimization Studio");
    ImGui::SameLine();
    ImGui::TextDisabled("live profiler");
    ImGui::Separator();

    if (const auto* stats = statsProvider_ == nullptr ? nullptr : statsProvider_()) {
        const auto frameMs = stats->FPS > 0.001 ? 1000.0 / stats->FPS : 0.0;
        ImGui::Text("Frame rate: %.2f", stats->FPS);
        ImGui::Text("Frame time: %.3f ms", frameMs);
        ImGui::Text("Draw calls: %llu", static_cast<unsigned long long>(stats->vkDrawIndexed));
        ImGui::Text("Visible triangles: %llu", static_cast<unsigned long long>(stats->lastFrameVisibleTriangleCount));
    } else {
        ImGui::TextDisabled("Profiling: waiting for Vulkan frame");
    }
    ImGui::Separator();

    ImGui::TextUnformatted("Live tuning");
    ImGui::SameLine();
    ImGui::TextDisabled("runtime settings");
    ImGui::Separator();

    if (ImGui::Button("Asset completo")) {
        applyCompleteAssetPreset(settings_);
        changed = true;
    }
    if (!narrowLayout) {
        ImGui::SameLine();
    }
    if (ImGui::Button("Solo LOD")) {
        applySoloLodPreset(settings_);
        changed = true;
    }
    if (!narrowLayout) {
        ImGui::SameLine();
    }
    if (ImGui::Button("Sin culling")) {
        applyNoCullingPreset(settings_);
        changed = true;
    }
    if (!narrowLayout) {
        ImGui::SameLine();
    }
    if (ImGui::Button("Balanceado")) {
        applyQualityPreset(QualityPreset::Medium, settings_);
        balancedPresetRequested_ = true;
        changed = true;
    }
    if (!narrowLayout) {
        ImGui::SameLine();
    }
    if (ImGui::Button("Importar JSON")) {
        importJsonRequested_ = true;
    }
    if (!importJsonStatus_.empty()) {
        ImGui::TextDisabled("%s", importJsonStatus_.c_str());
    }

    if (ImGui::CollapsingHeader("LOD / HLOD", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::Checkbox("HLOD activo", &settings_.lod.hlodEnabled);

        const char* lodQualityNames[] {"Rendimiento", "Balanceado", "Calidad", "Ultra"};
        auto lodQuality = static_cast<int>(settings_.lod.quality);
        if (ImGui::Combo("Calidad LOD", &lodQuality, lodQualityNames, 4)) {
            settings_.lod.quality = static_cast<LodQuality>(lodQuality);
            changed = true;
        }

        const char* aggressivenessNames[] {"Baja", "Media", "Alta"};
        auto aggressiveness = static_cast<int>(settings_.lod.aggressiveness);
        if (ImGui::Combo("Agresividad HLOD", &aggressiveness, aggressivenessNames, 3)) {
            settings_.lod.aggressiveness = static_cast<HlodAggressiveness>(aggressiveness);
            changed = true;
        }

        const char* overrideNames[] {"Automatico", "Forzar detallado", "Forzar HLOD"};
        auto overrideValue = static_cast<int>(settings_.lod.debugOverride);
        if (ImGui::Combo("Override HLOD", &overrideValue, overrideNames, 3)) {
            settings_.lod.debugOverride = static_cast<ViewportHlodDebugOverride>(overrideValue);
            changed = true;
        }

        changed |= ImGui::DragFloat("Distancia LOD", &settings_.lod.lodDistance, 0.25F, 0.0F, 1000.0F, "%.2f m", ImGuiSliderFlags_AlwaysClamp);
        changed |= ImGui::DragFloat("Screen error", &settings_.lod.screenError, 0.01F, 0.05F, 4.0F, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        changed |= ImGui::DragFloat("Hysteresis", &settings_.lod.hysteresis, 0.005F, 0.0F, 0.45F, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        changed |= ImGui::DragInt("Chunk budget", &settings_.lod.chunkBudget, 4.0F, 1, 1'000'000, "%d", ImGuiSliderFlags_AlwaysClamp);
        changed |= ImGui::DragInt("Draw packet budget", &settings_.lod.drawPacketBudget, 8.0F, 1, 1'000'000, "%d", ImGuiSliderFlags_AlwaysClamp);
        changed |= ImGui::Checkbox("Forzar LOD0 global", &settings_.lod.forceLod0);
        changed |= ImGui::Checkbox("Presupuesto de triangulos", &settings_.lod.triangleBudgetEnabled);
        changed |= ImGui::DragInt("Triangulos detallados", &settings_.lod.detailedTriangleBudget, 25000.0F, 1'000, 1'000'000'000, "%d", ImGuiSliderFlags_AlwaysClamp);
    }

    if (ImGui::CollapsingHeader("Culling", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::Checkbox("Frustum culling", &settings_.lod.frustumCullingEnabled);
        changed |= ImGui::Checkbox("Culling por instancia", &settings_.lod.instanceCullingEnabled);
        changed |= ImGui::Checkbox("Occlusion culling", &settings_.lod.occlusionCullingEnabled);
        changed |= ImGui::Checkbox("Spatial cell culling", &settings_.lod.spatialCellCullingEnabled);
        changed |= ImGui::DragFloat("Bounds padding", &settings_.lod.cullingBoundsPadding, 0.01F, 0.0F, 25.0F, "%.2f m", ImGuiSliderFlags_AlwaysClamp);
    }

    if (ImGui::CollapsingHeader("Camera settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto cameraChanged = false;
        cameraChanged |= ImGui::DragFloat("Near plane", &cameraNearPlane_, 0.005F, 0.001F, 100.0F, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        cameraChanged |= ImGui::DragFloat("Far plane", &cameraFarPlane_, 1.0F, 1.0F, 100000.0F, "%.1f", ImGuiSliderFlags_AlwaysClamp);
        cameraChanged |= ImGui::DragFloat("Field of view", &cameraFovDegrees_, 0.25F, 5.0F, 170.0F, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
        if (cameraChanged && cameraSettings_ != nullptr) {
            cameraSettings_(cameraNearPlane_, cameraFarPlane_, cameraFovDegrees_ * 0.01745329251994329577F);
        }
    }

    if (ImGui::CollapsingHeader("Hardware info / Key Binds", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (const auto* stats = statsProvider_ == nullptr ? nullptr : statsProvider_()) {
            ImGui::Text("GPU: %s", stats->gpuName.c_str());
            ImGui::Text("Vulkan %u.%u.%u", stats->apiVersionMajor, stats->apiVersionMinor, stats->apiVersionPatch);
            ImGui::Text("Resident: %llu meshes / %llu textures",
                static_cast<unsigned long long>(stats->residentMeshCount),
                static_cast<unsigned long long>(stats->residentTextureCount));
        }
        ImGui::Separator();
        ImGui::TextUnformatted("W/A/S/D move");
        ImGui::TextUnformatted("D debug mode   E edit mode   X wireframe");
        ImGui::TextUnformatted("C vsync        F focus selected");
        ImGui::TextUnformatted("Q/W/E/R hand, move, rotate, scale");
    }

    if (ImGui::CollapsingHeader("LOD Manager")) {
        const auto* stats = statsProvider_ == nullptr ? nullptr : statsProvider_();
        if (stats == nullptr) {
            ImGui::TextDisabled("No frame data");
        } else {
            ImGui::Text("LOD draws: %llu", static_cast<unsigned long long>(stats->lastFrameLodMeshDrawCount));
            ImGui::Text("HLOD draws: %llu", static_cast<unsigned long long>(stats->lastFrameHlodMeshDrawCount));
            ImGui::Text("Triangles saved: %llu",
                static_cast<unsigned long long>(stats->lastFrameLodTriangleReductionCount + stats->lastFrameHlodTriangleReductionCount));
            ImGui::Text("Visible objects: %llu / %llu",
                static_cast<unsigned long long>(stats->lastFrameVisibleRenderInstanceCount),
                static_cast<unsigned long long>(stats->lastFrameRenderInstanceCount));
        }
    }

    if (ImGui::CollapsingHeader("Optimization Warnings")) {
        const auto* stats = statsProvider_ == nullptr ? nullptr : statsProvider_();
        if (stats == nullptr) {
            ImGui::TextDisabled("No diagnostics yet");
        } else {
            if (stats->lastFrameCandidateMeshDrawCount > 0U && stats->lastFrameMeshDrawCount == 0U) {
                ImGui::TextColored({1.0F, 0.25F, 0.25F, 1.0F}, "ERROR: candidates exist but no mesh is visible");
            }
            if (stats->lastFrameHlodRejectedNoOverviewCount > 0U) {
                ImGui::TextColored({1.0F, 0.72F, 0.25F, 1.0F}, "Missing HLOD overview: %llu",
                    static_cast<unsigned long long>(stats->lastFrameHlodRejectedNoOverviewCount));
            }
            if (stats->resourceFallback > 0U) {
                ImGui::TextColored({1.0F, 0.72F, 0.25F, 1.0F}, "GPU resource fallbacks: %llu",
                    static_cast<unsigned long long>(stats->resourceFallback));
            }
            if (stats->objectsConsidered > stats->passedFrustum) {
                ImGui::Text("CPU outside frustum: %llu",
                    static_cast<unsigned long long>(stats->objectsConsidered - stats->passedFrustum));
            }
        }
    }

    if (ImGui::CollapsingHeader("Streaming GPU", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::Checkbox("Uploads sin limite", &settings_.lod.unlimitedStaticUploads);
        changed |= ImGui::DragInt("Presupuesto upload MB", &settings_.lod.uploadBudgetMb, 4.0F, 1, 16'384, "%d MB", ImGuiSliderFlags_AlwaysClamp);
        changed |= ImGui::DragInt("Batches upload/frame", &settings_.lod.uploadBatchBudget, 1.0F, 1, 100'000, "%d", ImGuiSliderFlags_AlwaysClamp);
        ImGui::TextDisabled("Si cambia el LOD, Vulkan conserva el mesh ya cargado mientras sube el nuevo.");
    }

    if (ImGui::CollapsingHeader("Terrain cerca", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::Checkbox("Forzar LOD0 cerca", &settings_.lod.terrainNearHighQualityEnabled);
        changed |= ImGui::DragFloat("Radio LOD0", &settings_.lod.terrainNearHighQualityRadius, 0.5F, 0.0F, 2000.0F, "%.1f m", ImGuiSliderFlags_AlwaysClamp);
        changed |= ImGui::Checkbox("Desactivar HLOD terrain", &settings_.lod.debugDisableTerrainHlod);
        changed |= ImGui::Checkbox("Desactivar chunk LOD terrain", &settings_.lod.debugDisableTerrainChunkLod);
    }

    if (ImGui::CollapsingHeader("Sombras", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* shadowModeNames[] {"Live", "Frozen", "Off"};
        auto shadowMode = static_cast<int>(settings_.shadow.updateMode);
        if (ImGui::Combo("Update mode", &shadowMode, shadowModeNames, 3)) {
            settings_.shadow.updateMode = static_cast<renderer::RenderShadowUpdateMode>(shadowMode);
            settings_.shadow.quality = settings_.shadow.updateMode == renderer::RenderShadowUpdateMode::Off
                ? ShadowQuality::Off
                : (settings_.shadow.quality == ShadowQuality::Off ? ShadowQuality::High : settings_.shadow.quality);
            changed = true;
        }
        changed |= ImGui::DragInt("Resolucion shadow", &settings_.shadow.resolution, 512.0F, 512, 8192, "%d px", ImGuiSliderFlags_AlwaysClamp);
        changed |= ImGui::DragFloat("Radio frustum", &settings_.shadow.distance, 10.0F, 48.0F, 5000.0F, "%.0f m", ImGuiSliderFlags_AlwaysClamp);
        changed |= ImGui::DragInt("Budget casters", &settings_.lod.shadowCasterBudget, 8.0F, 1, 1'000'000, "%d", ImGuiSliderFlags_AlwaysClamp);
        if (const auto* stats = statsProvider_ == nullptr ? nullptr : statsProvider_()) {
            ImGui::Separator();
            ImGui::Text("Casters: %llu/%llu | batches %llu | views %llu",
                static_cast<unsigned long long>(stats->shadowSubmitted),
                static_cast<unsigned long long>(stats->shadowCandidates),
                static_cast<unsigned long long>(stats->shadowBatchesSubmitted),
                static_cast<unsigned long long>(stats->lastFrameShadowViewCount));
            const auto rejected = stats->shadowRejectedByPolicy + stats->shadowRejectedByCasterCull;
            ImGui::Text("Rechazados: policy %llu | frustum %llu",
                static_cast<unsigned long long>(stats->shadowRejectedByPolicy),
                static_cast<unsigned long long>(stats->shadowRejectedByCasterCull));
            if (rejected > 0U || stats->shadowOnlyRejectedInstances > 0U) {
                ImGui::TextColored({1.0F, 0.72F, 0.25F, 1.0F},
                    "Aviso: sube Radio frustum o Budget casters si ves cortes.");
            }
        }
    }

    if (ImGui::CollapsingHeader("Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::Checkbox("Bounds / XRay", &settings_.debug.boundsXray);
        changed |= ImGui::Checkbox("Colores LOD / HLOD", &settings_.debug.lodColors);
        settings_.lod.debugColors = settings_.debug.lodColors;
        changed |= ImGui::Checkbox("Shadow casters", &settings_.debug.shadowCasters);
        changed |= ImGui::Checkbox("Source objects", &settings_.debug.sourceObjects);
        changed |= ImGui::Checkbox("Direccion del sol", &settings_.debug.sunDirection);
    }

    if (ImGui::CollapsingHeader("Frame actual", ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto* stats = statsProvider_ == nullptr ? nullptr : statsProvider_();
        if (stats == nullptr) {
            ImGui::TextDisabled("Sin frame Vulkan todavia");
        } else {
            ImGui::Text("FPS %.1f", stats->FPS);
            ImGui::Text("Draws %llu / candidatos %llu / culled %llu",
                static_cast<unsigned long long>(stats->lastFrameMeshDrawCount),
                static_cast<unsigned long long>(stats->lastFrameCandidateMeshDrawCount),
                static_cast<unsigned long long>(stats->lastFrameCulledMeshDrawCount));
            ImGui::Text("Chunks %llu / %llu / final %llu",
                static_cast<unsigned long long>(stats->lastFrameVisibleRenderChunkCount),
                static_cast<unsigned long long>(stats->lastFrameRenderChunkCount),
                static_cast<unsigned long long>(stats->lastFrameFinalVisibleChunkCount));
            ImGui::Text("HLOD %llu | OCC rechazados %llu | CELL rechazadas %llu",
                static_cast<unsigned long long>(stats->lastFrameHlodMeshDrawCount),
                static_cast<unsigned long long>(stats->lastFrameOcclusionRejectedChunkCount),
                static_cast<unsigned long long>(stats->lastFrameSpatialCellRejectedCount));
            ImGui::Text("GPU batches %llu | draws %llu | vkDraw %llu",
                static_cast<unsigned long long>(stats->visibleBatches),
                static_cast<unsigned long long>(stats->lastFrameMeshDrawCount),
                static_cast<unsigned long long>(stats->vkDrawIndexed));
            ImGui::Text("Uploads diferidos %llu | fallback LOD %llu | %.1f MB pendientes",
                static_cast<unsigned long long>(stats->resourceDeferred),
                static_cast<unsigned long long>(stats->resourceFallback),
                static_cast<double>(stats->resourceDeferredBytes) / (1024.0 * 1024.0));
            if (stats->resourceDeferred > stats->resourceFallback) {
                ImGui::TextColored({1.0F, 0.55F, 0.25F, 1.0F}, "Faltan batches GPU: activa Uploads sin limite para diagnosticar.");
            }
        }
    }

    ImGui::PopItemWidth();
    ImGui::End();
    return changed;
}

void ViewportTuningImGuiWindow::applyLiveSettings()
{
    const auto balancedRequest = balancedPresetRequested_;
    settings_.graphics.preset = balancedRequest ? QualityPreset::Medium : QualityPreset::Custom;
    if (applySettings_ != nullptr) {
        applyingSettings_ = true;
        applySettings_(settings_, false);
        applyingSettings_ = false;
    }
    balancedPresetRequested_ = false;
    settings_.graphics.preset = QualityPreset::Custom;
    if (balancedRequest) {
        return;
    }
    if (persistTimer_ != nullptr) {
        persistTimer_->start();
    }
}

void ViewportTuningImGuiWindow::importSettingsFromJson()
{
    const auto selected = QFileDialog::getOpenFileName(
        nullptr,
        QStringLiteral("Importar configuracion JSON"),
        QString::fromStdString(projectGraphicsSettingsPath().parent_path().string()),
        QStringLiteral("ProjectUnity Graphics Settings (*.json);;JSON (*.json);;Todos los archivos (*.*)"));
    if (selected.isEmpty()) {
        importJsonStatus_ = "Importar JSON cancelado";
        return;
    }

    QFile file(selected);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        importJsonStatus_ = "ERROR: no se pudo abrir JSON";
        return;
    }

    QJsonParseError parseError {};
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        importJsonStatus_ = QStringLiteral("ERROR JSON: %1")
            .arg(parseError.errorString())
            .toStdString();
        return;
    }

    settings_ = loadEditorQualitySettings(std::filesystem::path(selected.toStdWString()));
    if (applySettings_ != nullptr) {
        applyingSettings_ = true;
        applySettings_(settings_, true);
        applyingSettings_ = false;
    }
    importJsonStatus_ = QStringLiteral("JSON importado: %1")
        .arg(QFileInfo(selected).fileName())
        .toStdString();
}

void ViewportTuningImGuiWindow::setCurrentImGuiContext() const
{
    ImGui::SetCurrentContext(imguiContext_);
}

void ViewportTuningImGuiWindow::submitKeyboardModifiers(Qt::KeyboardModifiers modifiers)
{
    if (imguiContext_ == nullptr) {
        return;
    }
    setCurrentImGuiContext();
    auto& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, modifiers.testFlag(Qt::ControlModifier));
    io.AddKeyEvent(ImGuiMod_Shift, modifiers.testFlag(Qt::ShiftModifier));
    io.AddKeyEvent(ImGuiMod_Alt, modifiers.testFlag(Qt::AltModifier));
    io.AddKeyEvent(ImGuiMod_Super, modifiers.testFlag(Qt::MetaModifier));
}

void ViewportTuningImGuiWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (imguiContext_ != nullptr) {
        setCurrentImGuiContext();
        ImGui::GetIO().AddMousePosEvent(static_cast<float>(event->position().x()), static_cast<float>(event->position().y()));
    }
    event->accept();
}

void ViewportTuningImGuiWindow::mousePressEvent(QMouseEvent* event)
{
    if (imguiContext_ != nullptr) {
        setCurrentImGuiContext();
        const auto button = imguiMouseButton(event->button());
        if (button >= 0) {
            ImGui::GetIO().AddMouseButtonEvent(button, true);
        }
        submitKeyboardModifiers(event->modifiers());
    }
    requestActivate();
    event->accept();
}

void ViewportTuningImGuiWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (imguiContext_ != nullptr) {
        setCurrentImGuiContext();
        const auto button = imguiMouseButton(event->button());
        if (button >= 0) {
            ImGui::GetIO().AddMouseButtonEvent(button, false);
        }
        submitKeyboardModifiers(event->modifiers());
    }
    event->accept();
}

void ViewportTuningImGuiWindow::wheelEvent(QWheelEvent* event)
{
    if (imguiContext_ != nullptr) {
        setCurrentImGuiContext();
        const auto delta = event->angleDelta();
        ImGui::GetIO().AddMouseWheelEvent(
            static_cast<float>(delta.x()) / 120.0F,
            static_cast<float>(delta.y()) / 120.0F);
    }
    event->accept();
}

void ViewportTuningImGuiWindow::keyPressEvent(QKeyEvent* event)
{
    if (imguiContext_ != nullptr) {
        submitKeyboardModifiers(event->modifiers());
        const auto utf8 = event->text().toUtf8();
        if (!utf8.isEmpty()) {
            setCurrentImGuiContext();
            ImGui::GetIO().AddInputCharactersUTF8(utf8.constData());
        }
    }
    event->accept();
}

void ViewportTuningImGuiWindow::keyReleaseEvent(QKeyEvent* event)
{
    submitKeyboardModifiers(event->modifiers());
    event->accept();
}

bool ViewportTuningImGuiWindow::event(QEvent* event)
{
    if (event != nullptr && event->type() == QEvent::Leave && imguiContext_ != nullptr) {
        setCurrentImGuiContext();
        ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }
    return QOpenGLWindow::event(event);
}

void MainWindow::createViewportTuningWindow(QWidget* embeddedParent)
{
    if (viewportTuningImGuiWindow_ != nullptr) {
        return;
    }
    viewportTuningImGuiWindow_ = new ViewportTuningImGuiWindow(
        qualitySettings_,
        [this](EditorQualitySettings settings, bool persist) {
            if (!persist && settings.graphics.preset == QualityPreset::Medium) {
                applyOptimizationBalancedMode();
                QTimer::singleShot(0, this, [this] { syncViewportTuningPanel(); });
                return;
            }
            applyEditorQualitySettings(std::move(settings), persist);
        },
        [this]() -> const renderer::RendererStats* {
            if (sceneViewport_ != nullptr) {
                if (const auto* stats = sceneViewport_->lastRendererStats()) {
                    return stats;
                }
            }
            return renderer_ != nullptr && renderer_->isReady() ? &renderer_->stats() : nullptr;
        },
        [this](float nearPlane, float farPlane, float verticalFovRadians) {
            for (auto* viewport : {optimizationPrimaryViewport_, optimizationDebugViewport_}) {
                if (viewport != nullptr) {
                    viewport->setSceneCameraProjection(verticalFovRadians, nearPlane, farPlane);
                }
            }
        },
        nullptr,
        embeddedParent != nullptr);
    viewportTuningImGuiWindow_->setObjectName(QStringLiteral("ViewportTuningImGuiWindow"));
}

void MainWindow::showViewportTuningWindow()
{
    createViewportTuningWindow();
    if (viewportTuningImGuiWindow_ != nullptr && viewportTuningImGuiWindow_->isEmbedded()) {
        focusOptimizationStudio();
        viewportTuningImGuiWindow_->showToolWindow();
        return;
    }
    if (viewportTuningImGuiWindow_ == nullptr
        || QGuiApplication::platformName() == QStringLiteral("offscreen")) {
        return;
    }
    viewportTuningImGuiWindow_->showToolWindow();
}

} // namespace projectunity::editor
