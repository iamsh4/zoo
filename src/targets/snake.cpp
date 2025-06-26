#include <backends/imgui_impl_sdl.h>
#include <backends/imgui_impl_opengl3.h>

#include "frontend/sdl2_frontend_support.h"

#include "systems/ps1/hw/disc.h"
#include "systems/ps1/console.h"
#include "systems/ps1/director.h"
#include "systems/ps1/controllers/digital.h"

#include "shared/profiling.h"
#include "gui/window_cpu.h"
#include "gui/window_jit_workbench.h"
#include "gui/window_cpu_guest_r3000.h"
#include "gui/imgui_container.h"
#include "gui/window_memeditor.h"
#include "systems/ps1/gui/hw_registers.h"
#include "systems/ps1/gui/vram.h"
#include "systems/ps1/gui/screen.h"
#include "systems/ps1/gui/gpu.h"
#include "systems/ps1/gui/shared_data.h"

class SnakeApp : public SDL2_OpenGL_App {
private:
  std::unique_ptr<zoo::Vulkan> m_vulkan;
  std::shared_ptr<zoo::ps1::Console> m_console;
  std::shared_ptr<zoo::ps1::ConsoleDirector> m_director;
  std::unique_ptr<gui::ImGuiContainer> m_imgui_container;

  zoo::ps1::gui::SharedData m_shared_data;
  SDL_Surface *m_window_icon;

public:
  SnakeApp(const ArgumentParser &arg_parser, const char *title)
    : SDL2_OpenGL_App(arg_parser, title)
  {
    // XXX
    m_vulkan = std::make_unique<zoo::Vulkan>(std::vector<const char *> {});

    m_console = std::make_shared<zoo::ps1::Console>(m_vulkan.get());
    m_director = std::make_shared<zoo::ps1::ConsoleDirector>(m_console);
    m_imgui_container = std::make_unique<gui::ImGuiContainer>();

    m_imgui_container->addWindow(
      std::make_shared<gui::MemoryEditor>(m_console->memory()));
    m_imgui_container->addWindow(
      std::make_shared<zoo::ps1::gui::HWRegisters>(m_console.get()));
    m_imgui_container->addWindow(
      std::make_shared<zoo::ps1::gui::GPU>(m_console.get(), &m_shared_data));

    // m_imgui_container->addWindow(std::make_shared<gui::JitWorkbenchWindow>(m_console.memory()));
    m_imgui_container->addWindow(std::make_shared<gui::CPUWindow>("R3000",
      std::make_shared<gui::R3000CPUWindowGuest>(m_director.get()), nullptr));

    if (auto disc_path = m_arg_parser.get_string("-disc")) {
      m_console->cdrom()->set_disc(zoo::ps1::Disc::create(disc_path.value().c_str()));
    }

    if (m_arg_parser.get_flag("-paused")) {
      m_director->set_execution_mode(zoo::ps1::ConsoleDirector::ExecutionMode::Paused);
    }

    m_console->set_controller(0, std::make_unique<zoo::ps1::DigitalPad>());

    m_director->launch_threads();
  }

  void shutdown()
  {
    m_director->shutdown_threads();
  }

  void post_init() override
  {
    // Setup app icon
    m_window_icon = SDL_LoadBMP("resources/snake256.bmp");
    SDL_SetWindowIcon(m_window, m_window_icon);

    // Setup silly VRAM opengl texture
    glGenTextures(1, &gl_vram_tex);
    glBindTexture(GL_TEXTURE_2D, gl_vram_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // m_imgui_container->addWindow(std::make_unique<PS1VRAMWindow>(gl_vram_tex));
    m_imgui_container->addWindow(std::make_shared<zoo::ps1::gui::VRAM>(
      m_console.get(), &m_shared_data, gl_vram_tex));

    m_imgui_container->addWindow(
      std::make_shared<zoo::ps1::gui::Screen>(m_console.get(), gl_vram_tex));
  }

protected:
  u32 gl_vram_tex;

  virtual void handle_sdl2_event(const SDL_Event &event) override
  {
    switch (event.type) {

      case SDL_KEYDOWN: {
        switch (event.key.keysym.sym) {
          case SDLK_0: {
            auto psx_exe = m_arg_parser.get_string("-exe");
            if (psx_exe.has_value()) {
              m_director->load_psx_exe(psx_exe.value().c_str());
            }
          } break;
          case SDLK_F1: {
            m_director->dump_ram("ps1.ram.bin", 0, 2 * 1024 * 1024);
            printf("Wrote RAM dump to file\n");
          } break;

          case SDLK_RETURN: {
            m_console->controller(0)->set_button(zoo::ps1::Controller::Button::Start, 1);
          } break;
          case SDLK_DOWN: {
            m_console->controller(0)->set_button(zoo::ps1::Controller::Button::JoypadDown,
                                                 1);
          } break;
          case SDLK_UP: {
            m_console->controller(0)->set_button(zoo::ps1::Controller::Button::JoypadUp,
                                                 1);
          } break;
          case SDLK_LEFT: {
            m_console->controller(0)->set_button(zoo::ps1::Controller::Button::JoypadLeft,
                                                 1);
          } break;
          case SDLK_RIGHT: {
            m_console->controller(0)->set_button(
              zoo::ps1::Controller::Button::JoypadRight, 1);
          } break;
          case SDLK_z: {
            m_console->controller(0)->set_button(zoo::ps1::Controller::Button::Cross, 1);
          } break;
          case SDLK_x: {
            m_console->controller(0)->set_button(zoo::ps1::Controller::Button::Circle, 1);
          } break;
          case SDLK_a: {
            m_console->controller(0)->set_button(zoo::ps1::Controller::Button::Square, 1);
          } break;
          case SDLK_s: {
            m_console->controller(0)->set_button(zoo::ps1::Controller::Button::Triangle,
                                                 1);
          } break;

          case SDLK_q: {
            m_console->controller(0)->set_button(zoo::ps1::Controller::Button::L1, 1);
          } break;
          case SDLK_e: {
            m_console->controller(0)->set_button(zoo::ps1::Controller::Button::R1, 1);
          } break;
          case SDLK_w: {
            m_console->controller(0)->set_button(zoo::ps1::Controller::Button::L2, 1);
          } break;
          case SDLK_r: {
            m_console->controller(0)->set_button(zoo::ps1::Controller::Button::R2, 1);
          } break;

          default:
            break;
        }
        break;
      }

      case SDL_KEYUP: {
        switch (event.key.keysym.sym) {
          case SDLK_RETURN: {
            m_console->controller(0)->set_button(zoo::ps1::Controller::Button::Start, 0);
          } break;
          case SDLK_DOWN: {
            m_console->controller(0)->set_button(zoo::ps1::Controller::Button::JoypadDown,
                                                 0);
          } break;
          case SDLK_UP: {
            m_console->controller(0)->set_button(zoo::ps1::Controller::Button::JoypadUp,
                                                 0);
          } break;
          case SDLK_LEFT: {
            m_console->controller(0)->set_button(zoo::ps1::Controller::Button::JoypadLeft,
                                                 0);
          } break;
          case SDLK_RIGHT: {
            m_console->controller(0)->set_button(
              zoo::ps1::Controller::Button::JoypadRight, 0);
          } break;
          case SDLK_z: {
            m_console->controller(0)->set_button(zoo::ps1::Controller::Button::Cross, 0);
          } break;
          case SDLK_x: {
            m_console->controller(0)->set_button(zoo::ps1::Controller::Button::Circle, 0);
          } break;
          case SDLK_a: {
            m_console->controller(0)->set_button(zoo::ps1::Controller::Button::Square, 0);
          } break;
          case SDLK_s: {
            m_console->controller(0)->set_button(zoo::ps1::Controller::Button::Triangle,
                                                 0);
          } break;

          case SDLK_q: {
            m_console->controller(0)->set_button(zoo::ps1::Controller::Button::L1, 0);
          } break;
          case SDLK_e: {
            m_console->controller(0)->set_button(zoo::ps1::Controller::Button::R1, 0);
          } break;
          case SDLK_w: {
            m_console->controller(0)->set_button(zoo::ps1::Controller::Button::L2, 0);
          } break;
          case SDLK_r: {
            m_console->controller(0)->set_button(zoo::ps1::Controller::Button::R2, 0);
          } break;

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
      const u8 *vram_ptr = m_console->gpu()->display_vram_ptr();
      glBindTexture(GL_TEXTURE_2D, gl_vram_tex);
      glTexImage2D(GL_TEXTURE_2D,
                   0,
                   GL_RGB,
                   1024,
                   512,
                   0,
                   GL_RGBA,
                   GL_UNSIGNED_SHORT_1_5_5_5_REV,
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
        SDL_Window *backup_current_window = SDL_GL_GetCurrentWindow();
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
  SnakeApp app(arg_parser, "Snake");
  app.init();

  for (u32 i = 0; i < 10'000'000; ++i) {
    if (app.is_exiting()) {
      break;
    }

    app.tick();
  }

  app.shutdown();
  return 0;
}
