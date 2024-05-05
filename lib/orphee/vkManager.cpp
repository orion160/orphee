#include <orphee/orphee.hpp>
#include <orphee/vkManager.hpp>

#include <spdlog/spdlog.h>

namespace orphee {
vkManager::vkManager(vkInit init)
    : initInfo{std::move(init)}, instance{createInstance()} {}

vkManager::~vkManager() { spdlog::info("Destroying Vulkan manager..."); }

std::optional<Device>
vkManager::createDevice(const DeviceRequirements &r) const {
  for (const auto &physicalDevice : instance.enumeratePhysicalDevices()) {
    // query extensions
    const auto cx = checkPhysicalDeviceExtensions(physicalDevice, r.extensions);
    if (!cx) {
      continue;
    }

    // query queues
    const auto qMap =
        obtainPhysicalDeviceQueues(physicalDevice, r.queueDescriptors);
    if (qMap.empty()) {
      continue;
    }

    // query match!

    std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
    for (const auto &[idx, _] : qMap) {
      std::vector<float> priorities{1.0F};
      queueCreateInfos.push_back({{}, idx, priorities});
    }

    std::vector<const char *> extensions(r.extensions.begin(),
                                         r.extensions.end());

    vk::DeviceCreateInfo deviceInfo{
        {}, queueCreateInfos,
        {}, extensions,
        {}, &r.features.get<vk::PhysicalDeviceFeatures2>()};

    auto d = physicalDevice.createDevice(deviceInfo);

    QS queues;
    for (const auto &[idx, tags] : qMap) {
      for (const auto &tag : tags) {
        // TODO [QUEUE-CREATE]
        queues.emplace(tag, Queue{d.getQueue(idx, 0)});
        spdlog::info("Created queue \"{}\" from queue family {} at index {}",
                     tag, idx, 0);
      }
    }

    return Device{physicalDevice, std::move(d), queues};
  }

  return {};
}

std::optional<uint32_t>
vkManager::findMemoryType(std::span<vk::MemoryType> types, uint32_t typeFilter,
                          vk::MemoryPropertyFlags properties) {
  for (uint32_t i = 0; const auto &t : types) {
    if (((typeFilter & (1U << i)) != 0U) &&
        (t.propertyFlags & properties) == properties) {
      return i;
    }
  }

  return {};
}

vk::raii::Instance vkManager::createInstance() {
  const auto vc = checkInstanceVersion(ORPHEE_VK_VERSION,
                                       context.enumerateInstanceVersion());
  if (!vc) {
    throw std::runtime_error("Vulkan version mismatch");
  }

  const auto lc = checkInstanceLayers(initInfo.layers);
  if (!lc) {
    throw std::runtime_error("Vulkan layers mismatch");
  }

  const auto ec = checkInstanceExtensions(initInfo.extensions);
  if (!ec) {
    throw std::runtime_error("Vulkan extensions mismatch");
  }

  vk::ApplicationInfo appInfo{
      initInfo.appName.c_str(),
      VK_MAKE_API_VERSION(0, initInfo.appVersionMajor, initInfo.appVersionMinor,
                          0),
      ORPHEE_NAME,
      VK_MAKE_API_VERSION(0, ORPHEE_VERSION_MAJOR, ORPHEE_VERSION_MINOR, 0),
      ORPHEE_VK_VERSION};

  std::vector<const char *> layers(initInfo.layers.begin(),
                                   initInfo.layers.end());
  std::vector<const char *> extensions(initInfo.extensions.begin(),
                                       initInfo.extensions.end());
  vk::InstanceCreateInfo instanceInfo{{}, &appInfo, layers, extensions};

  return context.createInstance(instanceInfo);
}

bool vkManager::checkInstanceVersion(uint32_t target, uint32_t instance) {
  spdlog::info("Checking Vulkan version...");

  const auto targetMajor = VK_VERSION_MAJOR(target);
  const auto instanceMajor = VK_VERSION_MAJOR(instance);

  if (targetMajor < instanceMajor) {
    spdlog::error("Vulkan major version mismatch, it might be incompatible");
    return false;
  }

  const auto targetMinor = VK_VERSION_MINOR(target);
  const auto instanceMinor = VK_VERSION_MINOR(instance);
  if (targetMinor > instanceMinor) {
    spdlog::error("Vulkan Minor version mismatch");
    return false;
  }

  spdlog::info("Intance vk {}.{} is compatible with requested vk{}.{}",
               instanceMajor, instanceMinor, targetMajor, targetMinor);
  return true;
}

bool vkManager::checkInstanceLayers(const CharSet &layers) const {
  spdlog::info("Checking layers...");

  const auto availableLayers{context.enumerateInstanceLayerProperties()};
  spdlog::info("Found {} available layers", availableLayers.size());

  std::unordered_set<std::string> names;
  for (const auto &l : availableLayers) {
    names.insert(l.layerName);
  }

  bool foundAll = true;

  for (const auto &l : layers) {
    if (names.find(l) == names.end()) {
      spdlog::error("Layer {} is not available", l);
      foundAll = false;
    } else {
      spdlog::info("Requested {} layer found", l);
    }
  }

  return foundAll;
}

bool vkManager::checkInstanceExtensions(const CharSet &extensions) const {
  spdlog::info("Checking extensions...");

  const auto availableExtensions{
      context.enumerateInstanceExtensionProperties()};
  spdlog::info("Found {} available extensions", availableExtensions.size());

  std::unordered_set<std::string> names;
  for (const auto &ex : availableExtensions) {
    names.insert(ex.extensionName);
  }

  bool foundAll = true;

  for (const auto &ext : extensions) {
    if (names.find(ext) == names.end()) {
      spdlog::error("Extension {} is not available", ext);
      foundAll = false;
    } else {
      spdlog::info("Requested {} extension found", ext);
    }
  }

  return foundAll;
}

bool vkManager::checkPhysicalDeviceExtensions(
    const vk::raii::PhysicalDevice &device, const CharSet &extensions) {
  spdlog::info("Checking physical device extensions...");

  const auto availableExtensions{device.enumerateDeviceExtensionProperties()};
  spdlog::info("Found {} available extensions", availableExtensions.size());

  std::unordered_set<std::string> names;
  for (const auto &ex : availableExtensions) {
    names.insert(ex.extensionName);
  }

  bool foundAll = true;

  for (const auto &ext : extensions) {
    if (names.find(ext) == names.end()) {
      spdlog::error("Extension {} is not available", ext);
      foundAll = false;
    } else {
      spdlog::info("Requested {} extension found", ext);
    }
  }

  return foundAll;
}

vkManager::QueueCreate vkManager::obtainPhysicalDeviceQueues(
    const vk::PhysicalDevice &device,
    std::span<const QueueFamilyDescriptor> qds) {
  QueueCreate qMap;

  const auto qf = device.getQueueFamilyProperties();
  spdlog::info("Found {} queue families", qf.size());

  for (const auto &q : qds) {
    for (uint32_t idx = 0; const auto &qProp : qf) {
      if (q.flags | qProp.queueFlags) {
        qMap[idx].push_back(q.tag);
      }
      ++idx;
    }
  }

  return qMap;
}
} // namespace orphee
