#include "renderer/vkinit.h"
#include <vulkan/vulkan.h>

namespace vkinit {

VkCommandPoolCreateInfo
command_pool_create_info(uint32_t queueFamilyIndex,
                         VkCommandPoolCreateFlags flags /*= 0*/)
{
  VkCommandPoolCreateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  info.pNext = nullptr;

  info.queueFamilyIndex = queueFamilyIndex;
  info.flags = flags;
  return info;
}

VkCommandBufferAllocateInfo
command_buffer_allocate_info(
  VkCommandPool pool,
  uint32_t count /*= 1*/,
  VkCommandBufferLevel level /*= VK_COMMAND_BUFFER_LEVEL_PRIMARY*/)
{
  VkCommandBufferAllocateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  info.pNext = nullptr;

  info.commandPool = pool;
  info.commandBufferCount = count;
  info.level = level;
  return info;
}

VkCommandBufferBeginInfo
command_buffer_begin_info(VkCommandBuffer command_buffer, VkCommandBufferUsageFlags flags)
{
  VkCommandBufferBeginInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  info.flags = flags;

  return info;
}

} // namespace vkinit
