#if 0
#include <csignal>
#include <stdexcept>

#ifdef __APPLE__
#include <mach/task.h>
#include <mach/mach_init.h>
#include <mach/mach_port.h>
#endif

#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#include <imgui.h>
#include <imgui/backends/imgui_impl_sdl.h>
#include <imgui/backends/imgui_impl_vulkan.h>

#include "frontend/sdl2_vulkan_frontend.h"

#ifdef __APPLE__
void
deny_exc_bad_access()
{
  task_set_exception_ports(mach_task_self(),
                           EXC_MASK_BAD_ACCESS,
                           MACH_PORT_NULL, // m_exception_port,
                           EXCEPTION_DEFAULT,
                           0);
}
#endif

SDL2_Vulkan_App::SDL2_Vulkan_App(const ArgumentParser &arg_parser, const char *title)
  : m_arg_parser(arg_parser),
    m_window(nullptr)
{
  init_sdl2();
  init_vulkan(title);
  init_imgui();
}

SDL2_Vulkan_App::~SDL2_Vulkan_App()
{
  // Uninstall the signal handlers
  signal(SIGSEGV, SIG_DFL);
  signal(SIGBUS, SIG_DFL);

  // TODO : This doesn't actually do nice cleanup for OpenGL etc. Just let SDL2 handle it.
  // This assumes that the app has only one instance during the lifetime of the program.
  SDL_Quit();
}

void
SDL2_Vulkan_App::init_sdl2()
{
#ifdef __APPLE__
  deny_exc_bad_access();
#endif

  /* Optional... */
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

  static const auto sdl_subsystems =
    SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER;
  if (SDL_Init(sdl_subsystems) != 0) {
    throw new std::runtime_error("Could not initialize SDL!");
  }
}

void
SDL2_Vulkan_App::init_vulkan(const char *title)
{
  const int width = 1800, height = width * 3 / 4;

  m_window =
    SDL_CreateWindow(title,
                     SDL_WINDOWPOS_CENTERED,
                     SDL_WINDOWPOS_CENTERED,
                     width,
                     height,
                     SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
  if (m_window == nullptr) {
    throw new std::runtime_error("Could not create SDL Window!");
  }

  u32 instance_extension_count;
  SDL_Vulkan_GetInstanceExtensions(m_window, &instance_extension_count, nullptr);

  std::vector<const char *> instance_extensions(instance_extension_count);
  SDL_Vulkan_GetInstanceExtensions(
    m_window, &instance_extension_count, instance_extensions.data());

  m_vulkan = std::make_unique<zoo::Vulkan>(instance_extensions);

  assert(SDL_Vulkan_CreateSurface(m_window,
                                  (SDL_vulkanInstance)m_vulkan->m_instance,
                                  (SDL_vulkanSurface *)&m_window_surface));
}

void
SDL2_Vulkan_App::init_imgui()
{
  // 1: create descriptor pool for IMGUI
  //  the size of the pool is very oversized, but it's copied from imgui demo itself.
  VkDescriptorPoolSize pool_sizes[] = {
    { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
    { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
    { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
    { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
    { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
    { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
  };

  VkDescriptorPoolCreateInfo pool_info = {};
  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  pool_info.maxSets = 1000;
  pool_info.poolSizeCount = std::size(pool_sizes);
  pool_info.pPoolSizes = pool_sizes;

  VkDescriptorPool imguiPool;
  vkCreateDescriptorPool(m_vulkan->m_device, &pool_info, nullptr, &m_imgui_pool);

  // 2: initialize imgui library

  // this initializes the core structures of imgui
  ImGui::CreateContext();

  // this initializes imgui for SDL
  ImGui_ImplSDL2_InitForVulkan(m_window);

  // this initializes imgui for Vulkan
  ImGui_ImplVulkan_InitInfo init_info = {};
  init_info.Instance = m_vulkan->m_instance;
  init_info.PhysicalDevice = m_vulkan->m_physical_device;
  init_info.Device = m_vulkan->m_device;
  init_info.Queue = m_vulkan->m_queue;
  init_info.DescriptorPool = m_imgui_pool;
  init_info.MinImageCount = 3;
  init_info.ImageCount = 3;
  init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

  {
    const VkCommandBufferAllocateInfo cb_info {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = m_vulkan->m_command_pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
    };
    vkAllocateCommandBuffers(m_vulkan->m_device, &cb_info, &m_imgui_command_buffer);
  }

  // Define our renderpass
  {
    VkAttachmentDescription color_attachment = {};
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
    color_attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkAttachmentReference color_attachment_ref = {};
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_ref;

    VkRenderPassCreateInfo renderpass_info {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &color_attachment,
      .subpassCount = 1,
      .pSubpasses = &subpass,
    };
    vkCreateRenderPass(
      m_vulkan->m_device, &renderpass_info, nullptr, &m_imgui_renderpass);
  }

  // Color framebuffer
  {
    VkImageCreateInfo image_info = {};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.arrayLayers = 1;
    image_info.extent = { 1024, 512, 1 };
    image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.mipLevels = 1;
    image_info.pQueueFamilyIndices = &m_vulkan->m_queue_family;
    image_info.queueFamilyIndexCount = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode = VkSharingMode::VK_SHARING_MODE_EXCLUSIVE;
    image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                       VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_info.tiling = VK_IMAGE_TILING_LINEAR;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    allocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
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
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .image = m_vkimage,
        .subresourceRange {
          .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
          .baseMipLevel = 0,
          .levelCount = 1,
          .layerCount = 1,
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

  ImGui_ImplVulkan_Init(&init_info, m_imgui_renderpass);

  // execute a gpu command to upload imgui font textures
  m_vulkan->execute_blocking(
    [&](const VkCommandBuffer &cmd) { ImGui_ImplVulkan_CreateFontsTexture(cmd); });

  ImGui_ImplVulkan_DestroyFontUploadObjects();
}

void
SDL2_Vulkan_App::tick()
{
  // Handle generic events
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    ImGui_ImplSDL2_ProcessEvent(&event);
    handle_sdl2_event(event);

    switch (event.type) {
      case SDL_QUIT:
        m_is_exiting = true;
        break;

      case SDL_WINDOWEVENT:
        switch (event.window.event) {
          case SDL_WINDOWEVENT_SIZE_CHANGED:
            // TODO : Possible aspect ratio and or corner pinning
            // glViewport(0, 0, event.window.data1, event.window.data2);
            break;
        }
        break;

      case SDL_KEYDOWN: {
        switch (event.key.keysym.sym) {
          case SDLK_ESCAPE:
            m_is_exiting = true;
            break;
          case SDLK_F5:
            m_draw_windows = !m_draw_windows;
            break;
        }
      }
    }
  }

  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplSDL2_NewFrame(m_window);
  ImGui::NewFrame();

  // Tick application logic and rendering
  tick_logic();

  if (m_rebuild_swapchain) {
    int width, height;
    SDL_GetWindowSize(m_window, &width, &height);
    if (width > 0 && height > 0) {
      ImGui_ImplVulkan_SetMinImageCount(3);
      ImGui_ImplVulkanH_CreateOrResizeWindow(m_vulkan->m_instance,
                                             m_vulkan->m_physical_device,
                                             m_vulkan->m_device,
                                             &g_MainWindowData,
                                             g_QueueFamily,
                                             g_Allocator,
                                             width,
                                             height,
                                             3);
      g_MainWindowData.FrameIndex = 0;
      g_SwapChainRebuild = false;
    }
  }

  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), m_imgui_command_buffer, nullptr);
}
#endif
