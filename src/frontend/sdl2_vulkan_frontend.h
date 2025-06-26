#if 0
#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include "renderer/vulkan.h"
#include "shared/argument_parser.h"

struct ImFont;

class SDL2_Vulkan_App {
public:
  SDL2_Vulkan_App(const ArgumentParser &arg_parser, const char *title);
  ~SDL2_Vulkan_App();

  bool is_exiting() const
  {
    return m_is_exiting;
  }

  void tick();

private:
  void init_sdl2();
  void init_vulkan(const char *title);
  void init_imgui();

protected:
  const ArgumentParser &m_arg_parser;
  SDL_Window *m_window = nullptr;
  std::unique_ptr<zoo::Vulkan> m_vulkan;

  VkDescriptorPool m_imgui_pool;
  VkRenderPass m_imgui_renderpass;
  VkCommandBuffer m_imgui_command_buffer;
  VkSurfaceKHR m_window_surface;

  ImFont *m_imgui_font = nullptr;
  bool m_is_exiting = false;

  bool m_draw_windows = true;
  bool m_rebuild_swapchain = true;

  virtual void handle_sdl2_event(const SDL_Event &) {}
  virtual void tick_logic() = 0;
};

#endif
