#pragma once

#include <optional>
#include <span>
#include <string>

#include <orphee/vulkan.hpp>

namespace orphee {
struct Settings {
  bool windowing = false;
};

struct Meta {
  std::string appName;
  uint32_t appVersionMajor;
  uint32_t appVersionMinor;
};

struct QueueFamilyRequirements {
  std::string tag;
  uint32_t count;
  vk::QueueFlags capabilities;
  std::optional<vk::SurfaceKHR> surface;
};

struct vkManager {
public:
  vkManager(Settings s = {}, Meta m = {});

  vkManager(const vkManager &) = delete;

  vkManager(vkManager &&) noexcept = default;

  vkManager &operator=(const vkManager &) = delete;

  vkManager &operator=(vkManager &&) = default;

  ~vkManager();

  [[nodiscard]] std::optional<Device>
  createDevice(const QueueFamilyRequirements &reqs) const;

  Settings settings;

  Meta meta;

  vk::raii::Context context;

  vk::raii::Instance instance;

private:
  vk::raii::Instance createInstance();

  [[nodiscard]] static bool checkInstanceVersion(uint32_t target,
                                                 uint32_t instance);

  [[nodiscard]] bool checkInstanceLayers(std::span<const char *> layers) const;

  [[nodiscard]] bool
  checkInstanceExtensions(std::span<const char *> extensions) const;

  [[nodiscard]] static bool
  checkPhysicalDeviceExtensions(const vk::PhysicalDevice &device,
                                std::span<const char *> extensions);

  [[nodiscard]] static std::optional<uint32_t>
  obtainQueueFamilies(const vk::PhysicalDevice &device,
                      const QueueFamilyRequirements &r);
};
} // namespace orphee
