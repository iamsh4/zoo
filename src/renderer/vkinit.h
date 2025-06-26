#pragma once

#include <vulkan/vulkan.h>

namespace vkinit {

VkCommandPoolCreateInfo command_pool_create_info(uint32_t queueFamilyIndex,
                                                 VkCommandPoolCreateFlags flags = 0);

VkCommandBufferAllocateInfo command_buffer_allocate_info(
  VkCommandPool pool,
  uint32_t count = 1,
  VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);

VkCommandBufferBeginInfo command_buffer_begin_info(
  VkCommandBuffer command_buffer,
  VkCommandBufferUsageFlags flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

}
