#include "VulkanSupport.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>

namespace projectunity::renderer::vulkan {

std::uint32_t major(std::uint32_t version)
{
    return VK_VERSION_MAJOR(version);
}

std::uint32_t minor(std::uint32_t version)
{
    return VK_VERSION_MINOR(version);
}

std::uint32_t patch(std::uint32_t version)
{
    return VK_VERSION_PATCH(version);
}

bool hasLayer(std::string_view name)
{
    std::uint32_t count = 0;
    if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS || count == 0) {
        return false;
    }

    std::vector<VkLayerProperties> layers(count);
    if (vkEnumerateInstanceLayerProperties(&count, layers.data()) != VK_SUCCESS) {
        return false;
    }
    return std::any_of(layers.begin(), layers.end(), [name](const auto& layer) {
        return name == layer.layerName;
    });
}

bool hasInstanceExtension(std::string_view name)
{
    std::uint32_t count = 0;
    if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS || count == 0) {
        return false;
    }

    std::vector<VkExtensionProperties> extensions(count);
    if (vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data()) != VK_SUCCESS) {
        return false;
    }
    return std::any_of(extensions.begin(), extensions.end(), [name](const auto& extension) {
        return name == extension.extensionName;
    });
}

bool hasDeviceExtension(VkPhysicalDevice device, std::string_view name)
{
    std::uint32_t count = 0;
    if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr) != VK_SUCCESS || count == 0) {
        return false;
    }

    std::vector<VkExtensionProperties> extensions(count);
    if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data()) != VK_SUCCESS) {
        return false;
    }
    return std::any_of(extensions.begin(), extensions.end(), [name](const auto& extension) {
        return name == extension.extensionName;
    });
}

std::vector<const char*> requiredInstanceExtensions()
{
    std::vector<const char*> extensions;
    extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
#ifdef _WIN32
    extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#endif
    return extensions;
}

void requireInstanceExtensions(const std::vector<const char*>& extensions)
{
    for (const auto* extension : extensions) {
        if (!hasInstanceExtension(extension)) {
            throw std::runtime_error(std::string("Missing required Vulkan instance extension: ") + extension);
        }
    }
}

std::uint32_t findGraphicsQueueFamily(VkPhysicalDevice device)
{
    std::uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, families.data());

    for (std::uint32_t index = 0; index < familyCount; ++index) {
        if ((families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0U) {
            return index;
        }
    }
    throw std::runtime_error("No Vulkan graphics queue family was found");
}

VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats)
{
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    return formats.front();
}

VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes, bool vsync)
{
    if (vsync) {
        return VK_PRESENT_MODE_FIFO_KHR;
    }
    for (const auto mode : modes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return mode;
        }
    }
    for (const auto mode : modes) {
        if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
            return mode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities, std::uint32_t width, std::uint32_t height)
{
    if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    return {
        std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
        std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height),
    };
}

VkPhysicalDevice choosePhysicalDevice(VkInstance instance)
{
    std::uint32_t deviceCount = 0;
    if (vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr) != VK_SUCCESS || deviceCount == 0) {
        throw std::runtime_error("No Vulkan physical device is available");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    if (vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to enumerate Vulkan physical devices");
    }

    VkPhysicalDevice selected = VK_NULL_HANDLE;
    for (const auto device : devices) {
        if (!hasDeviceExtension(device, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            continue;
        }

        VkPhysicalDeviceProperties properties {};
        vkGetPhysicalDeviceProperties(device, &properties);
        if (selected == VK_NULL_HANDLE || properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            selected = device;
            if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                break;
            }
        }
    }
    if (selected == VK_NULL_HANDLE) {
        throw std::runtime_error("No Vulkan physical device with swapchain support is available");
    }
    return selected;
}

} // namespace projectunity::renderer::vulkan
