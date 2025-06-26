#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1

#include "renderer/vkinit.h"
#include "shared/file.h"
#include "shared/profiling.h"
#include "systems/ps1/renderer.h"

namespace zoo::ps1 {

// constexpr u32 kMaxGeometryBufferSize = 2 * 1024 * 1024;
constexpr u32 kPCRangeSize = 4 * 32;

Renderer::Renderer(Vulkan *vulkan) : m_vulkan(vulkan)
{
  init();
}

Renderer::~Renderer()
{
  // TODO : Cleanup resources
}

void
Renderer::compile_shader(const char *spirv_path, VkShaderModule *module)
{
  ProfileZone;
  // open the file. With cursor at the end
  std::ifstream file(spirv_path, std::ios::ate | std::ios::binary);

  if (!file.is_open()) {
    assert(false && "couldn't open spirv file");
  }

  size_t file_size = (size_t)file.tellg();
  std::vector<uint32_t> buffer(file_size / sizeof(uint32_t));
  file.seekg(0);
  file.read((char *)buffer.data(), file_size);

  VkShaderModuleCreateInfo info {
    .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize = buffer.size() * sizeof(uint32_t),
    .pCode    = buffer.data(),
  };

  VkResult result = vkCreateShaderModule(m_vulkan->m_device, &info, nullptr, module);
  assert(VK_SUCCESS == result);
  (void)result;
}

void
Renderer::init()
{
  ProfileZone;

  // Setup VMA
  VmaVulkanFunctions vulkanFunctions    = {};
  vulkanFunctions.vkGetInstanceProcAddr = &vkGetInstanceProcAddr;
  vulkanFunctions.vkGetDeviceProcAddr   = &vkGetDeviceProcAddr;

  VmaAllocatorCreateInfo allocator_info = {};
  allocator_info.device                 = m_vulkan->m_device;
  allocator_info.instance               = m_vulkan->m_instance;
  allocator_info.vulkanApiVersion       = VK_API_VERSION_1_1;
  allocator_info.physicalDevice         = m_vulkan->m_physical_device;
  allocator_info.pVulkanFunctions       = &vulkanFunctions;
  vmaCreateAllocator(&allocator_info, &m_vma_allocator);

  // const std::string vertex_source = read_file_to_string("shaders/ps1.vert");
  // const std::string fragment_source = read_file_to_string("shaders/ps1.frag");

  const auto cmd_buffer_info =
    vkinit::command_buffer_allocate_info(m_vulkan->m_command_pool);
  vkAllocateCommandBuffers(m_vulkan->m_device, &cmd_buffer_info, &m_command_buffer);

  {
    VkFenceCreateInfo fence_info = {};
    fence_info.sType             = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags             = VK_FENCE_CREATE_SIGNALED_BIT;
    assert(VK_SUCCESS ==
           vkCreateFence(m_vulkan->m_device, &fence_info, nullptr, &m_fence));
  }

  {
    VkSamplerCreateInfo sampler_info {
      .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .magFilter               = VK_FILTER_NEAREST,
      .minFilter               = VK_FILTER_NEAREST,
      .addressModeU            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .anisotropyEnable        = false,
      .compareEnable           = false,
      .unnormalizedCoordinates = false,
    };
    vkCreateSampler(m_vulkan->m_device, &sampler_info, nullptr, &m_sampler);
  }

  {
    VkImageCreateInfo image_info     = {};
    image_info.sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.arrayLayers           = 1;
    image_info.extent                = { 1024, 512, 1 };
    image_info.format                = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.imageType             = VK_IMAGE_TYPE_2D;
    image_info.mipLevels             = 1;
    image_info.pQueueFamilyIndices   = &m_vulkan->m_queue_family;
    image_info.queueFamilyIndexCount = 1;
    image_info.samples               = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode           = VkSharingMode::VK_SHARING_MODE_EXCLUSIVE;
    image_info.usage                 = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                       VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_info.tiling = VK_IMAGE_TILING_LINEAR;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage                   = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags                   = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    allocInfo.requiredFlags           = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    vmaCreateImage(m_vma_allocator,
                   &image_info,
                   &allocInfo,
                   &m_vkimage,
                   &m_vkimage_allocation,
                   nullptr);

    vmaMapMemory(m_vma_allocator, m_vkimage_allocation, (void **)&m_vkimage_mapped);

    // Transition to general layout
    {
      const VkImageMemoryBarrier image_barrier = {
        .sType     = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .image     = m_vkimage,
        .subresourceRange {
          .aspectMask   = VK_IMAGE_ASPECT_COLOR_BIT,
          .baseMipLevel = 0,
          .levelCount   = 1,
          .layerCount   = 1,
        },
      };

      VkPipelineStageFlagBits srcFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
      VkPipelineStageFlagBits dstFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

      synchronous_cmd_begin();
      vkCmdPipelineBarrier(m_command_buffer,
                           srcFlags,
                           dstFlags,
                           0,
                           0,
                           nullptr,
                           0,
                           nullptr,
                           1,
                           &image_barrier);
      synchronous_cmd_end_and_submit(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    }
  }

  // VRAM "read" image, used for texture sampling
  {
    VkImageCreateInfo image_info     = {};
    image_info.sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.arrayLayers           = 1;
    image_info.extent                = { 1024, 512, 1 };
    image_info.format                = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.imageType             = VK_IMAGE_TYPE_2D;
    image_info.mipLevels             = 1;
    image_info.pQueueFamilyIndices   = &m_vulkan->m_queue_family;
    image_info.queueFamilyIndexCount = 1;
    image_info.samples               = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode           = VkSharingMode::VK_SHARING_MODE_EXCLUSIVE;
    image_info.usage                 = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                       VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                       VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.tiling = VK_IMAGE_TILING_LINEAR;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage                   = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags                   = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    allocInfo.requiredFlags           = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    vmaCreateImage(m_vma_allocator,
                   &image_info,
                   &allocInfo,
                   &m_vram_read_image,
                   &m_vram_read_image_allocation,
                   nullptr);

    vmaMapMemory(
      m_vma_allocator, m_vram_read_image_allocation, (void **)&m_vram_read_image_mapped);

    // Transition to general layout
    {
      const VkImageMemoryBarrier image_barrier = {
        .sType     = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .image     = m_vram_read_image,
        .subresourceRange {
          .aspectMask   = VK_IMAGE_ASPECT_COLOR_BIT,
          .baseMipLevel = 0,
          .levelCount   = 1,
          .layerCount   = 1,
        },
      };

      VkPipelineStageFlagBits srcFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
      VkPipelineStageFlagBits dstFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

      synchronous_cmd_begin();
      vkCmdPipelineBarrier(m_command_buffer,
                           srcFlags,
                           dstFlags,
                           0,
                           0,
                           nullptr,
                           0,
                           nullptr,
                           1,
                           &image_barrier);
      synchronous_cmd_end_and_submit(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    }
  }

  VkComponentMapping RGBA {
    .r = VK_COMPONENT_SWIZZLE_IDENTITY,
    .g = VK_COMPONENT_SWIZZLE_IDENTITY,
    .b = VK_COMPONENT_SWIZZLE_IDENTITY,
    .a = VK_COMPONENT_SWIZZLE_IDENTITY,
  };

  {
    VkImageViewCreateInfo imageview_info       = {};
    imageview_info.sType                       = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageview_info.image                       = m_vkimage;
    imageview_info.format                      = VK_FORMAT_R8G8B8A8_UNORM;
    imageview_info.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
    imageview_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageview_info.subresourceRange.baseArrayLayer = 0;
    imageview_info.subresourceRange.baseMipLevel   = 0;
    imageview_info.subresourceRange.layerCount     = 1;
    imageview_info.subresourceRange.levelCount     = 1;
    imageview_info.components                      = RGBA;
    vkCreateImageView(m_vulkan->m_device, &imageview_info, nullptr, &m_vkimageview);
  }

  {
    VkImageViewCreateInfo imageview_info       = {};
    imageview_info.sType                       = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageview_info.image                       = m_vram_read_image;
    imageview_info.format                      = VK_FORMAT_R8G8B8A8_UNORM;
    imageview_info.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
    imageview_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageview_info.subresourceRange.baseArrayLayer = 0;
    imageview_info.subresourceRange.baseMipLevel   = 0;
    imageview_info.subresourceRange.layerCount     = 1;
    imageview_info.subresourceRange.levelCount     = 1;
    imageview_info.components                      = RGBA;
    vkCreateImageView(
      m_vulkan->m_device, &imageview_info, nullptr, &m_vram_read_imageview);
  }

  // Define our renderpass
  {
    VkAttachmentDescription color_attachment = {};
    color_attachment.initialLayout           = VK_IMAGE_LAYOUT_GENERAL;
    color_attachment.finalLayout             = VK_IMAGE_LAYOUT_GENERAL;
    color_attachment.format                  = VK_FORMAT_R8G8B8A8_UNORM;
    color_attachment.loadOp                  = VK_ATTACHMENT_LOAD_OP_LOAD;
    color_attachment.storeOp                 = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.samples                 = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.stencilLoadOp           = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp          = VK_ATTACHMENT_STORE_OP_DONT_CARE;

    VkAttachmentReference color_attachment_ref = {};
    color_attachment_ref.attachment            = 0;
    color_attachment_ref.layout                = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &color_attachment_ref;

    VkRenderPassCreateInfo renderpass_info {
      .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments    = &color_attachment,
      .subpassCount    = 1,
      .pSubpasses      = &subpass,
    };
    vkCreateRenderPass(m_vulkan->m_device, &renderpass_info, nullptr, &m_renderpass);
  }

  VkFramebufferCreateInfo fb_info {
    .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
    .renderPass      = m_renderpass,
    .attachmentCount = 1,
    .pAttachments    = &m_vkimageview,
    .width           = 1024,
    .height          = 512,
    .layers          = 1,
  };
  vkCreateFramebuffer(m_vulkan->m_device, &fb_info, nullptr, &m_framebuffer);

  // Pixel buffer
  {
    VkBufferCreateInfo buffer_info    = {};
    buffer_info.sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.usage                 = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    buffer_info.pQueueFamilyIndices   = &m_vulkan->m_queue_family;
    buffer_info.queueFamilyIndexCount = 1;
    buffer_info.size                  = 1024 * 512 * sizeof(u32);

    // TODO : Host coherent is likely extremely slow.
    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage                   = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags                   = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    allocInfo.requiredFlags           = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    vmaCreateBuffer(m_vma_allocator,
                    &buffer_info,
                    &allocInfo,
                    &m_pixbuf,
                    &m_pixbuf_allocation,
                    nullptr);
    vmaMapMemory(m_vma_allocator, m_pixbuf_allocation, (void **)&m_pixbuf_mapped);
  }

  // Draw call polygon data
  {
    VkBufferCreateInfo buffer_info    = {};
    buffer_info.sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.usage                 = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    buffer_info.pQueueFamilyIndices   = &m_vulkan->m_queue_family;
    buffer_info.queueFamilyIndexCount = 1;
    buffer_info.size                  = 1024 * 1024 * 2;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage                   = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags                   = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    allocInfo.requiredFlags           = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    vmaCreateBuffer(m_vma_allocator,
                    &buffer_info,
                    &allocInfo,
                    &m_polygon_buffer,
                    &m_polygon_buffer_allocation,
                    nullptr);
  }

  {
    // create a descriptor pool that will hold 10 uniform buffers
    std::vector<VkDescriptorPoolSize> sizes = {
      { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10 },
      { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10 },
    };

    VkDescriptorPoolCreateInfo info {
      .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .flags         = 0,
      .maxSets       = 10,
      .poolSizeCount = u32(sizes.size()),
      .pPoolSizes    = sizes.data(),
    };
    vkCreateDescriptorPool(m_vulkan->m_device, &info, nullptr, &m_descriptor_pool);

    // Bindings for shaders
    VkDescriptorSetLayoutBinding pixbuf_binding = {
      .binding         = 0,
      .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .descriptorCount = 1,
      .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    VkDescriptorSetLayoutBinding image_sampler_binding {
      .binding         = 1,
      .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .descriptorCount = 1,
      .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    std::vector<VkDescriptorSetLayoutBinding> descriptor_set_bindings = {
      pixbuf_binding,
      image_sampler_binding,
    };

    const VkDescriptorSetLayoutCreateInfo setinfo = {
      .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .flags        = 0,
      .bindingCount = u32(descriptor_set_bindings.size()),
      .pBindings    = descriptor_set_bindings.data(),
    };
    VkResult result = vkCreateDescriptorSetLayout(
      m_vulkan->m_device, &setinfo, nullptr, &m_polygon_descriptor_set_layout);
    assert(VK_SUCCESS == result);
    (void)result;

    VkPushConstantRange pc_range {
      .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
      .offset     = 0,
      .size       = kPCRangeSize,
    };

    VkPipelineLayoutCreateInfo polygon_pipeline_layout_info {
      .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .flags                  = 0,
      .setLayoutCount         = 1,
      .pSetLayouts            = &m_polygon_descriptor_set_layout,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges    = &pc_range,
    };
    vkCreatePipelineLayout(m_vulkan->m_device,
                           &polygon_pipeline_layout_info,
                           nullptr,
                           &m_polygon_pipeline_layout);

    VkDescriptorSetAllocateInfo allocInfo = {
      .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool     = m_descriptor_pool,
      .descriptorSetCount = 1,
      .pSetLayouts        = &m_polygon_descriptor_set_layout,
    };
    result = vkAllocateDescriptorSets(
      m_vulkan->m_device, &allocInfo, &m_polygon_pipeline_descriptor_set);
    assert(VK_SUCCESS == result);
    (void)result;

    //
    VkDescriptorBufferInfo descriptor_buffer_info {
      .buffer = m_pixbuf,
      .offset = 0,
      .range  = sizeof(u16) * 1024 * 512,
    };
    VkWriteDescriptorSet buffer_write {
      .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet          = m_polygon_pipeline_descriptor_set,
      .dstBinding      = 0,
      .descriptorCount = 1,
      .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .pBufferInfo     = &descriptor_buffer_info,
    };

    // Trying to read from the color attachment (?? hope it works)
    VkDescriptorImageInfo descriptor_image_info {
      .sampler     = m_sampler,
      .imageView   = m_vram_read_imageview,
      .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
    };
    VkWriteDescriptorSet image_write {
      .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet          = m_polygon_pipeline_descriptor_set,
      .dstBinding      = 1,
      .descriptorCount = 1,
      .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .pImageInfo      = &descriptor_image_info,
    };

    std::vector<VkWriteDescriptorSet> descriptor_writes = { buffer_write, image_write };
    vkUpdateDescriptorSets(
      m_vulkan->m_device, descriptor_writes.size(), descriptor_writes.data(), 0, nullptr);
  }

  {
    compile_shader("resources/shaders/ps1.vert.spirv", &m_vertex_shader);
    compile_shader("resources/shaders/ps1.frag.spirv", &m_fragment_shader);

    //
    VkVertexInputBindingDescription vertex_binding {
      .binding   = 0,
      .stride    = sizeof(GPUVertexData),
      .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    VkVertexInputAttributeDescription vertex_attrib1 {
      .location = 0,
      .binding  = 0,
      .format   = VK_FORMAT_R32G32B32A32_SFLOAT,
      .offset   = 0,
    };
    VkVertexInputAttributeDescription vertex_attrib2 {
      .location = 1,
      .binding  = 0,
      .format   = VK_FORMAT_R8G8B8A8_UNORM,
      .offset   = offsetof(GPUVertexData, color),
    };
    VkVertexInputAttributeDescription vertex_attrib3 {
      .location = 2,
      .binding  = 0,
      .format   = VK_FORMAT_R32G32_UINT,
      .offset   = offsetof(GPUVertexData, texpage_clut),
    };

    std::vector<VkVertexInputAttributeDescription> vertex_attributes = { vertex_attrib1,
                                                                         vertex_attrib2,
                                                                         vertex_attrib3 };

    VkPipelineVertexInputStateCreateInfo vertex_input_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount   = 1,
      .pVertexBindingDescriptions      = &vertex_binding,
      .vertexAttributeDescriptionCount = u32(vertex_attributes.size()),
      .pVertexAttributeDescriptions    = vertex_attributes.data(),
    };
    VkPipelineShaderStageCreateInfo vs_stage_info = {
      .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage  = VK_SHADER_STAGE_VERTEX_BIT,
      .module = m_vertex_shader,
      .pName  = "main",
    };
    VkPipelineShaderStageCreateInfo fs_stage_info = {
      .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
      .module = m_fragment_shader,
      .pName  = "main",
    };
    VkPipelineInputAssemblyStateCreateInfo assembly_info = {
      .sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkPipelineRasterizationStateCreateInfo rasterization_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode             = VK_POLYGON_MODE_FILL,
      .cullMode                = VK_CULL_MODE_NONE,
      .lineWidth               = 1.0f,
      // Lots of stuff not filled in, but we don't cull, no depth, etc.
    };
    VkPipelineMultisampleStateCreateInfo multisample_info = {
      .sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
      .sampleShadingEnable  = VK_FALSE,
      .minSampleShading     = 1.0f,
    };
    VkPipelineColorBlendAttachmentState blend_info = {
      .blendEnable    = VK_FALSE,
      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };

    // make viewport state from our stored viewport and scissor.
    // at the moment we won't support multiple viewports or scissors
    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.pNext = nullptr;

    VkViewport viewport { .x = 0, .y = 0, .width = 1024, .height = 512 };
    VkRect2D scissor { .offset = { 0, 0 }, .extent = { 1024, 512 } };

    viewportState.viewportCount = 1;
    viewportState.pViewports    = &viewport;
    viewportState.scissorCount  = 1;
    viewportState.pScissors     = &scissor;

    // setup dummy color blending. We aren't using transparent objects yet
    // the blending is just "no blend", but we do write to the color attachment
    VkPipelineColorBlendStateCreateInfo colorBlending = {
      .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable   = false,
      .logicOp         = VK_LOGIC_OP_COPY,
      .attachmentCount = 1,
      .pAttachments    = &blend_info,
    };
    const std::vector<VkPipelineShaderStageCreateInfo> shader_stages = { vs_stage_info,
                                                                         fs_stage_info };

    VkGraphicsPipelineCreateInfo pipelineInfo = {
      .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount          = u32(shader_stages.size()),
      .pStages             = shader_stages.data(),
      .pVertexInputState   = &vertex_input_info,
      .pInputAssemblyState = &assembly_info,
      .pViewportState      = &viewportState,
      .pRasterizationState = &rasterization_info,
      .pMultisampleState   = &multisample_info,
      .pColorBlendState    = &colorBlending,
      .layout              = m_polygon_pipeline_layout,
      .renderPass          = m_renderpass,
      .subpass             = 0,
      .basePipelineHandle  = VK_NULL_HANDLE,
    };
    vkCreateGraphicsPipelines(
      m_vulkan->m_device, nullptr, 1, &pipelineInfo, nullptr, &m_polygon_pipeline);
  }

  // Persistently-map polygon data buffer
  vmaMapMemory(
    m_vma_allocator, m_polygon_buffer_allocation, (void **)&m_polygon_data_base);
  m_current_vertex_count = 0;
}

void
Renderer::push_triangle(const CmdDrawTriangle &cmd)
{
  ProfileZone;
  assert(cmd.opcode);

  m_commands.push_back({
    .type              = DrawCmdType::Triangle,
    .cmd_draw_triangle = cmd,
  });

  GPUVertexData::Flags flags;
  flags.opcode = cmd.opcode;

  // if (cmd.opcode == 0x2c) {
  //   printf("renderer_2c: (%f,%f) (%f,%f) (%f,%f) :: (%f,%f) (%f,%f) \n",
  //          float(cmd.pos1.x),
  //          float(cmd.pos1.y),
  //          float(cmd.pos2.x),
  //          float(cmd.pos2.y),
  //          float(cmd.pos3.x),
  //          float(cmd.pos3.y),
  //          float(cmd.tex1.x),
  //          float(cmd.tex1.y),
  //          float(cmd.tex2.x),
  //          float(cmd.tex2.y),
  //          float(cmd.tex3.x),
  //          float(cmd.tex3.y)
  //          );
  // }

  const u32 texpage_clut = (u32(cmd.clut_xy) << 16) | u32(cmd.tex_page);

  m_polygon_data_base[m_current_vertex_count].x            = cmd.pos1.x;
  m_polygon_data_base[m_current_vertex_count].y            = cmd.pos1.y;
  m_polygon_data_base[m_current_vertex_count].u            = cmd.tex1.x;
  m_polygon_data_base[m_current_vertex_count].v            = cmd.tex1.y;
  m_polygon_data_base[m_current_vertex_count].color        = cmd.color1;
  m_polygon_data_base[m_current_vertex_count].texpage_clut = texpage_clut;
  m_polygon_data_base[m_current_vertex_count].flags        = flags;
  m_current_vertex_count++;

  m_polygon_data_base[m_current_vertex_count].x            = cmd.pos2.x;
  m_polygon_data_base[m_current_vertex_count].y            = cmd.pos2.y;
  m_polygon_data_base[m_current_vertex_count].u            = cmd.tex2.x;
  m_polygon_data_base[m_current_vertex_count].v            = cmd.tex2.y;
  m_polygon_data_base[m_current_vertex_count].color        = cmd.color2;
  m_polygon_data_base[m_current_vertex_count].texpage_clut = texpage_clut;
  m_polygon_data_base[m_current_vertex_count].flags        = flags;
  m_current_vertex_count++;

  m_polygon_data_base[m_current_vertex_count].x            = cmd.pos3.x;
  m_polygon_data_base[m_current_vertex_count].y            = cmd.pos3.y;
  m_polygon_data_base[m_current_vertex_count].u            = cmd.tex3.x;
  m_polygon_data_base[m_current_vertex_count].v            = cmd.tex3.y;
  m_polygon_data_base[m_current_vertex_count].color        = cmd.color3;
  m_polygon_data_base[m_current_vertex_count].texpage_clut = texpage_clut;
  m_polygon_data_base[m_current_vertex_count].flags        = flags;
  m_current_vertex_count++;
}

void
Renderer::synchronous_cmd_begin()
{
  ProfileZone;
  const auto begin_info = vkinit::command_buffer_begin_info(m_command_buffer);
  vkResetCommandBuffer(m_command_buffer, 0);
  vkBeginCommandBuffer(m_command_buffer, &begin_info);
}

void
Renderer::synchronous_cmd_end_and_submit(VkPipelineStageFlags wait_stage_mask)
{
  if (m_commands.empty()) {
    return;
  }

  ProfileZone;
  vkEndCommandBuffer(m_command_buffer);

  VkSubmitInfo info {
    .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .pWaitDstStageMask  = &wait_stage_mask,
    .commandBufferCount = 1,
    .pCommandBuffers    = &m_command_buffer,
  };

  // Submit, then wait for work to complete
  vkResetFences(m_vulkan->m_device, 1, &m_fence);
  vkQueueSubmit(m_vulkan->m_queue, 1, &info, m_fence);
  vkWaitForFences(m_vulkan->m_device, 1, &m_fence, true, u64(2'000'000'000ULL));
}

void
Renderer::perform_pending_draws()
{
  ProfileZone;
  VkRenderPassBeginInfo render_pass_info{
    .sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
    .renderPass=m_renderpass,
    .framebuffer=m_framebuffer,
    .renderArea={
      .offset={.x=0, .y=0},
      .extent={.width=1024,.height=512},
    },
    .clearValueCount=0,
  };

  // Prelogue
  synchronous_cmd_begin();
  vkCmdBeginRenderPass(m_command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

  vkCmdBindPipeline(
    m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_polygon_pipeline);

  vkCmdPushConstants(m_command_buffer,
                     m_polygon_pipeline_layout,
                     VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                     0,
                     sizeof(GPUState),
                     &m_gpu_state);

  vkCmdBindDescriptorSets(m_command_buffer,
                          VK_PIPELINE_BIND_POINT_GRAPHICS,
                          m_polygon_pipeline_layout,
                          0,
                          1,
                          &m_polygon_pipeline_descriptor_set,
                          0,
                          nullptr);

  // Series of calls/PC changes
  std::vector<VkDeviceSize> buffer_offsets = { 0 };
  vkCmdBindVertexBuffers(
    m_command_buffer, 0, 1, &m_polygon_buffer, buffer_offsets.data());

  u32 vertex_offset        = 0;
  u32 pending_vertex_count = 0;

  for (const auto &cmd : m_commands) {

    // If we're switching commands from triangle, drain pending
    if (cmd.type != DrawCmdType::Triangle && pending_vertex_count > 0) {
      vkCmdDraw(m_command_buffer, pending_vertex_count, 1, vertex_offset, 0);
      vertex_offset += pending_vertex_count;
      pending_vertex_count = 0;
    }

    switch (cmd.type) {
      case DrawCmdType::Triangle: {
        pending_vertex_count += 3;
        break;
      }
      case DrawCmdType::SetUniforms: {
        // TODO
        break;
      }
      case DrawCmdType::UpdateGPUState: {
        static_assert(sizeof(GPUState) <= kPCRangeSize);
        vkCmdPushConstants(m_command_buffer,
                           m_polygon_pipeline_layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0,
                           sizeof(GPUState),
                           &cmd.cmd_update_gpu_state.gpu_state);
        break;
      }
      default: {
        assert(false);
      }
    }
  }

  // Perform any last triangle draws
  if (pending_vertex_count > 0) {
    vkCmdDraw(m_command_buffer, pending_vertex_count, 1, vertex_offset, 0);
  }

  // Prologue

  vkCmdEndRenderPass(m_command_buffer);
  synchronous_cmd_end_and_submit(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

  m_commands             = {};
  m_current_vertex_count = 0;
}

void
Renderer::update_gpu_state(const CmdUpdateGPUState &cmd)
{
  ProfileZone;
  m_gpu_state = cmd.gpu_state;

  m_commands.push_back(DrawCmd {
    .type                 = DrawCmdType::UpdateGPUState,
    .cmd_update_gpu_state = cmd,
  });
}

// PS1  : A1B5G5R5
// Host : R8G8B8A8

// Convert R8G8B8A8 -> A1B5G5R5
u32
rgba_to_ps1(u32 rgba)
{
  const u32 r = (rgba >> 0) & 0xff;
  const u32 g = (rgba >> 8) & 0xff;
  const u32 b = (rgba >> 16) & 0xff;
  const u32 a = (rgba >> 24) & 0xff;

  u32 ps1 = 0;
  ps1 |= (a ? 1 : 0) << 15;
  ps1 |= (b >> 3) << 10;
  ps1 |= (g >> 3) << 5;
  ps1 |= (r >> 3) << 0;
  return ps1;
}

u32
ps1_to_rgba(u16 ps1)
{
  const u32 a = (ps1 >> 15) & 0x1;
  const u32 b = (ps1 >> 10) & 0x1f;
  const u32 g = (ps1 >> 5) & 0x1f;
  const u32 r = (ps1 >> 0) & 0x1f;

  u32 result = 0;
  result |= (r << 3) << 0;
  result |= (g << 3) << 8;
  result |= (b << 3) << 16;
  result |= (a ? 0xff : 0) << 24;
  return result;
}

void
Renderer::sync_gpu_to_renderer(u8 *src)
{
  // TODO : implement something like a timeline semaphore so we can tell if the src/dest
  // are already in sync with each other on both sides, then do nothing.

  ProfileZone;
  // Copy PS1 VRAM -> our VkImage
  u16 *src16 = (u16 *)src;
  for (u32 i = 0; i < 1024 * 512; i++) {
    m_vkimage_mapped[i]         = ps1_to_rgba(src16[i]);
    m_vram_read_image_mapped[i] = m_vkimage_mapped[i];
    m_pixbuf_mapped[i]          = src16[i];
  }
}

void
Renderer::sync_renderer_to_gpu(u8 *dest)
{
  ProfileZone;
  perform_pending_draws();

  // Copy our VkImage -> PS1 VRAM
  u16 *dest16 = (u16 *)dest;
  for (u32 i = 0; i < 1024 * 512; i++) {
    dest16[i] = rgba_to_ps1(m_vkimage_mapped[i]);
  }
}

} // namespace systems::ps1
