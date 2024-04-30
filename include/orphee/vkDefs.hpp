#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

namespace orphee {
struct vkQueueDesc;

using CharSet = std::unordered_set<const char *>;
using QS = std::unordered_map<std::string, std::shared_ptr<vkQueueDesc>>;

using FC = vk::StructureChain<
    vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features,
    vk::PhysicalDeviceVulkan12Features, vk::PhysicalDeviceVulkan13Features>;

struct vkSwapChainDesc {
  vk::raii::SwapchainKHR swapchain;
  void onResize() {}
};

struct vkQueueDesc {
  uint32_t fIdx;
  vk::raii::Queue queue;
};

struct QFDesc {
  std::string tag;
  vk::QueueFlags flags;
};

struct vkDeviceReqs {
  CharSet extensions;
  FC features;
  std::vector<QFDesc> queueDescriptors;
};

struct vkDeviceDesc {
  std::shared_ptr<vk::raii::PhysicalDevice> physicalDevice;
  std::shared_ptr<vk::raii::Device> device;
  QS queues;
};
} // namespace orphee
