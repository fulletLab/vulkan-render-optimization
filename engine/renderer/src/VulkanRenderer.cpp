#include <projectunity/renderer/VulkanRenderer.hpp>

#include <projectunity/renderer/RenderDrawOrdering.hpp>

#include "VulkanPlatform.hpp"
#include "VulkanSupport.hpp"
#include "VulkanMeshCache.hpp"
#include "VulkanTextureCache.hpp"
#include "VulkanUploadContext.hpp"
#include "VulkanViewportTarget.hpp"

#include <projectunity/core/Log.hpp>

#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>

#include <algorithm>
#include <cstdint>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace projectunity::renderer {
namespace {

[[nodiscard]] std::uint64_t colorUploadBytes(const RenderFrame& frame) noexcept
{
    std::uint64_t bytes = 0;
    for (const auto& draw : frame.colorMeshDraws) {
        bytes += static_cast<std::uint64_t>(draw.vertices.size_bytes());
        bytes += static_cast<std::uint64_t>(draw.indices.size_bytes());
    }
    return bytes;
}

} // namespace

struct VulkanRenderer::Impl {
    explicit Impl(RendererConfig inputConfig)
        : config(std::move(inputConfig))
    {
        createInstance();
        selectDevice();
        createDevice();
        createAllocator();
        createUploads();
        ready = true;
        core::logInfo(core::LogCategory::Renderer, "Vulkan renderer initialized");
    }

    ~Impl()
    {
        surfaces.clear();
        meshCache.clear();
        textureCache.clear();
        uploads.reset();
        if (allocator != VK_NULL_HANDLE) {
            vmaDestroyAllocator(allocator);
        }
        if (device != VK_NULL_HANDLE) {
            vkDestroyDevice(device, nullptr);
        }
        if (instance != VK_NULL_HANDLE) {
            vkDestroyInstance(instance, nullptr);
        }
    }

    void createInstance()
    {
        validationEnabled = config.enableValidation && vulkan::hasLayer("VK_LAYER_KHRONOS_validation");
        debugMarkersAvailable = config.enableRenderDocMarkers && vulkan::hasInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        VkApplicationInfo appInfo {};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = config.applicationName.c_str();
        appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
        appInfo.pEngineName = "ProjectUnity";
        appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
        appInfo.apiVersion = VK_API_VERSION_1_2;

        std::vector<const char*> layers;
        if (validationEnabled) {
            layers.push_back("VK_LAYER_KHRONOS_validation");
        }

        std::vector<const char*> extensions;
        auto requiredExtensions = vulkan::requiredInstanceExtensions();
        vulkan::requireInstanceExtensions(requiredExtensions);
        extensions.insert(extensions.end(), requiredExtensions.begin(), requiredExtensions.end());
        if (debugMarkersAvailable) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        VkInstanceCreateInfo createInfo {};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
        createInfo.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();
        createInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.empty() ? nullptr : extensions.data();

        if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan instance");
        }
    }

    void selectDevice()
    {
        physicalDevice = vulkan::choosePhysicalDevice(instance);
        queueFamilyIndex = vulkan::findGraphicsQueueFamily(physicalDevice);

        VkPhysicalDeviceProperties properties {};
        vkGetPhysicalDeviceProperties(physicalDevice, &properties);
        std::uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, families.data());
        stats.gpuName = properties.deviceName;
        stats.apiVersionMajor = vulkan::major(properties.apiVersion);
        stats.apiVersionMinor = vulkan::minor(properties.apiVersion);
        stats.apiVersionPatch = vulkan::patch(properties.apiVersion);
        stats.validationEnabled = validationEnabled;
        stats.debugMarkersAvailable = debugMarkersAvailable;
        stats.gpuTimestampsSupported = queueFamilyIndex < families.size()
            && families[queueFamilyIndex].timestampValidBits > 0
            && properties.limits.timestampPeriod > 0.0F;
        VkPhysicalDeviceFeatures supportedFeatures {};
        vkGetPhysicalDeviceFeatures(physicalDevice, &supportedFeatures);
        geometryShaderSupported = supportedFeatures.geometryShader == VK_TRUE;
    }

    void createDevice()
    {
        constexpr float queuePriority = 1.0F;
        VkDeviceQueueCreateInfo queueInfo {};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = queueFamilyIndex;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &queuePriority;

        VkPhysicalDeviceFeatures features {};
        features.geometryShader = geometryShaderSupported ? VK_TRUE : VK_FALSE;
        VkDeviceCreateInfo createInfo {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = 1;
        createInfo.pQueueCreateInfos = &queueInfo;
        createInfo.pEnabledFeatures = &features;
        const std::vector<const char*> extensions {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        createInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();

        if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan logical device");
        }
        vkGetDeviceQueue(device, queueFamilyIndex, 0, &graphicsQueue);
    }

    void createAllocator()
    {
        VmaAllocatorCreateInfo createInfo {};
        createInfo.instance = instance;
        createInfo.physicalDevice = physicalDevice;
        createInfo.device = device;
        createInfo.vulkanApiVersion = VK_API_VERSION_1_2;

        if (vmaCreateAllocator(&createInfo, &allocator) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan Memory Allocator");
        }
        stats.vmaAllocatorReady = true;
    }

    void createUploads()
    {
        uploads = std::make_unique<VulkanUploadContext>(resourceContext());
    }

    [[nodiscard]] bool prepareSurface(const ViewportRenderSurfaceDesc& surfaceDesc, std::string* errorMessage)
    {
        if (surfaceDesc.nativeWindowHandle == nullptr || surfaceDesc.width == 0U || surfaceDesc.height == 0U) {
            if (errorMessage != nullptr) {
                *errorMessage = "Invalid Vulkan viewport surface descriptor";
            }
            return false;
        }

        auto existing = surfaces.find(surfaceDesc.nativeWindowHandle);
        if (existing != surfaces.end() && existing->second->matches(surfaceDesc)) {
            return true;
        }

        try {
            if (existing != surfaces.end()) {
                vkDeviceWaitIdle(device);
                surfaces.erase(existing);
            }
            surfaces.emplace(surfaceDesc.nativeWindowHandle, std::make_unique<VulkanViewportTarget>(viewportContext(), surfaceDesc));
            ++stats.viewportSurfacePrepareCount;
            core::logInfo(core::LogCategory::Renderer, "Prepared Vulkan viewport surface and swapchain");
            return true;
        } catch (const std::exception& error) {
            if (errorMessage != nullptr) {
                *errorMessage = error.what();
            }
            core::logWarning(core::LogCategory::Renderer, error.what());
            return false;
        }
    }

    [[nodiscard]] bool renderSurfaceFrame(
        const ViewportRenderSurfaceDesc& surfaceDesc,
        const RenderFrame& frame,
        std::string* errorMessage)
    {
        if (!prepareSurface(surfaceDesc, errorMessage)) {
            return false;
        }

        const auto existing = surfaces.find(surfaceDesc.nativeWindowHandle);
        if (existing == surfaces.end()) {
            if (errorMessage != nullptr) {
                *errorMessage = "Vulkan viewport surface was not prepared";
            }
            return false;
        }
        const auto meshUploadsBefore = meshCache.uploadCount();
        const auto textureUploadsBefore = textureCache.uploadCount();
        const auto meshBytesBefore = meshCache.uploadedBytes();
        const auto textureBytesBefore = textureCache.uploadedBytes();
        const auto dynamicColorBytes = colorUploadBytes(frame);
        const auto frameStart = std::chrono::steady_clock::now();
        const auto rendered = existing->second->renderFrame(frame, *uploads, meshCache, textureCache, errorMessage);
        const auto frameElapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - frameStart).count();
        if (rendered) {
            ++stats.viewportFramesPresented;
            stats.lastFrameCandidateMeshDrawCount = frame.candidateMeshDrawCount;
            stats.lastFrameCulledMeshDrawCount = frame.culledMeshDrawCount;
            stats.lastFrameMeshDrawCount = static_cast<std::uint64_t>(frame.meshDraws.size());
            stats.lastFrameMeshBatchCount = existing->second->lastMeshBatchCount();
            stats.lastFrameColorMeshDrawCount = static_cast<std::uint64_t>(frame.colorMeshDraws.size());
            stats.lastFrameCandidateTriangleCount = frame.candidateTriangleCount;
            stats.lastFrameCulledTriangleCount = frame.culledTriangleCount;
            stats.lastFrameLodMeshDrawCount = frame.lodMeshDrawCount;
            stats.lastFrameLodTriangleReductionCount = frame.lodTriangleReductionCount;
            stats.lastFrameHlodMeshDrawCount = frame.hlodMeshDrawCount;
            stats.lastFrameHlodCandidateDrawCount = frame.hlodCandidateDrawCount;
            stats.lastFrameHlodTriangleReductionCount = frame.hlodTriangleReductionCount;
            stats.lastFrameOcclusionTestedChunkCount = frame.occlusionTestedChunkCount;
            stats.lastFrameOcclusionRejectedChunkCount = frame.occlusionRejectedChunkCount;
            stats.lastFrameOcclusionOccluderChunkCount = frame.occlusionOccluderChunkCount;
            stats.lastFrameOcclusionRejectedInstanceCount = frame.occlusionRejectedInstanceCount;
            stats.lastFrameOcclusionRejectedTriangleCount = frame.occlusionRejectedTriangleCount;
            stats.lastFrameSceneNodeCount = frame.sceneNodeCount;
            stats.lastFrameRenderChunkCount = frame.renderChunkCount;
            stats.lastFrameVisibleRenderChunkCount = frame.visibleRenderChunkCount;
            stats.lastFrameRenderInstanceCount = frame.renderInstanceCount;
            stats.lastFrameVisibleRenderInstanceCount = frame.visibleRenderInstanceCount;
            stats.objectsConsidered = frame.candidateMeshDrawCount;
            stats.passedFrustum = frame.visibleRenderInstanceCount;
            stats.visibleBatches = existing->second->lastMeshBatchCount();
            stats.lastFrameLargeRenderChunkCount = frame.largeRenderChunkCount;
            stats.lastFrameLargestRenderChunkTriangleCount = frame.largestRenderChunkTriangleCount;
            stats.lastFrameLargestRenderChunkInstanceCount = frame.largestRenderChunkInstanceCount;
            stats.lastFrameMaxRenderChunkExtent = frame.maxRenderChunkExtent;
            const auto visibleBeforeLod = frame.candidateTriangleCount - frame.culledTriangleCount;
            const auto triangleReduction = frame.lodTriangleReductionCount + frame.hlodTriangleReductionCount;
            stats.lastFrameVisibleTriangleCount = visibleBeforeLod > triangleReduction
                ? visibleBeforeLod - triangleReduction
                : 0U;
            stats.lastFrameLightCount = static_cast<std::uint64_t>(frame.lights.size());
            stats.lastFrameShadowCasterCount = 0;
            stats.lastFrameRenderCpuTimeUs = static_cast<std::uint64_t>(std::max<std::int64_t>(frameElapsedUs, 0));
            stats.lastFrameEditorBuildCpuTimeUs = frame.editorBuildCpuTimeUs;
            stats.lastFrameRenderWorldBuildCpuTimeUs = frame.renderWorldBuildCpuTimeUs;
            stats.lastFrameRenderWorldRebuiltRecordCount = frame.renderWorldRebuiltRecordCount;
            stats.lastFrameRenderWorldReusedRecordCount = frame.renderWorldReusedRecordCount;
            const auto& profile = existing->second->lastFrameProfile();
            stats.lastFrameShadowViewCount = profile.shadowViewCount;
            stats.lastFrameShadowBatchCount = profile.shadowBatchCount;
            stats.lastFrameShadowCulledBatchCount = profile.shadowCulledBatchCount;
            stats.lastFrameShadowUpdateMode = profile.shadowUpdateMode;
            stats.lastFrameShadowMapUpdated = profile.shadowMapUpdated;
            stats.lastFrameResourcePrepareCpuTimeUs = profile.resourcePrepareCpuTimeUs;
            stats.lastFrameCommandRecordCpuTimeUs = profile.commandRecordCpuTimeUs;
            stats.lastFrameShadowRecordCpuTimeUs = profile.shadowRecordCpuTimeUs;
            stats.lastFrameMeshRecordCpuTimeUs = profile.meshRecordCpuTimeUs;
            stats.lastFrameColorRecordCpuTimeUs = profile.colorRecordCpuTimeUs;
            stats.resourcePrepared = profile.resourcePrepared;
            stats.shadowCastersSubmitted = profile.shadowCastersSubmitted;
            stats.shadowCandidateInstances = profile.shadowCandidateInstances;
            stats.shadowPolicyRejectedInstances = profile.shadowPolicyRejectedInstances;
            stats.shadowVisibleInstances = profile.shadowVisibleInstances;
            stats.shadowOnlyCandidateInstances = profile.shadowOnlyCandidateInstances;
            stats.shadowOnlyRejectedInstances = profile.shadowOnlyRejectedInstances;
            stats.shadowCandidates = profile.shadowCandidateInstances;
            stats.shadowSubmitted = profile.shadowBatchesSubmitted;
            stats.shadowTriangles = profile.shadowTrianglesSubmitted;
            stats.shadowRejectedByPolicy = profile.shadowPolicyRejectedInstances;
            stats.shadowRejectedByCasterCull = profile.shadowCulledBatchCount;
            stats.shadowCpuMs = static_cast<double>(profile.shadowRecordCpuTimeUs) / 1000.0;
            stats.shadowGpuMs = static_cast<double>(profile.shadowGpuTimeUs) / 1000.0;
            stats.shadowBatchesSubmitted = profile.shadowBatchesSubmitted;
            stats.shadowInstancesSubmitted = profile.shadowInstancesSubmitted;
            stats.shadowTrianglesSubmitted = profile.shadowTrianglesSubmitted;
            stats.lastFrameShadowCasterCount = profile.shadowCastersSubmitted;
            stats.vkBindVertex = profile.vkBindVertex;
            stats.vkBindIndex = profile.vkBindIndex;
            stats.vkBindDescriptors = profile.vkBindDescriptors;
            stats.vkDrawIndexed = profile.vkDrawIndexed;
            stats.trianglesSubmitted = profile.trianglesSubmitted;
            stats.commandRecordingMs = static_cast<double>(profile.commandRecordCpuTimeUs) / 1000.0;
            stats.resourcePrepareMs = static_cast<double>(profile.resourcePrepareCpuTimeUs) / 1000.0;
            stats.FPS = frameElapsedUs > 0 ? 1'000'000.0 / static_cast<double>(frameElapsedUs) : 0.0;
            stats.gpuTimestampsSupported = profile.gpuTimestampsSupported;
            stats.lastFrameGpuTimestampsValid = profile.gpuTimestampsValid;
            stats.lastFrameGpuTimeUs = profile.frameGpuTimeUs;
            stats.lastFrameShadowGpuTimeUs = profile.shadowGpuTimeUs;
            stats.lastFrameMeshGpuTimeUs = profile.meshGpuTimeUs;
            stats.lastFrameColorGpuTimeUs = profile.colorGpuTimeUs;
            if (stats.viewportFramesPresented == 1U) {
                stats.averageRenderCpuTimeUs = stats.lastFrameRenderCpuTimeUs;
            } else {
                stats.averageRenderCpuTimeUs = (stats.averageRenderCpuTimeUs * 15U + stats.lastFrameRenderCpuTimeUs) / 16U;
            }
            stats.meshDrawsPresented += stats.lastFrameMeshDrawCount;
            stats.colorMeshDrawsPresented += stats.lastFrameColorMeshDrawCount;
            stats.lastFrameMeshUploadCount = meshCache.uploadCount() - meshUploadsBefore;
            stats.lastFrameTextureUploadCount = textureCache.uploadCount() - textureUploadsBefore;
            stats.lastFrameStaticUploadBytes = (meshCache.uploadedBytes() - meshBytesBefore)
                + (textureCache.uploadedBytes() - textureBytesBefore);
            if (stats.viewportFramesPresented % 120U == 1U) {
                stats.recentMaxFrameCpuTimeUs = stats.lastFrameRenderCpuTimeUs;
                stats.recentMaxFrameGpuTimeUs = stats.lastFrameGpuTimeUs;
            } else {
                stats.recentMaxFrameCpuTimeUs = std::max(stats.recentMaxFrameCpuTimeUs, stats.lastFrameRenderCpuTimeUs);
                stats.recentMaxFrameGpuTimeUs = std::max(stats.recentMaxFrameGpuTimeUs, stats.lastFrameGpuTimeUs);
            }
            constexpr std::uint64_t kInteractiveHitchCpuUs = 16'667U;
            const auto averageThreshold = stats.averageRenderCpuTimeUs > 0U
                ? stats.averageRenderCpuTimeUs * 5U / 2U
                : kInteractiveHitchCpuUs;
            const auto cpuHitchThreshold = std::max(kInteractiveHitchCpuUs, averageThreshold);
            const auto gpuHitch = stats.lastFrameGpuTimestampsValid && stats.lastFrameGpuTimeUs >= kInteractiveHitchCpuUs;
            if (stats.lastFrameRenderCpuTimeUs >= cpuHitchThreshold || gpuHitch) {
                ++stats.hitchFrameCount;
                stats.lastHitchFrameIndex = stats.viewportFramesPresented;
                stats.lastHitchCpuTimeUs = stats.lastFrameRenderCpuTimeUs;
                stats.lastHitchGpuTimeUs = stats.lastFrameGpuTimestampsValid ? stats.lastFrameGpuTimeUs : 0U;
                stats.lastHitchEditorBuildCpuTimeUs = stats.lastFrameEditorBuildCpuTimeUs;
                stats.lastHitchRenderWorldBuildCpuTimeUs = stats.lastFrameRenderWorldBuildCpuTimeUs;
                stats.lastHitchResourcePrepareCpuTimeUs = stats.lastFrameResourcePrepareCpuTimeUs;
                stats.lastHitchCommandRecordCpuTimeUs = stats.lastFrameCommandRecordCpuTimeUs;
                stats.lastHitchMeshDrawCount = stats.lastFrameMeshDrawCount;
                stats.lastHitchVkDrawIndexed = stats.vkDrawIndexed;
                stats.lastHitchStaticUploadBytes = stats.lastFrameStaticUploadBytes;
            }
            stats.lastFrameColorUploadBytes = dynamicColorBytes;
            stats.residentMeshCount = meshCache.meshCount();
            stats.residentTextureCount = textureCache.textureCount();
            stats.totalMeshUploadCount = meshCache.uploadCount();
            stats.totalTextureUploadCount = textureCache.uploadCount();
            stats.totalStaticUploadBytes = meshCache.uploadedBytes() + textureCache.uploadedBytes();
            stats.totalColorUploadBytes += dynamicColorBytes;
            for (const auto& draw : frame.meshDraws) {
                if (draw.baseColorTexture != nullptr && draw.baseColorTexture->id.isValid()) {
                    ++stats.texturedMeshDrawsPresented;
                }
            }
            if (frame.shadowsEnabled) {
                ++stats.shadowFramesPresented;
                stats.shadowCasterDrawsPresented += stats.shadowCastersSubmitted;
            }
        }
        return rendered;
    }

    void releaseSurface(void* nativeWindowHandle) noexcept
    {
        if (nativeWindowHandle == nullptr) {
            return;
        }

        auto existing = surfaces.find(nativeWindowHandle);
        if (existing == surfaces.end()) {
            return;
        }

        if (device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device);
        }
        surfaces.erase(existing);
    }

    [[nodiscard]] VulkanViewportContext viewportContext() const noexcept
    {
        return {instance, physicalDevice, device, graphicsQueue, allocator, queueFamilyIndex, geometryShaderSupported};
    }

    [[nodiscard]] VulkanResourceContext resourceContext() const noexcept
    {
        return {physicalDevice, device, graphicsQueue, allocator, queueFamilyIndex, geometryShaderSupported};
    }

    RendererConfig config;
    RendererStats stats;
    VkInstance instance {VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice {VK_NULL_HANDLE};
    VkDevice device {VK_NULL_HANDLE};
    VkQueue graphicsQueue {VK_NULL_HANDLE};
    VmaAllocator allocator {VK_NULL_HANDLE};
    std::unique_ptr<VulkanUploadContext> uploads;
    VulkanMeshCache meshCache;
    VulkanTextureCache textureCache;
    std::unordered_map<void*, std::unique_ptr<VulkanViewportTarget>> surfaces;
    std::uint32_t queueFamilyIndex {0};
    bool validationEnabled {false};
    bool debugMarkersAvailable {false};
    bool geometryShaderSupported {false};
    bool ready {false};
};

VulkanRenderer::VulkanRenderer(RendererConfig config)
    : impl_(std::make_unique<Impl>(std::move(config)))
{
}

VulkanRenderer::~VulkanRenderer() = default;

const RendererStats& VulkanRenderer::stats() const noexcept
{
    return impl_->stats;
}

bool VulkanRenderer::isReady() const noexcept
{
    return impl_->ready;
}

bool VulkanRenderer::supportsSurface(const ViewportRenderSurfaceDesc& surface) const noexcept
{
    return surface.nativeWindowHandle != nullptr && surface.width > 0 && surface.height > 0;
}

bool VulkanRenderer::prepareSurface(const ViewportRenderSurfaceDesc& surface, std::string* errorMessage)
{
    return impl_->prepareSurface(surface, errorMessage);
}

bool VulkanRenderer::renderSurfaceFrame(
    const ViewportRenderSurfaceDesc& surface,
    const RenderFrame& frame,
    std::string* errorMessage)
{
    return impl_->renderSurfaceFrame(surface, frame, errorMessage);
}

void VulkanRenderer::releaseSurface(void* nativeWindowHandle) noexcept
{
    impl_->releaseSurface(nativeWindowHandle);
}

std::unique_ptr<IRenderer> createVulkanRenderer(RendererConfig config, std::string* errorMessage)
{
    try {
        return std::make_unique<VulkanRenderer>(std::move(config));
    } catch (const std::exception& error) {
        if (errorMessage != nullptr) {
            *errorMessage = error.what();
        }
        core::logError(core::LogCategory::Renderer, error.what());
        return nullptr;
    }
}

} // namespace projectunity::renderer
