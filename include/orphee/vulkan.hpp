#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

namespace orphee {
struct Queue;

using CharSet = std::unordered_set<const char *>;
using QS = std::unordered_map<std::string, Queue>;

using FC = vk::StructureChain<
    vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features,
    vk::PhysicalDeviceVulkan12Features, vk::PhysicalDeviceVulkan13Features>;

struct Swapchain {
  vk::raii::SwapchainKHR swapchain;
  std::vector<vk::Image> images;
  std::vector<vk::raii::ImageView> views;
  vk::Format format;
  vk::Extent2D extent;
};

struct Queue {
  vk::raii::Queue queue;
  uint32_t fIdx;
};

struct QueueFamilyDescriptor {
  std::string tag;
  vk::QueueFlags flags;
  // TODO [QUEUE-CREATE]: count
  // can create queues with <tag><id> where id is in range [0, count)
};

struct DeviceRequirements {
  CharSet extensions;
  FC features;
  std::vector<QueueFamilyDescriptor> queueDescriptors;
};

struct Device {
  vk::raii::PhysicalDevice physicalDevice;
  vk::raii::Device device;
  QS queues;
};
} // namespace orphee
