#pragma once

#include <functional>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include "shared/types.h"

namespace zoo {

// Vulkan boiler-plate manager
class Vulkan {
public:
  VkInstance m_instance;
  VkPhysicalDevice m_physical_device;
  VkDevice m_device;

  u32 m_queue_family;
  VkQueue m_queue;
  VkCommandPool m_command_pool;
  
  VkCommandBuffer m_blocking_call_command_buffer;
  VkFence m_blocking_call_fence;

  void execute_blocking(const std::function<void(VkCommandBuffer &)> &);

  Vulkan(const std::vector<const char *> &additional_instance_extensions);
  ~Vulkan();
};

}
