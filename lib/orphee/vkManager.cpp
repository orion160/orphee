#include <orphee/OrpheeDefs.hpp>
#include <orphee/vkManager.hpp>

#include <memory>
#include <utility>

#include <spdlog/spdlog.h>
#include <vulkan/vulkan_raii.hpp>

namespace orphee {
vkManager::vkManager(vkInit init)
    : mInit{std::move(init)}, mInstance{createInstance()} {}

vk::raii::SurfaceKHR vkManager::createSurface(
    std::function<vk::raii::SurfaceKHR(const vk::raii::Instance &)> F) {
  return F(mInstance);
}

std::optional<vkDeviceDesc>
vkManager::createDevice(std::function<vkDeviceReqs()> F) {
  const auto r = F();

  for (const auto &physicalDevice : mInstance.enumeratePhysicalDevices()) {
    const auto cx = checkPhysicalDeviceExtensions(physicalDevice, r.extensions);
    if (!cx) {
      continue;
    }

    const auto qMap =
        obtainPhysicalDeviceQueues(physicalDevice, r.queueDescriptors);

    if (qMap.empty()) {
      continue;
    }

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
        queues[tag] =
            std::make_shared<vkQueueDesc>(vkQueueDesc{idx, d.getQueue(idx, 0)});
      }
    }

    return vkDeviceDesc{
        std::make_shared<vk::raii::PhysicalDevice>(physicalDevice),
        std::make_shared<vk::raii::Device>(std::move(d)), queues};
  }

  return {};
}

vk::raii::Instance vkManager::createInstance() {
  const auto vc = checkInstanceVersion(ORPHEE_VK_VERSION,
                                       mContext.enumerateInstanceVersion());
  if (!vc) {
    throw std::runtime_error("Vulkan version mismatch");
  }

  const auto lc = checkInstanceLayers(mInit.layers);
  if (!lc) {
    throw std::runtime_error("Vulkan layers mismatch");
  }

  const auto ec = checkInstanceExtensions(mInit.extensions);
  if (!ec) {
    throw std::runtime_error("Vulkan extensions mismatch");
  }

  vk::ApplicationInfo appInfo{
      mInit.appName.c_str(),
      VK_MAKE_API_VERSION(0, mInit.appVersionMajor, mInit.appVersionMinor, 0),
      ORPHEE_NAME,
      VK_MAKE_API_VERSION(0, ORPHEE_VERSION_MAJOR, ORPHEE_VERSION_MINOR, 0),
      ORPHEE_VK_VERSION};

  std::vector<const char *> layers(mInit.layers.begin(), mInit.layers.end());
  std::vector<const char *> extensions(mInit.extensions.begin(),
                                       mInit.extensions.end());
  vk::InstanceCreateInfo instanceInfo{{}, &appInfo, layers, extensions};

  return mContext.createInstance(instanceInfo);
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

bool vkManager::checkInstanceLayers(const CharSet &layers) {
  spdlog::info("Checking layers...");

  const auto availableLayers{mContext.enumerateInstanceLayerProperties()};
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

bool vkManager::checkInstanceExtensions(const CharSet &extensions) {
  spdlog::info("Checking extensions...");

  const auto availableExtensions{
      mContext.enumerateInstanceExtensionProperties()};
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

vkManager::QSCreate
vkManager::obtainPhysicalDeviceQueues(const vk::PhysicalDevice &device,
                                      const std::vector<QFDesc> &qds) {
  QSCreate qMap;

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
