#pragma once

#include <orphee/vkManager.hpp>
#include <orphee/vulkan.hpp>

namespace orphee {
constexpr auto ORPHEE_NAME = "Orphee";

constexpr uint32_t ORPHEE_VERSION_MAJOR = 0;

constexpr uint32_t ORPHEE_VERSION_MINOR = 0;

constexpr uint32_t ORPHEE_VK_VERSION = VK_API_VERSION_1_3;

const std::vector<const char *> ORPHEE_REQUIRED_VK_INSTANCE_LAYERS{
    "VK_LAYER_KHRONOS_validation",
};

const std::vector<const char *> ORPHEE_REQUIRED_VK_INSTANCE_EXTENSIONS{};

const std::vector<const char *>
    ORPHEE_REQUIRED_VK_INSTANCE_WINDOWING_EXTENSIONS{
        VK_KHR_SURFACE_EXTENSION_NAME,
        "VK_KHR_win32_surface",
    };

const std::vector<const char *> ORPHEE_REQUIRED_VK_DEVICE_EXTENSIONS{
    VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
    VK_KHR_MAINTENANCE_1_EXTENSION_NAME,
    VK_KHR_MAINTENANCE_2_EXTENSION_NAME,
    VK_KHR_MAINTENANCE_3_EXTENSION_NAME,
    VK_KHR_MAINTENANCE_4_EXTENSION_NAME,
    VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME,
    VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
    VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,
    VK_KHR_BIND_MEMORY_2_EXTENSION_NAME,
    VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
    VK_EXT_MEMORY_BUDGET_EXTENSION_NAME,
    VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME,
    VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
    VK_KHR_COPY_COMMANDS_2_EXTENSION_NAME,
};

const std::vector<const char *> ORPHEE_REQUIRED_VK_DEVICE_WINDOWING_EXTENSIONS{
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

static vk::PhysicalDeviceFeatures2 getFeatures2() { return {}; }

static vk::PhysicalDeviceVulkan11Features getFeaturesVK11() { return {}; }

static vk::PhysicalDeviceVulkan12Features getFeaturesVK12() {
  vk::PhysicalDeviceVulkan12Features f12{};
  f12.setTimelineSemaphore(vk::True);
  f12.setBufferDeviceAddress(vk::True);
  return f12;
}

static vk::PhysicalDeviceVulkan13Features getFeaturesVK13() {
  vk::PhysicalDeviceVulkan13Features f13{};
  f13.setDynamicRendering(vk::True);
  f13.setSynchronization2(vk::True);
  f13.setMaintenance4(vk::True);
  return f13;
}

const vk::StructureChain<
    vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features,
    vk::PhysicalDeviceVulkan12Features, vk::PhysicalDeviceVulkan13Features,
    vk::PhysicalDeviceMemoryPriorityFeaturesEXT>
    ORPHEE_REQUIRED_VK_DEVICE_FEATURES = {
        getFeatures2(),
        getFeaturesVK11(),
        getFeaturesVK12(),
        getFeaturesVK13(),
        vk::PhysicalDeviceMemoryPriorityFeaturesEXT{vk::True},
};
} // namespace orphee
