#include <memory>
#include <unordered_set>

#include <spdlog/spdlog.h>

#include <orphee/orphee.hpp>
#include <orphee/vkManager.hpp>

namespace orphee {
vkManager::vkManager(Settings s, Meta m)
    : settings{s}, meta{std::move(m)}, instance{createInstance()} {}

vkManager::~vkManager() { spdlog::info("Destroying Vulkan manager..."); }

std::optional<Device>
vkManager::createDevice(const QueueFamilyRequirements &reqs) const {
  for (auto &physicalDevice : instance.enumeratePhysicalDevices()) {
    if (reqs.surface.has_value() && !settings.windowing) {
      spdlog::error("Windowing is required");
      return {};
    }

    std::vector<const char *> extensions;
    if (reqs.surface.has_value()) {
      for (const auto &x : ORPHEE_REQUIRED_VK_DEVICE_WINDOWING_EXTENSIONS) {
        extensions.push_back(x);
      }
    }

    for (const auto &x : ORPHEE_REQUIRED_VK_DEVICE_EXTENSIONS) {
      extensions.push_back(x);
    }

    // check extensions
    const auto cx = checkPhysicalDeviceExtensions(physicalDevice, extensions);
    if (!cx) {
      continue;
    }

    auto qfR = obtainQueueFamilies(physicalDevice, reqs);
    if (!qfR) {
      continue;
    }
    const auto fIdx = *qfR;

    std::vector<float> queuePriorities;
    for (uint32_t i = 0; i < reqs.count; ++i) {
      queuePriorities.emplace_back(1.0F);
    }

    vk::DeviceQueueCreateInfo queueInfo{{}, fIdx, queuePriorities};

    vk::DeviceCreateInfo deviceInfo{
        {},
        queueInfo,
        {},
        extensions,
        {},
        &ORPHEE_REQUIRED_VK_DEVICE_FEATURES.get<vk::PhysicalDeviceFeatures2>()};

    auto d = physicalDevice.createDevice(deviceInfo);

    std::unordered_map<std::string, std::unique_ptr<QueueFamily>> qfs;
    std::unordered_map<std::string, std::unique_ptr<Queue>> qs;

    auto qf = std::make_unique<QueueFamily>();
    qf->capabilities = reqs.capabilities;
    qf->present = reqs.surface.has_value();

    for (uint32_t i = 0; i < reqs.count; ++i) {
      auto q = std::make_unique<Queue>();
      q->h = d.getQueue(fIdx, i);
      q->fIdx = fIdx;
      q->queueFamily = qf.get();
      qf->queues.push_back(q.get());
      qs.insert({reqs.tag + std::to_string(i), std::move(q)});
    }

    qfs.insert({reqs.tag, std::move(qf)});

    VmaVulkanFunctions vulkanFunctions{};
    vulkanFunctions.vkGetInstanceProcAddr =
        instance.getDispatcher()->vkGetInstanceProcAddr;
    vulkanFunctions.vkGetDeviceProcAddr =
        d.getDispatcher()->vkGetDeviceProcAddr;
    vulkanFunctions.vkGetPhysicalDeviceProperties =
        physicalDevice.getDispatcher()->vkGetPhysicalDeviceProperties;
    vulkanFunctions.vkGetPhysicalDeviceMemoryProperties =
        physicalDevice.getDispatcher()->vkGetPhysicalDeviceMemoryProperties;
    vulkanFunctions.vkAllocateMemory = d.getDispatcher()->vkAllocateMemory;
    vulkanFunctions.vkFreeMemory = d.getDispatcher()->vkFreeMemory;
    vulkanFunctions.vkMapMemory = d.getDispatcher()->vkMapMemory;
    vulkanFunctions.vkUnmapMemory = d.getDispatcher()->vkUnmapMemory;
    vulkanFunctions.vkFlushMappedMemoryRanges =
        d.getDispatcher()->vkFlushMappedMemoryRanges;
    vulkanFunctions.vkInvalidateMappedMemoryRanges =
        d.getDispatcher()->vkInvalidateMappedMemoryRanges;
    vulkanFunctions.vkBindBufferMemory = d.getDispatcher()->vkBindBufferMemory;
    vulkanFunctions.vkBindImageMemory = d.getDispatcher()->vkBindImageMemory;
    vulkanFunctions.vkGetBufferMemoryRequirements =
        d.getDispatcher()->vkGetBufferMemoryRequirements;
    vulkanFunctions.vkGetImageMemoryRequirements =
        d.getDispatcher()->vkGetImageMemoryRequirements;
    vulkanFunctions.vkCreateBuffer = d.getDispatcher()->vkCreateBuffer;
    vulkanFunctions.vkDestroyBuffer = d.getDispatcher()->vkDestroyBuffer;
    vulkanFunctions.vkCreateImage = d.getDispatcher()->vkCreateImage;
    vulkanFunctions.vkDestroyImage = d.getDispatcher()->vkDestroyImage;
    vulkanFunctions.vkCmdCopyBuffer = d.getDispatcher()->vkCmdCopyBuffer;
    vulkanFunctions.vkGetBufferMemoryRequirements2KHR =
        d.getDispatcher()->vkGetBufferMemoryRequirements2KHR;
    vulkanFunctions.vkGetImageMemoryRequirements2KHR =
        d.getDispatcher()->vkGetImageMemoryRequirements2KHR;
    vulkanFunctions.vkBindBufferMemory2KHR =
        d.getDispatcher()->vkBindBufferMemory2KHR;
    vulkanFunctions.vkBindImageMemory2KHR =
        d.getDispatcher()->vkBindImageMemory2KHR;
    vulkanFunctions.vkGetPhysicalDeviceMemoryProperties2KHR =
        physicalDevice.getDispatcher()->vkGetPhysicalDeviceMemoryProperties2KHR;
    vulkanFunctions.vkGetDeviceBufferMemoryRequirements =
        d.getDispatcher()->vkGetDeviceBufferMemoryRequirements;
    vulkanFunctions.vkGetDeviceImageMemoryRequirements =
        d.getDispatcher()->vkGetDeviceImageMemoryRequirements;

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT |
                          VMA_ALLOCATOR_CREATE_KHR_BIND_MEMORY2_BIT |
                          VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE4_BIT |
                          VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT |
                          VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT |
                          VMA_ALLOCATOR_CREATE_EXT_MEMORY_PRIORITY_BIT;
    allocatorInfo.physicalDevice = *physicalDevice;
    allocatorInfo.device = *d;
    allocatorInfo.pVulkanFunctions = &vulkanFunctions;
    allocatorInfo.instance = *instance;
    allocatorInfo.vulkanApiVersion = ORPHEE_VK_VERSION;

    VmaAllocator allocator{};
    vmaCreateAllocator(&allocatorInfo, &allocator);

    return Device{physicalDevice, d, qfs, qs, allocator};
  }

  return {};
}

vk::raii::Instance vkManager::createInstance() {
  const auto vc = checkInstanceVersion(ORPHEE_VK_VERSION,
                                       context.enumerateInstanceVersion());

  std::vector<const char *> layers;
  for (const auto &l : ORPHEE_REQUIRED_VK_INSTANCE_LAYERS) {
    layers.push_back(l);
  }

  if (!vc) {
    throw std::runtime_error("Vulkan version mismatch");
  }

  const auto lc = checkInstanceLayers(layers);
  if (!lc) {
    throw std::runtime_error("Vulkan layers mismatch");
  }

  std::vector<const char *> extensions;
  for (const auto &x : ORPHEE_REQUIRED_VK_INSTANCE_EXTENSIONS) {
    extensions.push_back(x);
  }

  if (settings.windowing) {
    for (const auto &x : ORPHEE_REQUIRED_VK_INSTANCE_WINDOWING_EXTENSIONS) {
      extensions.push_back(x);
    }
  }

  const auto ec = checkInstanceExtensions(extensions);
  if (!ec) {
    throw std::runtime_error("Vulkan extensions mismatch");
  }

  vk::ApplicationInfo appInfo{
      meta.appName.c_str(),
      VK_MAKE_API_VERSION(0, meta.appVersionMajor, meta.appVersionMinor, 0),
      ORPHEE_NAME,
      VK_MAKE_API_VERSION(0, ORPHEE_VERSION_MAJOR, ORPHEE_VERSION_MINOR, 0),
      ORPHEE_VK_VERSION};

  return context.createInstance({
      {},
      &appInfo,
      layers,
      extensions,
  });
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
    spdlog::error("Vulkan minor version mismatch");
    return false;
  }

  spdlog::info("Intance vk {}.{} is compatible with requested vk {}.{}",
               instanceMajor, instanceMinor, targetMajor, targetMinor);
  return true;
}

bool vkManager::checkInstanceLayers(std::span<const char *> layers) const {
  spdlog::info("Checking instance layers...");

  const auto availableLayers = context.enumerateInstanceLayerProperties();
  spdlog::info("Found {} available layers", availableLayers.size());

  std::unordered_set<std::string> availableSet;
  for (const auto &l : availableLayers) {
    availableSet.insert(l.layerName);
  }

  bool foundAll = true;
  for (const auto &l : layers) {
    if (availableSet.find(l) == availableSet.end()) {
      spdlog::error("Layer {} is not available", l);
      foundAll = false;
    } else {
      spdlog::info("Requested {} instance layer found", l);
    }
  }

  return foundAll;
}

bool vkManager::checkInstanceExtensions(
    std::span<const char *> extensions) const {
  spdlog::info("Checking instance extensions...");

  const auto availableExtensions =
      context.enumerateInstanceExtensionProperties();
  spdlog::info("Found {} available extensions", availableExtensions.size());

  std::unordered_set<std::string> availableSet;
  for (const auto &x : availableExtensions) {
    availableSet.insert(x.extensionName);
  }

  bool foundAll = true;
  for (const auto &x : extensions) {
    if (availableSet.find(x) == availableSet.end()) {
      spdlog::error("Extension {} is not available", x);
      foundAll = false;
    } else {
      spdlog::info("Requested {} instance extension found", x);
    }
  }

  return foundAll;
}

bool vkManager::checkPhysicalDeviceExtensions(
    const vk::PhysicalDevice &device, std::span<const char *> extensions) {
  spdlog::info("Checking physical device extensions...");

  const auto availableExtensions = device.enumerateDeviceExtensionProperties();
  spdlog::info("Found {} available extensions", availableExtensions.size());

  std::unordered_set<std::string> names;
  for (const auto &ex : availableExtensions) {
    names.insert(ex.extensionName);
  }

  bool foundAll = true;
  for (const auto &x : extensions) {
    if (names.find(x) == names.end()) {
      spdlog::error("Extension {} is not available", x);
      foundAll = false;
    } else {
      spdlog::info("Requested {} device extension found", x);
    }
  }

  return foundAll;
}

std::optional<uint32_t>
vkManager::obtainQueueFamilies(const vk::PhysicalDevice &device,
                               const QueueFamilyRequirements &r) {
  spdlog::info("Obtaining queue families...");

  const auto availableQueueFamilies = device.getQueueFamilyProperties();
  spdlog::info("Found {} queue families", availableQueueFamilies.size());

  for (uint32_t i = 0; const auto &q : availableQueueFamilies) {
    spdlog::info("Queue family {} queue count {}", i, q.queueCount);

    // check queue count
    if (r.count > q.queueCount) {
      continue;
    }

    // check capabilities
    if ((q.queueFlags & r.capabilities) != r.capabilities) {
      continue;
    }

    // check surface present support
    if (r.surface.has_value()) {
      if (device.getSurfaceSupportKHR(i, *r.surface) == vk::False) {
        continue;
      }
    }

    return i;

    ++i;
  }

  return {};
}
} // namespace orphee
