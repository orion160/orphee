#pragma once

#include <functional>
#include <optional>

#include <orphee/vkDefs.hpp>

namespace orphee {
struct vkInit {
  std::string appName;
  uint32_t appVersionMajor;
  uint32_t appVersionMinor;
  CharSet layers;
  CharSet extensions;
};

class vkManager {
public:
  [[nodiscard]] vkManager(vkInit init);

  [[nodiscard]] vk::raii::SurfaceKHR createSurface(
      std::function<vk::raii::SurfaceKHR(const vk::raii::Instance &)> F);

  [[nodiscard]] std::optional<vkDeviceDesc>
  createDevice(std::function<vkDeviceReqs()> F);

private:
  using QSCreate = std::unordered_map<uint32_t, std::vector<std::string>>;

  [[nodiscard]] vk::raii::Instance createInstance();

  [[nodiscard]] static bool checkInstanceVersion(uint32_t target,
                                                 uint32_t instance);

  [[nodiscard]] bool checkInstanceLayers(const CharSet &layers);

  [[nodiscard]] bool checkInstanceExtensions(const CharSet &extensions);

  [[nodiscard]] static bool
  checkPhysicalDeviceExtensions(const vk::raii::PhysicalDevice &device,
                                const CharSet &extensions);

  [[nodiscard]] static QSCreate
  obtainPhysicalDeviceQueues(const vk::PhysicalDevice &device,
                             const std::vector<QFDesc> &qds);

  vkInit mInit;
  vk::raii::Context mContext;
  vk::raii::Instance mInstance;
};
} // namespace orphee
