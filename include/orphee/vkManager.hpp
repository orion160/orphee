#pragma once

#include <optional>

#include <orphee/vulkan.hpp>

namespace orphee {
struct vkInit {
  std::string appName;
  uint32_t appVersionMajor;
  uint32_t appVersionMinor;
  CharSet layers;
  CharSet extensions;
};

/*
SELECTOR API


declaration
template <typename S>
concept Selector = requires(S f, "needed-info-arg") {
  { f("needed-info-arg") } -> std::same_as<"?">;
};

implementation

R createXXX(Selector auto && s) {}
*/

class vkManager {
public:
  vkManager(vkInit init);
  vkManager(const vkManager &) = delete;
  vkManager(vkManager &&) noexcept = default;
  vkManager &operator=(const vkManager &) = delete;
  vkManager &operator=(vkManager &&) = default;
  ~vkManager();

  // TODO: refactor into selector API
  [[nodiscard]] std::optional<Device>
  createDevice(const DeviceRequirements &r) const;

  [[nodiscard]] static std::optional<uint32_t>
  findMemoryType(std::span<vk::MemoryType> types, uint32_t typeFilter,
                 vk::MemoryPropertyFlags properties);

  vkInit initInfo;
  vk::raii::Context context;
  vk::raii::Instance instance;

private:
  using QueueCreate = std::unordered_map<uint32_t, std::vector<std::string>>;

  [[nodiscard]] vk::raii::Instance createInstance();

  [[nodiscard]] static bool checkInstanceVersion(uint32_t target,
                                                 uint32_t instance);

  [[nodiscard]] bool checkInstanceLayers(const CharSet &layers) const;

  [[nodiscard]] bool checkInstanceExtensions(const CharSet &extensions) const;

  [[nodiscard]] static bool
  checkPhysicalDeviceExtensions(const vk::raii::PhysicalDevice &device,
                                const CharSet &extensions);

  [[nodiscard]] static QueueCreate
  obtainPhysicalDeviceQueues(const vk::PhysicalDevice &device,
                             std::span<const QueueFamilyDescriptor> qds);
};
} // namespace orphee
