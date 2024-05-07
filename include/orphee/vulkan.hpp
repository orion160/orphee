#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <vk_mem_alloc.h>

namespace orphee {
struct QueueFamily;
struct Queue;
struct Device;
struct Swapchain;

struct QueueFamily {
  std::vector<Queue *> queues;
  vk::QueueFlags capabilities;
  bool present;
};

struct Queue {
  vk::raii::Queue h{nullptr};
  uint32_t fIdx;
  QueueFamily *queueFamily;
};

struct Device {
  Device() = default;

  Device(vk::raii::PhysicalDevice &physical, vk::raii::Device &device,
         std::unordered_map<std::string, std::unique_ptr<QueueFamily>> &qf,
         std::unordered_map<std::string, std::unique_ptr<Queue>> &qs,
         VmaAllocator allocator)
      : physical{std::move(physical)}, h{std::move(device)},
        queueFamilies{std::move(qf)}, queues{std::move(qs)},
        allocator{allocator} {}

  Device(const Device &other) = delete;

  Device(Device &&other) noexcept
      : physical{std::move(other.physical)}, h{std::move(other.h)},
        queueFamilies{std::move(other.queueFamilies)},
        queues{std::move(other.queues)} {
    std::swap(allocator, other.allocator);
  };

  Device &operator=(const Device &other) = delete;

  Device &operator=(Device &&other) noexcept {
    physical = std::move(other.physical);
    h = std::move(other.h);
    queueFamilies = std::move(other.queueFamilies);
    queues = std::move(other.queues);
    std::swap(allocator, other.allocator);

    return *this;
  }

  ~Device() {
    if (allocator != nullptr) {
      vmaDestroyAllocator(allocator);
    }
  }

  vk::raii::PhysicalDevice physical{nullptr};
  vk::raii::Device h{nullptr};
  std::unordered_map<std::string, std::unique_ptr<QueueFamily>> queueFamilies;
  std::unordered_map<std::string, std::unique_ptr<Queue>> queues;
  VmaAllocator allocator{};
};

struct Swapchain {
  Swapchain() = default;

  Swapchain(vk::raii::SwapchainKHR &swapchain, std::vector<vk::Image> &images,
            std::vector<vk::raii::ImageView> &views, vk::Format format,
            vk::Extent2D extent)
      : h{std::move(swapchain)}, images{std::move(images)},
        views{std::move(views)}, format{format}, extent{extent} {}

  Swapchain(const Swapchain &) = delete;

  Swapchain(Swapchain &&) noexcept = default;

  Swapchain &operator=(const Swapchain &) = delete;

  Swapchain &operator=(Swapchain &&) noexcept = default;

  ~Swapchain() = default;

  vk::raii::SwapchainKHR h{nullptr};
  std::vector<vk::Image> images;
  std::vector<vk::raii::ImageView> views;
  vk::Format format{};
  vk::Extent2D extent;
};
} // namespace orphee
