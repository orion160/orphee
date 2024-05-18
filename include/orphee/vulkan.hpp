#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <vk_mem_alloc.h>

namespace orphee {
struct vmaBuffer;
struct vmaImage;

struct QueueFamily;
struct Queue;
struct Device;
struct Swapchain;

struct vmaBuffer {
  friend struct Device;

  vmaBuffer(std::nullptr_t) {}

  vmaBuffer(const vmaBuffer &) = default;

  vmaBuffer(vmaBuffer &&other) noexcept
      : h{other.h}, size{other.size}, allocator{other.allocator},
        allocation{other.allocation}, allocationInfo{other.allocationInfo} {
    other.h = nullptr;
    other.size = 0;
    other.allocator = nullptr;
    other.allocation = nullptr;
    other.allocationInfo = {};
  }

  vmaBuffer &operator=(const vmaBuffer &) = default;

  vmaBuffer &operator=(vmaBuffer &&other) noexcept {
    h = other.h;
    size = other.size;
    allocator = other.allocator;
    allocation = other.allocation;
    allocationInfo = other.allocationInfo;

    other.h = nullptr;
    other.size = 0;
    other.allocator = nullptr;
    other.allocation = nullptr;
    other.allocationInfo = {};

    return *this;
  }

  ~vmaBuffer() {
    if (allocation != nullptr) {
      clear();
    }
  }

  void clear() const {
    if (allocation != nullptr) {
      vmaDestroyBuffer(allocator, h, allocation);
    }
  }

  vk::Buffer h;
  size_t size{};
  VmaAllocator allocator{};
  VmaAllocation allocation{};
  VmaAllocationInfo allocationInfo{};

private:
  vmaBuffer() = default;
};

struct vmaImage {
  friend struct Device;

  vmaImage(std::nullptr_t) {}

  vmaImage(const vmaImage &) = default;

  vmaImage(vmaImage &&other) noexcept
      : h{other.h}, extent{other.extent}, allocator{other.allocator},
        allocation{other.allocation}, allocationInfo{other.allocationInfo} {
    other.h = nullptr;
    other.extent = vk::Extent2D{};
    other.allocator = nullptr;
    other.allocation = nullptr;
    other.allocationInfo = {};
  }

  vmaImage &operator=(const vmaImage &) = default;

  vmaImage &operator=(vmaImage &&other) noexcept {
    h = other.h;
    extent = other.extent;
    allocator = other.allocator;
    allocation = other.allocation;
    allocationInfo = other.allocationInfo;

    other.h = nullptr;
    other.extent = vk::Extent2D{};
    other.allocator = nullptr;
    other.allocation = nullptr;
    other.allocationInfo = {};

    return *this;
  }

  ~vmaImage() {
    if (allocation != nullptr) {
      clear();
    }
  }

  void clear() const {
    if (allocation != nullptr) {
      vmaDestroyImage(allocator, h, allocation);
    }
  }

  vk::Image h;
  vk::Extent2D extent;
  VmaAllocator allocator{};
  VmaAllocation allocation{};
  VmaAllocationInfo allocationInfo{};

private:
  vmaImage() = default;
};

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

  [[nodiscard]] vmaBuffer
  createBuffer(const vk::BufferCreateInfo &info,
               const VmaAllocationCreateInfo &allocInfo) const {
    vmaBuffer b;

    const auto vbR = vmaCreateBuffer(
        this->allocator, reinterpret_cast<const VkBufferCreateInfo *>(&info),
        &allocInfo, reinterpret_cast<VkBuffer *>(&b.h), &b.allocation,
        &b.allocationInfo);

    if (vbR != VK_SUCCESS) {
      throw std::runtime_error("Failed to create buffer");
    }

    b.allocator = this->allocator;

    return b;
  }

  [[nodiscard]] vmaImage
  createImage(const vk::ImageCreateInfo &info,
              const VmaAllocationCreateInfo &allocInfo) const {
    vmaImage img;

    const auto viR = vmaCreateImage(
        this->allocator, reinterpret_cast<const VkImageCreateInfo *>(&info),
        &allocInfo, reinterpret_cast<VkImage *>(&img.h), &img.allocation,
        &img.allocationInfo);

    if (viR != VK_SUCCESS) {
      throw std::runtime_error("Failed to create image");
    }

    img.allocator = this->allocator;
    img.extent = vk::Extent2D{info.extent.width, info.extent.height};

    return img;
  }

  void destroyImage(const vmaImage &img) const {
    vmaDestroyImage(this->allocator, img.h, img.allocation);
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
