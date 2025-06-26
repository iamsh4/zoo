#include <set>
#include <algorithm>
#include <string>
#include <vector>

#include "renderer/vkinit.h"
#include "renderer/vulkan.h"
#include "shared/platform.h"
#include "shared/profiling.h"
#include "shared/types.h"

namespace zoo {

#define CAT_(a, b) a##b
#define CAT(a, b) CAT_(a, b)
#define VARNAME(Var) CAT(Var, __LINE__)

#define ASSERT_SUCCESS(x)                                                                \
  const auto VARNAME(result) = x;                                                        \
  if (VARNAME(result) != VK_SUCCESS) {                                                   \
    printf("Expected VK_SUCCESS, got code %d\n", VARNAME(result));                       \
    assert(-1);                                                                          \
  }

i32
findQueueFamily(VkPhysicalDevice phys_device)
{
  u32 prop_count;
  vkGetPhysicalDeviceQueueFamilyProperties(phys_device, &prop_count, nullptr);

  std::vector<VkQueueFamilyProperties> props(prop_count);
  vkGetPhysicalDeviceQueueFamilyProperties(phys_device, &prop_count, props.data());

  // TODO : Prefer different queues, for now just look for the one guaranteed queue that
  // supports all operations.
  for (u32 i = 0; i < prop_count; ++i) {
    const auto &queue = props[i];
    const bool has_compute = queue.queueFlags & VK_QUEUE_COMPUTE_BIT;
    const bool has_transfer = queue.queueFlags & VK_QUEUE_TRANSFER_BIT;
    const bool has_graphics = queue.queueFlags & VK_QUEUE_GRAPHICS_BIT;

    if (has_compute && has_transfer && has_graphics) {
      return i;
    }
  }

  return -1;
}

std::vector<VkLayerProperties>
enumerateLayerProperties()
{
  u32 layer_props_count;
  ASSERT_SUCCESS(vkEnumerateInstanceLayerProperties(&layer_props_count, nullptr));

  std::vector<VkLayerProperties> layer_props(layer_props_count);
  ASSERT_SUCCESS(
    vkEnumerateInstanceLayerProperties(&layer_props_count, layer_props.data()));

  return layer_props;
}

VkPhysicalDevice
getBestPhysicalDevice(VkInstance instance)
{
  u32 device_count;
  vkEnumeratePhysicalDevices(instance, &device_count, nullptr);

  std::vector<VkPhysicalDevice> devices(device_count);
  vkEnumeratePhysicalDevices(instance, &device_count, devices.data());

  if (devices.empty()) {
    throw std::runtime_error("No physical devices found in this vulkan instance");
  }

  // TODO : Select the appropriate physical device (integrated vs dedicated, etc.)
  return devices[0];
}

Vulkan::Vulkan(const std::vector<const char *> &additional_instance_extensions)
{
  // Get Layer Properties
  const std::vector<VkLayerProperties> layer_properties = enumerateLayerProperties();
  std::set<std::string> available_layer_names;
  printf("renderer: available vulkan layers\n");
  for (const auto &layer : layer_properties) {
    printf(" - %-40s : %s\n", layer.layerName, layer.description);
    available_layer_names.insert(std::string(layer.layerName));
  }

  const auto is_layer_available = [&available_layer_names](const std::string&& str) {
    return std::find(available_layer_names.begin(), available_layer_names.end(), str) != available_layer_names.end();
  };

  // Create Instance
  {
    std::vector<const char *> layers;
    std::vector<const char *> instance_extensions;

    if constexpr (platform::getBuildOS() == platform::OS::MacOS) {
      layers.push_back("VK_LAYER_KHRONOS_validation");
      instance_extensions.push_back("VK_EXT_debug_utils");
      instance_extensions.push_back("VK_EXT_metal_surface");
      instance_extensions.push_back("VK_MVK_macos_surface");
      instance_extensions.push_back("VK_KHR_surface");
      instance_extensions.push_back("VK_KHR_portability_enumeration");
    } else if constexpr (platform::getBuildOS() == platform::OS::Linux) {
      layers.push_back("VK_LAYER_KHRONOS_validation");
      // instance_extensions.push_back("VK_EXT_debug_utils");
    }

    // Ensure all the layers we're asking for actually exist.
    for (const auto &layer_name : layers) {
      if(!is_layer_available(layer_name)) {
        printf("Required Vulkan layer '%s' is not present on this system\n", layer_name);
        assert("Missing Vulkan layer");
      }
    }

    // Add in the ones passed in through the constructor. This is intended to capture
    // windowing-system related extensions
    for (const auto &ext : additional_instance_extensions) {
      if (std::find(instance_extensions.begin(), instance_extensions.end(), ext) ==
          instance_extensions.end()) {
        instance_extensions.push_back(ext);
      }
    }

    VkApplicationInfo appInfo {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "Zoo",
      .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
      .pEngineName = "ZooRenderer",
      .engineVersion = VK_MAKE_VERSION(1, 0, 0),
      .apiVersion = VK_API_VERSION_1_1,
    };

    const VkInstanceCreateInfo instanceInfo {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
#ifdef ZOO_OS_MACOS
      .flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR,
#endif
      .pApplicationInfo = &appInfo,
      .enabledLayerCount = u32(layers.size()),
      .ppEnabledLayerNames = layers.data(),
      .enabledExtensionCount = u32(instance_extensions.size()),
      .ppEnabledExtensionNames = instance_extensions.data(),
    };

    ASSERT_SUCCESS(vkCreateInstance(&instanceInfo, nullptr, &m_instance));
    printf("renderer: Created vulkan instance\n");
  }

  // Create our device
  {
    m_physical_device = getBestPhysicalDevice(m_instance);

    VkPhysicalDeviceProperties physical_device_props;
    vkGetPhysicalDeviceProperties(m_physical_device, &physical_device_props);

    i32 found_queue_family = findQueueFamily(m_physical_device);
    assert(found_queue_family >= 0);
    m_queue_family = u32(found_queue_family);

    float queue_priority = 1.0;
    VkDeviceQueueCreateInfo queueCreateInfo {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = m_queue_family,
      .queueCount = 1,
      .pQueuePriorities = &queue_priority,
    };

    std::vector<const char *> device_extensions;

    // MacOS Vulkan is built on top of MoltenVK which requires this.
    if constexpr (platform::getBuildOS() == platform::OS::MacOS) {
      device_extensions.push_back("VK_KHR_portability_subset");
    }

    // Needed for PS1 VRAM read/write functionality where we directly read 16bit data.
    device_extensions.push_back("VK_KHR_16bit_storage");

    const VkPhysicalDeviceFeatures physical_device_features {
      .fragmentStoresAndAtomics = 1,
      .shaderInt16 = 1,
    };
    VkPhysicalDevice16BitStorageFeatures storage_feature {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES,
      .storageBuffer16BitAccess = true,
    };

    const VkDeviceCreateInfo createInfo {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = &storage_feature,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &queueCreateInfo,
      .enabledExtensionCount = u32(device_extensions.size()),
      .ppEnabledExtensionNames = device_extensions.data(),
      .pEnabledFeatures = &physical_device_features,
    };

    ASSERT_SUCCESS(vkCreateDevice(m_physical_device, &createInfo, nullptr, &m_device));
    printf("renderer: Created vulkan device ('%s')\n", physical_device_props.deviceName);
  }

  vkGetDeviceQueue(m_device, m_queue_family, 0, &m_queue);
  printf("renderer: Created vulkan queue\n");

  {
    const VkCommandPoolCreateInfo info {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT |
               VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
      .queueFamilyIndex = m_queue_family,
    };
    ASSERT_SUCCESS(vkCreateCommandPool(m_device, &info, nullptr, &m_command_pool));
    printf("renderer: Created vulkan command pool\n");
  }

  {
    const VkCommandBufferAllocateInfo cb_info {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = m_command_pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
    };
    vkAllocateCommandBuffers(m_device, &cb_info, &m_blocking_call_command_buffer);

    const VkFenceCreateInfo fence_info {
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    vkCreateFence(m_device, &fence_info, nullptr, &m_blocking_call_fence);
  }

  // Acquire swapchain
  // ...
}

void
Vulkan::execute_blocking(const std::function<void(VkCommandBuffer &)> &func)
{
  ProfileZone;
  const auto begin_info =
    vkinit::command_buffer_begin_info(m_blocking_call_command_buffer);

  vkResetCommandBuffer(m_blocking_call_command_buffer, 0);
  vkBeginCommandBuffer(m_blocking_call_command_buffer, &begin_info);
  func(m_blocking_call_command_buffer);
  vkEndCommandBuffer(m_blocking_call_command_buffer);

  VkPipelineStageFlags wait_stage_mask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
  VkSubmitInfo info {
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .pWaitDstStageMask = &wait_stage_mask,
    .commandBufferCount = 1,
    .pCommandBuffers = &m_blocking_call_command_buffer,
  };

  // Submit, then wait for work to complete
  vkResetFences(m_device, 1, &m_blocking_call_fence);
  vkQueueSubmit(m_queue, 1, &info, m_blocking_call_fence);
  vkWaitForFences(m_device, 1, &m_blocking_call_fence, true, u64(2'000'000'000ULL));
  vkDeviceWaitIdle(m_device);
}

Vulkan::~Vulkan() {}

} // namespace renderer
