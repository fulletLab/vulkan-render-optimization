#include "VulkanViewportTarget.hpp"
#include "VulkanDebugLabels.hpp"
#include "VulkanSupport.hpp"
#include <projectunity/core/Log.hpp>
#include <projectunity/renderer/RenderDrawOrdering.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <stdexcept>
namespace projectunity::renderer {
namespace {
[[nodiscard]] std::span<const std::byte> instanceBytes(const std::vector<VulkanGpuInstance>& instances)
{
    return {
        reinterpret_cast<const std::byte*>(instances.data()),
        instances.size() * sizeof(VulkanGpuInstance),
    };
}
[[nodiscard]] bool canBatch(const RenderMeshDraw& lhs, const RenderMeshDraw& rhs) noexcept
{
    return lhs.modelAssetId == rhs.modelAssetId
        && lhs.primitiveIndex == rhs.primitiveIndex
        && lhs.lodIndex == rhs.lodIndex
        && lhs.material == rhs.material
        && lhs.baseColorTexture == rhs.baseColorTexture
        && lhs.normalTexture == rhs.normalTexture
        && lhs.metallicRoughnessTexture == rhs.metallicRoughnessTexture
        && lhs.occlusionTexture == rhs.occlusionTexture
        && lhs.emissiveTexture == rhs.emissiveTexture
        && lhs.flipsWinding == rhs.flipsWinding
        && lhs.castsShadow == rhs.castsShadow
        && isTransparentMeshDraw(lhs) == isTransparentMeshDraw(rhs);
}
[[nodiscard]] std::uint64_t elapsedUs(std::chrono::steady_clock::time_point start) noexcept
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count());
}
void copyGpuTimes(VulkanViewportFrameProfile& profile, const VulkanGpuFrameTimes& times) noexcept
{
    profile.gpuTimestampsSupported = times.supported;
    profile.gpuTimestampsValid = times.valid;
    profile.frameGpuTimeUs = times.frameGpuTimeUs;
    profile.shadowGpuTimeUs = times.shadowGpuTimeUs;
    profile.meshGpuTimeUs = times.meshGpuTimeUs;
    profile.colorGpuTimeUs = times.colorGpuTimeUs;
}
} // namespace
VulkanViewportTarget::VulkanViewportTarget(VulkanViewportContext context, ViewportRenderSurfaceDesc desc)
    : context_(context)
    , desc_(desc)
{
    try {
        createSurface();
        createSwapchain();
        createImageViews();
        meshPipeline_ = std::make_unique<VulkanMeshPipeline>(context_.resources(), surfaceFormat_.format);
        shadowPipeline_ = std::make_unique<VulkanShadowPipeline>(
            context_.resources(),
            meshPipeline_->textureLayout(),
            meshPipeline_->depthFormat());
        colorPipeline_ = std::make_unique<VulkanColorPipeline>(context_.resources(), meshPipeline_->renderPass());
        std::string frameError;
        if (!frameData_.create(context_.resources(), &frameError)) {
            throw std::runtime_error(frameError);
        }
        createDepthTarget();
        createFramebuffers();
        createDescriptors();
        createCommands();
        std::string profilerError;
        if (!gpuProfiler_.create(context_.resources(), &profilerError) && !profilerError.empty()) {
            core::logWarning(core::LogCategory::Renderer, profilerError);
        }
        createSync();
    } catch (...) {
        destroy();
        throw;
    }
}
VulkanViewportTarget::~VulkanViewportTarget()
{
    destroy();
}
bool VulkanViewportTarget::matches(const ViewportRenderSurfaceDesc& surfaceDesc) const noexcept
{
    return desc_.width == surfaceDesc.width
        && desc_.height == surfaceDesc.height
        && desc_.vsync == surfaceDesc.vsync
        && swapchain_ != VK_NULL_HANDLE;
}
std::uint64_t VulkanViewportTarget::lastMeshBatchCount() const noexcept
{
    return static_cast<std::uint64_t>(meshBatches_.size());
}

const VulkanViewportFrameProfile& VulkanViewportTarget::lastFrameProfile() const noexcept
{
    return lastFrameProfile_;
}
bool VulkanViewportTarget::renderFrame(
    const RenderFrame& frame,
    VulkanUploadContext& uploads,
    VulkanMeshCache& meshCache,
    VulkanTextureCache& textureCache,
    std::string* errorMessage)
{
    constexpr std::uint64_t kFenceTimeoutNs = 1'000'000'000ULL;
    if (vkWaitForFences(context_.device, 1, &inFlight_, VK_TRUE, kFenceTimeoutNs) != VK_SUCCESS) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to wait for Vulkan viewport frame fence";
        }
        return false;
    }
    gpuProfiler_.collect();
    std::uint32_t imageIndex = 0;
    const auto acquireResult = vkAcquireNextImageKHR(
        context_.device,
        swapchain_,
        UINT64_MAX,
        imageAvailable_,
        VK_NULL_HANDLE,
        &imageIndex);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR || acquireResult == VK_SUBOPTIMAL_KHR) {
        if (errorMessage != nullptr) {
            *errorMessage = "Vulkan viewport swapchain is out of date";
        }
        return false;
    }
    if (acquireResult != VK_SUCCESS) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to acquire Vulkan viewport swapchain image";
        }
        return false;
    }
    if (!recordFrameCommand(imageIndex, frame, uploads, meshCache, textureCache, errorMessage)) {
        return false;
    }
    if (imageIndex >= renderFinished_.size()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Vulkan viewport acquired an image without a presentation semaphore";
        }
        return false;
    }
    const auto renderFinished = renderFinished_[imageIndex];
    (void)vkResetFences(context_.device, 1, &inFlight_);
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &imageAvailable_;
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commandBuffer_;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &renderFinished;
    if (vkQueueSubmit(context_.graphicsQueue, 1, &submit, inFlight_) != VK_SUCCESS) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to submit Vulkan viewport command buffer";
        }
        return false;
    }
    gpuProfiler_.markSubmitted();
    VkPresentInfoKHR present {};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &renderFinished;
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_;
    present.pImageIndices = &imageIndex;
    const auto presentResult = vkQueuePresentKHR(context_.graphicsQueue, &present);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        if (errorMessage != nullptr) {
            *errorMessage = "Vulkan viewport present target is out of date";
        }
        return false;
    }
    if (presentResult != VK_SUCCESS) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to present Vulkan viewport frame";
        }
        return false;
    }
    return true;
}
void VulkanViewportTarget::destroy() noexcept
{
    if (context_.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(context_.device);
    }
    if (inFlight_ != VK_NULL_HANDLE) {
        vkDestroyFence(context_.device, inFlight_, nullptr);
        inFlight_ = VK_NULL_HANDLE;
    }
    for (const auto semaphore : renderFinished_) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(context_.device, semaphore, nullptr);
        }
    }
    renderFinished_.clear();
    if (imageAvailable_ != VK_NULL_HANDLE) {
        vkDestroySemaphore(context_.device, imageAvailable_, nullptr);
        imageAvailable_ = VK_NULL_HANDLE;
    }
    if (commandPool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(context_.device, commandPool_, nullptr);
        commandPool_ = VK_NULL_HANDLE;
        commandBuffer_ = VK_NULL_HANDLE;
    }
    gpuProfiler_.destroy();
    textureDescriptors_.clear();
    descriptorIrradianceKey_ = 0;
    descriptorPrefilteredEnvironmentKey_ = 0;
    descriptorEnvironmentKeyValid_ = false;
    pointShadowCubeReadable_ = false;
    shadowMapValid_ = false;
    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(context_.device, descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
    }
    for (const auto framebuffer : framebuffers_) {
        vkDestroyFramebuffer(context_.device, framebuffer, nullptr);
    }
    framebuffers_.clear();
    if (depthView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(context_.device, depthView_, nullptr);
        depthView_ = VK_NULL_HANDLE;
    }
    if (depthImage_ != VK_NULL_HANDLE) {
        vmaDestroyImage(context_.allocator, depthImage_, depthAllocation_);
        depthImage_ = VK_NULL_HANDLE;
        depthAllocation_ = VK_NULL_HANDLE;
    }
    colorMeshes_.clear();
    meshInstanceBuffer_.destroy();
    meshInstances_.clear();
    meshBatches_.clear();
    frameData_.destroy();
    colorPipeline_.reset();
    shadowPipeline_.reset();
    meshPipeline_.reset();
    for (const auto view : imageViews_) {
        vkDestroyImageView(context_.device, view, nullptr);
    }
    imageViews_.clear();
    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(context_.device, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
    if (surface_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(context_.instance, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }
}
void VulkanViewportTarget::createSurface()
{
#ifdef _WIN32
    VkWin32SurfaceCreateInfoKHR createInfo {};
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.hinstance = GetModuleHandleW(nullptr);
    createInfo.hwnd = static_cast<HWND>(desc_.nativeWindowHandle);
    if (vkCreateWin32SurfaceKHR(context_.instance, &createInfo, nullptr, &surface_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Win32 Vulkan viewport surface");
    }
#else
    throw std::runtime_error("Vulkan viewport surface creation is not implemented for this platform");
#endif
    VkBool32 supported = VK_FALSE;
    if (vkGetPhysicalDeviceSurfaceSupportKHR(context_.physicalDevice, context_.queueFamilyIndex, surface_, &supported) != VK_SUCCESS
        || supported != VK_TRUE) {
        throw std::runtime_error("Selected Vulkan queue family cannot present to the viewport surface");
    }
}
void VulkanViewportTarget::createSwapchain()
{
    VkSurfaceCapabilitiesKHR capabilities {};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context_.physicalDevice, surface_, &capabilities) != VK_SUCCESS) {
        throw std::runtime_error("Failed to query Vulkan surface capabilities");
    }
    std::uint32_t formatCount = 0;
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(context_.physicalDevice, surface_, &formatCount, nullptr) != VK_SUCCESS || formatCount == 0) {
        throw std::runtime_error("Vulkan viewport surface has no supported formats");
    }
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(context_.physicalDevice, surface_, &formatCount, formats.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to query Vulkan surface formats");
    }
    std::uint32_t presentModeCount = 0;
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(context_.physicalDevice, surface_, &presentModeCount, nullptr) != VK_SUCCESS
        || presentModeCount == 0) {
        throw std::runtime_error("Vulkan viewport surface has no present modes");
    }
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(context_.physicalDevice, surface_, &presentModeCount, presentModes.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to query Vulkan present modes");
    }
    surfaceFormat_ = vulkan::chooseSurfaceFormat(formats);
    extent_ = vulkan::chooseExtent(capabilities, desc_.width, desc_.height);
    auto imageCount = capabilities.minImageCount + 1U;
    if (capabilities.maxImageCount > 0U) {
        imageCount = std::min(imageCount, capabilities.maxImageCount);
    }
    VkSwapchainCreateInfoKHR createInfo {};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface_;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat_.format;
    createInfo.imageColorSpace = surfaceFormat_.colorSpace;
    createInfo.imageExtent = extent_;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = vulkan::choosePresentMode(presentModes, desc_.vsync);
    createInfo.clipped = VK_TRUE;
    if (vkCreateSwapchainKHR(context_.device, &createInfo, nullptr, &swapchain_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan viewport swapchain");
    }
    std::uint32_t imageCountOut = 0;
    if (vkGetSwapchainImagesKHR(context_.device, swapchain_, &imageCountOut, nullptr) != VK_SUCCESS || imageCountOut == 0) {
        throw std::runtime_error("Failed to query Vulkan swapchain image count");
    }
    images_.resize(imageCountOut);
    if (vkGetSwapchainImagesKHR(context_.device, swapchain_, &imageCountOut, images_.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to query Vulkan swapchain images");
    }
}
void VulkanViewportTarget::createImageViews()
{
    imageViews_.reserve(images_.size());
    for (const auto image : images_) {
        VkImageViewCreateInfo createInfo {};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = image;
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = surfaceFormat_.format;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.layerCount = 1;
        VkImageView imageView = VK_NULL_HANDLE;
        if (vkCreateImageView(context_.device, &createInfo, nullptr, &imageView) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan viewport image view");
        }
        imageViews_.push_back(imageView);
    }
}
void VulkanViewportTarget::createDepthTarget()
{
    VkImageCreateInfo imageInfo {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = meshPipeline_->depthFormat();
    imageInfo.extent = {extent_.width, extent_.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo allocationInfo {};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (vmaCreateImage(
            context_.allocator,
            &imageInfo,
            &allocationInfo,
            &depthImage_,
            &depthAllocation_,
            nullptr)
        != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan viewport depth image");
    }
    VkImageViewCreateInfo viewInfo {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = depthImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = meshPipeline_->depthFormat();
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(context_.device, &viewInfo, nullptr, &depthView_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan viewport depth image view");
    }
}
void VulkanViewportTarget::createFramebuffers()
{
    framebuffers_.reserve(imageViews_.size());
    for (const auto view : imageViews_) {
        const std::array<VkImageView, 2> attachments {view, depthView_};
        VkFramebufferCreateInfo info {};
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = meshPipeline_->renderPass();
        info.attachmentCount = static_cast<std::uint32_t>(attachments.size());
        info.pAttachments = attachments.data();
        info.width = extent_.width;
        info.height = extent_.height;
        info.layers = 1;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        if (vkCreateFramebuffer(context_.device, &info, nullptr, &framebuffer) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan viewport framebuffer");
        }
        framebuffers_.push_back(framebuffer);
    }
}
void VulkanViewportTarget::createDescriptors()
{
    std::array<VkDescriptorPoolSize, 2> poolSizes {};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 1024;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 10240;
    VkDescriptorPoolCreateInfo info {};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.maxSets = 1024;
    info.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
    info.pPoolSizes = poolSizes.data();
    if (vkCreateDescriptorPool(context_.device, &info, nullptr, &descriptorPool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan viewport descriptor pool");
    }
}
void VulkanViewportTarget::createCommands()
{
    VkCommandPoolCreateInfo poolInfo {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = context_.queueFamilyIndex;
    if (vkCreateCommandPool(context_.device, &poolInfo, nullptr, &commandPool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan viewport command pool");
    }
    VkCommandBufferAllocateInfo allocInfo {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(context_.device, &allocInfo, &commandBuffer_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate Vulkan viewport command buffer");
    }
    beginDebugLabel_ = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
        vkGetDeviceProcAddr(context_.device, "vkCmdBeginDebugUtilsLabelEXT"));
    endDebugLabel_ = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
        vkGetDeviceProcAddr(context_.device, "vkCmdEndDebugUtilsLabelEXT"));
}
void VulkanViewportTarget::createSync()
{
    VkSemaphoreCreateInfo semaphoreInfo {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (vkCreateSemaphore(context_.device, &semaphoreInfo, nullptr, &imageAvailable_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan viewport semaphores");
    }
    renderFinished_.resize(images_.size(), VK_NULL_HANDLE);
    for (auto& semaphore : renderFinished_) {
        if (vkCreateSemaphore(context_.device, &semaphoreInfo, nullptr, &semaphore) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan viewport presentation semaphores");
        }
    }
    VkFenceCreateInfo fenceInfo {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(context_.device, &fenceInfo, nullptr, &inFlight_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan viewport fence");
    }
}
bool VulkanViewportTarget::buildMeshBatches(
    std::span<const RenderMeshDraw> draws,
    std::string* errorMessage)
{
    orderMeshDraws(draws, orderedMeshDraws_);
    meshInstances_.clear();
    meshBatches_.clear();
    meshInstances_.reserve(orderedMeshDraws_.size());
    meshBatches_.reserve(orderedMeshDraws_.size());
    for (const auto* draw : orderedMeshDraws_) {
        if (draw == nullptr) {
            continue;
        }
        VulkanGpuInstance instance;
        std::copy(draw->modelMatrix.values.begin(), draw->modelMatrix.values.end(), instance.model);
        const auto instanceIndex = static_cast<std::uint32_t>(meshInstances_.size());
        meshInstances_.push_back(instance);
        if (!meshBatches_.empty() && canBatch(*meshBatches_.back().draw, *draw)) {
            ++meshBatches_.back().instanceCount;
        } else {
            VulkanMeshDrawBatch batch;
            batch.draw = draw;
            batch.firstInstance = instanceIndex;
            batch.instanceCount = 1U;
            meshBatches_.push_back(batch);
        }
    }
    if (meshInstances_.empty()) {
        meshInstanceBuffer_.destroy();
        return true;
    }
    return meshInstanceBuffer_.writeMapped(
        context_.resources(),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        instanceBytes(meshInstances_),
        errorMessage);
}
bool VulkanViewportTarget::recordFrameCommand(
    std::uint32_t imageIndex,
    const RenderFrame& frame,
    VulkanUploadContext& uploads,
    VulkanMeshCache& meshCache,
    VulkanTextureCache& textureCache,
    std::string* errorMessage)
{
    lastFrameProfile_ = {};
    copyGpuTimes(lastFrameProfile_, gpuProfiler_.lastTimes());
    if (imageIndex >= framebuffers_.size()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Invalid Vulkan viewport image index";
        }
        return false;
    }
    if (!frameData_.update(frame, errorMessage)) {
        return false;
    }
    for (const auto& draw : frame.meshDraws) {
        if (draw.primitive == nullptr || !draw.modelAssetId.isValid()) {
            if (errorMessage != nullptr) {
                *errorMessage = "Vulkan mesh draw has no imported primitive or asset ID";
            }
            return false;
        }
    }
    const auto prepareStart = std::chrono::steady_clock::now();
    if (!buildMeshBatches(frame.meshDraws, errorMessage)) {
        return false;
    }
    if (!prepareMeshBatchResources(frame, uploads, meshCache, textureCache, errorMessage)) {
        return false;
    }
    colorMeshes_.clear();
    colorMeshes_.reserve(frame.colorMeshDraws.size());
    for (const auto& draw : frame.colorMeshDraws) {
        VulkanColorMeshBuffers buffers;
        if (!buffers.upload(context_.resources(), uploads, draw, errorMessage)) {
            return false;
        }
        colorMeshes_.push_back(std::move(buffers));
    }
    lastFrameProfile_.resourcePrepareCpuTimeUs = elapsedUs(prepareStart);
    const auto commandRecordStart = std::chrono::steady_clock::now();
    vkResetCommandBuffer(commandBuffer_, 0);
    VkCommandBufferBeginInfo begin {};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(commandBuffer_, &begin) != VK_SUCCESS) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to begin Vulkan viewport command buffer";
        }
        return false;
    }
    {
        gpuProfiler_.beginFrame(commandBuffer_);
        VulkanScopedLabel frameLabel(
            beginDebugLabel_,
            endDebugLabel_,
            commandBuffer_,
            "ProjectUnity Viewport Frame",
            {0.10F, 0.62F, 0.90F, 1.0F});
        if (!recordShadowPass(frame, uploads, textureCache, errorMessage)) {
            return false;
        }
    std::array<VkClearValue, 2> clears {};
    clears[0].color.float32[0] = frame.clearColor.red;
    clears[0].color.float32[1] = frame.clearColor.green;
    clears[0].color.float32[2] = frame.clearColor.blue;
    clears[0].color.float32[3] = frame.clearColor.alpha;
    clears[1].depthStencil = {1.0F, 0};
    VkRenderPassBeginInfo renderPass {};
    renderPass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPass.renderPass = meshPipeline_->renderPass();
    renderPass.framebuffer = framebuffers_[imageIndex];
    renderPass.renderArea.extent = extent_;
    renderPass.clearValueCount = static_cast<std::uint32_t>(clears.size());
    renderPass.pClearValues = clears.data();
    vkCmdBeginRenderPass(commandBuffer_, &renderPass, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport {};
    viewport.width = static_cast<float>(extent_.width);
    viewport.height = static_cast<float>(extent_.height);
    viewport.maxDepth = 1.0F;
    VkRect2D scissor {};
    scissor.extent = extent_;
    vkCmdSetViewport(commandBuffer_, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer_, 0, 1, &scissor);
    VkPipeline activeMeshPipeline = VK_NULL_HANDLE;
    {
        const auto passStart = std::chrono::steady_clock::now();
        VulkanScopedLabel meshLabel(beginDebugLabel_, endDebugLabel_, commandBuffer_, "ProjectUnity Mesh Pass", {0.12F, 0.75F, 0.38F, 1.0F});
        gpuProfiler_.write(commandBuffer_, VulkanGpuFrameTimestamp::MeshStart, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
        for (const auto& batch : meshBatches_) {
            const auto& draw = *batch.draw;
            const auto doubleSided = draw.material != nullptr && draw.material->doubleSided;
            const auto noCull = doubleSided || draw.flipsWinding;
            const auto drawPipeline = isTransparentMeshDraw(draw)
                ? (noCull ? meshPipeline_->transparentDoubleSidedPipeline() : meshPipeline_->transparentPipeline())
                : (noCull ? meshPipeline_->doubleSidedPipeline() : meshPipeline_->pipeline());
            if (drawPipeline != activeMeshPipeline) {
                vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, drawPipeline);
                activeMeshPipeline = drawPipeline;
            }
            const auto* mesh = batch.mesh;
            const auto descriptor = batch.materialDescriptor;
            if (mesh == nullptr || descriptor == VK_NULL_HANDLE) {
                vkCmdEndRenderPass(commandBuffer_);
                return false;
            }
            VulkanDrawPushConstants push;
            push.modelMatrix = draw.modelMatrix.values;
            if (draw.material != nullptr) {
                push.baseColor = draw.material->baseColor;
                push.pbrFactors = {draw.material->metallicFactor, draw.material->roughnessFactor, draw.material->normalScale, static_cast<float>(draw.material->alphaMode)};
                push.emissiveColor = {draw.material->emissiveColor[0], draw.material->emissiveColor[1], draw.material->emissiveColor[2], draw.material->alphaCutoff};
                push.materialExtras[0] = draw.material->occlusionStrength;
            }
            const VkDeviceSize vertexOffset = 0;
            const VkDeviceSize instanceOffset = static_cast<VkDeviceSize>(batch.firstInstance) * sizeof(VulkanGpuInstance);
            const std::array<VkBuffer, 2> vertexBuffers {mesh->vertices, meshInstanceBuffer_.buffer()};
            const std::array<VkDeviceSize, 2> vertexOffsets {vertexOffset, instanceOffset};
            vkCmdBindVertexBuffers(commandBuffer_, 0, static_cast<std::uint32_t>(vertexBuffers.size()), vertexBuffers.data(), vertexOffsets.data());
            ++lastFrameProfile_.vkBindVertex;
            vkCmdBindIndexBuffer(commandBuffer_, mesh->indices.buffer(), 0, VK_INDEX_TYPE_UINT32);
            ++lastFrameProfile_.vkBindIndex;
            vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipeline_->layout(), 0, 1, &descriptor, 0, nullptr);
            ++lastFrameProfile_.vkBindDescriptors;
            vkCmdPushConstants(commandBuffer_, meshPipeline_->layout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(VulkanDrawPushConstants), &push);
            vkCmdDrawIndexed(commandBuffer_, mesh->indexCount, batch.instanceCount, 0, 0, 0);
            ++lastFrameProfile_.vkDrawIndexed;
            lastFrameProfile_.trianglesSubmitted += static_cast<std::uint64_t>(mesh->indexCount / 3U) * batch.instanceCount;
        }
        gpuProfiler_.write(commandBuffer_, VulkanGpuFrameTimestamp::MeshEnd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
        lastFrameProfile_.meshRecordCpuTimeUs = elapsedUs(passStart);
    }
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, colorPipeline_->pipeline());
    {
        const auto passStart = std::chrono::steady_clock::now();
        VulkanScopedLabel colorLabel(beginDebugLabel_, endDebugLabel_, commandBuffer_, "ProjectUnity Scene Aid Pass", {0.95F, 0.70F, 0.18F, 1.0F});
        gpuProfiler_.write(commandBuffer_, VulkanGpuFrameTimestamp::ColorStart, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
        for (std::size_t index = 0; index < frame.colorMeshDraws.size(); ++index) {
            VulkanColorPushConstants push;
            push.modelViewProjection = frame.colorMeshDraws[index].modelViewProjection.values;
            const VkDeviceSize vertexOffset = 0;
            const auto vertexBuffer = colorMeshes_[index].vertices.buffer();
            vkCmdBindVertexBuffers(commandBuffer_, 0, 1, &vertexBuffer, &vertexOffset);
            ++lastFrameProfile_.vkBindVertex;
            vkCmdBindIndexBuffer(commandBuffer_, colorMeshes_[index].indices.buffer(), 0, VK_INDEX_TYPE_UINT32);
            ++lastFrameProfile_.vkBindIndex;
            vkCmdPushConstants(commandBuffer_, colorPipeline_->layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(VulkanColorPushConstants), &push);
            vkCmdDrawIndexed(commandBuffer_, colorMeshes_[index].indexCount, 1, 0, 0, 0);
            ++lastFrameProfile_.vkDrawIndexed;
            lastFrameProfile_.trianglesSubmitted += colorMeshes_[index].indexCount / 3U;
        }
        gpuProfiler_.write(commandBuffer_, VulkanGpuFrameTimestamp::ColorEnd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
        lastFrameProfile_.colorRecordCpuTimeUs = elapsedUs(passStart);
    }
        vkCmdEndRenderPass(commandBuffer_);
        gpuProfiler_.endFrame(commandBuffer_);
    }
    if (vkEndCommandBuffer(commandBuffer_) != VK_SUCCESS) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to end Vulkan viewport command buffer";
        }
        return false;
    }
    lastFrameProfile_.commandRecordCpuTimeUs = elapsedUs(commandRecordStart);
    return true;
}
} // namespace projectunity::renderer
