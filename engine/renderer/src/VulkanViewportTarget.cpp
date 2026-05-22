#include "VulkanViewportTarget.hpp"

#include "VulkanSupport.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace projectunity::renderer {

VulkanViewportTarget::VulkanViewportTarget(VulkanViewportContext context, ViewportRenderSurfaceDesc desc)
    : context_(context)
    , desc_(desc)
{
    try {
        createSurface();
        createSwapchain();
        createImageViews();
        meshPipeline_ = std::make_unique<VulkanMeshPipeline>(context_.resources(), surfaceFormat_.format);
        colorPipeline_ = std::make_unique<VulkanColorPipeline>(context_.resources(), meshPipeline_->renderPass());
        createDepthTarget();
        createFramebuffers();
        createDescriptors();
        createCommands();
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
    submit.pSignalSemaphores = &renderFinished_;

    if (vkQueueSubmit(context_.graphicsQueue, 1, &submit, inFlight_) != VK_SUCCESS) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to submit Vulkan viewport command buffer";
        }
        return false;
    }

    VkPresentInfoKHR present {};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &renderFinished_;
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
    if (renderFinished_ != VK_NULL_HANDLE) {
        vkDestroySemaphore(context_.device, renderFinished_, nullptr);
        renderFinished_ = VK_NULL_HANDLE;
    }
    if (imageAvailable_ != VK_NULL_HANDLE) {
        vkDestroySemaphore(context_.device, imageAvailable_, nullptr);
        imageAvailable_ = VK_NULL_HANDLE;
    }
    if (commandPool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(context_.device, commandPool_, nullptr);
        commandPool_ = VK_NULL_HANDLE;
        commandBuffer_ = VK_NULL_HANDLE;
    }
    textureDescriptors_.clear();
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
    colorPipeline_.reset();
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
    VkDescriptorPoolSize poolSize {};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1024;
    VkDescriptorPoolCreateInfo info {};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.maxSets = poolSize.descriptorCount;
    info.poolSizeCount = 1;
    info.pPoolSizes = &poolSize;
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
}

void VulkanViewportTarget::createSync()
{
    VkSemaphoreCreateInfo semaphoreInfo {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (vkCreateSemaphore(context_.device, &semaphoreInfo, nullptr, &imageAvailable_) != VK_SUCCESS
        || vkCreateSemaphore(context_.device, &semaphoreInfo, nullptr, &renderFinished_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan viewport semaphores");
    }

    VkFenceCreateInfo fenceInfo {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(context_.device, &fenceInfo, nullptr, &inFlight_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan viewport fence");
    }
}

VkDescriptorSet VulkanViewportTarget::textureDescriptor(const VulkanTextureHandle& texture, std::string* errorMessage)
{
    if (const auto existing = textureDescriptors_.find(texture.key); existing != textureDescriptors_.end()) {
        return existing->second;
    }

    VkDescriptorSetAllocateInfo allocInfo {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = 1;
    const auto layout = meshPipeline_->textureLayout();
    allocInfo.pSetLayouts = &layout;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(context_.device, &allocInfo, &descriptor) != VK_SUCCESS) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to allocate Vulkan texture descriptor set";
        }
        return VK_NULL_HANDLE;
    }

    VkDescriptorImageInfo imageInfo {};
    imageInfo.sampler = texture.sampler;
    imageInfo.imageView = texture.view;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptor;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(context_.device, 1, &write, 0, nullptr);
    textureDescriptors_.emplace(texture.key, descriptor);
    return descriptor;
}

bool VulkanViewportTarget::recordFrameCommand(
    std::uint32_t imageIndex,
    const RenderFrame& frame,
    VulkanUploadContext& uploads,
    VulkanMeshCache& meshCache,
    VulkanTextureCache& textureCache,
    std::string* errorMessage)
{
    if (imageIndex >= framebuffers_.size()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Invalid Vulkan viewport image index";
        }
        return false;
    }

    for (const auto& draw : frame.meshDraws) {
        if (draw.primitive == nullptr || !draw.modelAssetId.isValid()) {
            if (errorMessage != nullptr) {
                *errorMessage = "Vulkan mesh draw has no imported primitive or asset ID";
            }
            return false;
        }
        const VulkanMeshKey meshKey {draw.modelAssetId.value(), draw.primitiveIndex};
        if (meshCache.ensureUploaded(context_.resources(), uploads, meshKey, *draw.primitive, errorMessage) == nullptr) {
            return false;
        }
        const auto* texture = textureCache.ensureUploaded(context_.resources(), uploads, draw.baseColorTexture, errorMessage);
        if (texture == nullptr || textureDescriptor(*texture, errorMessage) == VK_NULL_HANDLE) {
            return false;
        }
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
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipeline_->pipeline());
    VkViewport viewport {};
    viewport.width = static_cast<float>(extent_.width);
    viewport.height = static_cast<float>(extent_.height);
    viewport.maxDepth = 1.0F;
    VkRect2D scissor {};
    scissor.extent = extent_;
    vkCmdSetViewport(commandBuffer_, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer_, 0, 1, &scissor);

    for (const auto& draw : frame.meshDraws) {
        const VulkanMeshKey meshKey {draw.modelAssetId.value(), draw.primitiveIndex};
        const auto* mesh = meshCache.ensureUploaded(context_.resources(), uploads, meshKey, *draw.primitive, errorMessage);
        const auto* texture = textureCache.ensureUploaded(context_.resources(), uploads, draw.baseColorTexture, errorMessage);
        if (mesh == nullptr || texture == nullptr) {
            vkCmdEndRenderPass(commandBuffer_);
            return false;
        }
        const auto descriptor = textureDescriptor(*texture, errorMessage);
        if (descriptor == VK_NULL_HANDLE) {
            vkCmdEndRenderPass(commandBuffer_);
            return false;
        }

        VulkanDrawPushConstants push;
        push.modelViewProjection = draw.modelViewProjection.values;
        if (draw.material != nullptr) {
            push.baseColor = draw.material->baseColor;
            push.pbrFactors = {draw.material->metallicFactor, draw.material->roughnessFactor, 0.0F, 0.0F};
            push.emissiveColor = {
                draw.material->emissiveColor[0],
                draw.material->emissiveColor[1],
                draw.material->emissiveColor[2],
                0.0F,
            };
        }
        const VkDeviceSize vertexOffset = 0;
        const auto vertexBuffer = mesh->vertices.buffer();
        vkCmdBindVertexBuffers(commandBuffer_, 0, 1, &vertexBuffer, &vertexOffset);
        vkCmdBindIndexBuffer(commandBuffer_, mesh->indices.buffer(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdBindDescriptorSets(
            commandBuffer_,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            meshPipeline_->layout(),
            0,
            1,
            &descriptor,
            0,
            nullptr);
        vkCmdPushConstants(
            commandBuffer_,
            meshPipeline_->layout(),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(VulkanDrawPushConstants),
            &push);
        vkCmdDrawIndexed(commandBuffer_, mesh->indexCount, 1, 0, 0, 0);
    }
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, colorPipeline_->pipeline());
    for (std::size_t index = 0; index < frame.colorMeshDraws.size(); ++index) {
        VulkanColorPushConstants push;
        push.modelViewProjection = frame.colorMeshDraws[index].modelViewProjection.values;
        const VkDeviceSize vertexOffset = 0;
        const auto vertexBuffer = colorMeshes_[index].vertices.buffer();
        vkCmdBindVertexBuffers(commandBuffer_, 0, 1, &vertexBuffer, &vertexOffset);
        vkCmdBindIndexBuffer(commandBuffer_, colorMeshes_[index].indices.buffer(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdPushConstants(
            commandBuffer_,
            colorPipeline_->layout(),
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            sizeof(VulkanColorPushConstants),
            &push);
        vkCmdDrawIndexed(commandBuffer_, colorMeshes_[index].indexCount, 1, 0, 0, 0);
    }
    vkCmdEndRenderPass(commandBuffer_);
    if (vkEndCommandBuffer(commandBuffer_) != VK_SUCCESS) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to end Vulkan viewport command buffer";
        }
        return false;
    }
    return true;
}

} // namespace projectunity::renderer
