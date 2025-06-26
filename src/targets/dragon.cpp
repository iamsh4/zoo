#include <backends/imgui_impl_sdl.h>
#include <backends/imgui_impl_opengl3.h>

#include "frontend/sdl2_frontend_support.h"

#include "systems/dragon/console.h"
#include "systems/dragon/director.h"
#include "systems/dragon/gui/gpu.h"
#include "systems/dragon/gui/screen.h"

#include "shared/profiling.h"
#include "shared/file_watch.h"
#include "gui/window_cpu.h"
#include "gui/window_jit_workbench.h"
#include "gui/window_cpu_guest_dragon.h"
#include "gui/imgui_container.h"
#include "gui/window_memeditor.h"

class DragonApp : public SDL2_OpenGL_App {
private:
  // std::unique_ptr<zoo::Vulkan> m_vulkan;
  std::shared_ptr<zoo::dragon::Console> m_console;
  std::shared_ptr<zoo::dragon::ConsoleDirector> m_director;
  std::unique_ptr<gui::ImGuiContainer> m_imgui_container;

  FileWatcher *m_file_watcher;

  // zoo::ps1::gui::SharedData m_shared_data;
  SDL_Surface *m_window_icon;

  std::string m_bin_path;

public:
  DragonApp(const ArgumentParser &arg_parser, const char *title)
    : SDL2_OpenGL_App(arg_parser, title)
  {
    m_file_watcher = FileWatcher::singleton();

    // XXX
    // m_vulkan = std::make_unique<zoo::Vulkan>(std::vector<const char *> {});

    const std::optional<std::string> bios = m_arg_parser.get_string("-bios");
    if (!bios) {
      throw std::runtime_error("-bios must point to bios file");
    }

    m_console         = std::make_shared<zoo::dragon::Console>(bios.value().c_str());
    m_director        = std::make_shared<zoo::dragon::ConsoleDirector>(m_console);
    m_imgui_container = std::make_unique<gui::ImGuiContainer>();

    auto mem_gui = std::make_shared<gui::MemoryEditor>(m_console->memory());
    mem_gui->add_named_section("Program", 0, 32 * 1024);
    mem_gui->add_named_section("CPU Scratch/Stack", 0x8000'0000, 0x8000'0000 + 4096);
    mem_gui->add_named_section("RAM", 0x04000000u, 0x04000000u + 32 * 1024 * 1024);
    mem_gui->add_named_section("BIOS", 0x8000'1000, 0x8000'1000 + 4096);
    m_imgui_container->addWindow(mem_gui);

    // const auto workbench = std::make_shared<gui::JitWorkbenchWindow>(m_director);

    m_imgui_container->addWindow(std::make_shared<gui::CPUWindow>(
      "RV32", std::make_shared<gui::DragonCPUWindowGuest>(m_director.get()), nullptr));

    if (m_arg_parser.get_flag("-paused")) {
      m_director->set_execution_mode(zoo::dragon::ConsoleDirector::ExecutionMode::Paused);
    }

    if (auto bin_path = m_arg_parser.get_string("-bin")) {
      m_bin_path = bin_path.value();
      m_console->load_bin(bin_path.value().c_str());

      m_file_watcher->add_watch(bin_path.value().c_str(), [&](FileWatcher::Notification) {
        m_console->load_bin(m_bin_path.c_str());
        m_console->reset();
      });
    }

    m_director->reset();
    m_director->launch_threads();
  }

  void shutdown()
  {
    m_director->shutdown_threads();
  }

  void post_init() override
  {
    // Setup app icon
    m_window_icon = SDL_LoadBMP("resources/dragon256.bmp");
    SDL_SetWindowIcon(m_window, m_window_icon);

    // Setup silly VRAM opengl texture
    glGenTextures(1, &gl_vram_tex);
    glBindTexture(GL_TEXTURE_2D, gl_vram_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // m_imgui_container->addWindow(std::make_unique<PS1VRAMWindow>(gl_vram_tex));
    // m_imgui_container->addWindow(std::make_shared<zoo::ps1::gui::VRAM>(
    //   m_console.get(), &m_shared_data, gl_vram_tex));

    m_imgui_container->addWindow(
      std::make_shared<zoo::dragon::gui::Screen>(m_console.get(), gl_vram_tex));

    m_imgui_container->addWindow(
      std::make_shared<zoo::dragon::gui::GPU>(m_console.get()));
  }

protected:
  u32 gl_vram_tex;

  virtual void handle_sdl2_event(const SDL_Event &event) override
  {
    switch (event.type) {

      case SDL_KEYDOWN: {
        switch (event.key.keysym.sym) {
          default:
            break;
        }
        break;
      }

      case SDL_KEYUP: {
        switch (event.key.keysym.sym) {
          default:
            break;
        }
        break;
      }

      default:
        break;
    }
  }

  void tick_logic() override
  {
    // Note: Console execution takes place in Director's CPU thread

    ////////////////////////////////////////////////////////
    // Rendering
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // This is really silly. Copy VRAM to opengl texture.
    // This will go away when the vulkan renderer stuff is completed.
    {
      const u8 *root     = m_console->memory()->root();
      const u8 *vram_ptr = &root[0x0400'0000];
      glBindTexture(GL_TEXTURE_2D, gl_vram_tex);
      glTexImage2D(GL_TEXTURE_2D,
                   0,
                   GL_RGB,
                   320,
                   240,
                   0,
                   GL_RGBA,
                   GL_UNSIGNED_SHORT_5_5_5_1,
                   (void *)vram_ptr);
      glBindTexture(GL_TEXTURE_2D, 0);
    }

    {
      ProfileZoneNamed("ImGuiRender");

      ImGui_ImplOpenGL3_NewFrame();
      ImGui_ImplSDL2_NewFrame(m_window);
      ImGui::NewFrame();

      m_imgui_container->draw(m_draw_windows);

      const auto &io = ImGui::GetIO();
      ImGui::Render();
      glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
      ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
      if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        SDL_Window *backup_current_window    = SDL_GL_GetCurrentWindow();
        SDL_GLContext backup_current_context = SDL_GL_GetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        SDL_GL_MakeCurrent(backup_current_window, backup_current_context);
      }
    }

    SDL_GL_SwapWindow(m_window);

    FrameMark;
  }
};

int
main(int argc, char **argv)
{
  ArgumentParser arg_parser(argc, argv);
  DragonApp app(arg_parser, "Dragon");
  app.init();

  while (!app.is_exiting()) {
    app.tick();
  }

  app.shutdown();
  return 0;
}
