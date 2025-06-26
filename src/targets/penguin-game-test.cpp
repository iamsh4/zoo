// vim: expandtab:ts=2:sw=2

#ifdef __APPLE__
#include <mach/task.h>
#include <mach/mach_init.h>
#include <mach/mach_port.h>
#endif

#include <fcntl.h>
#include <filesystem>
#include <fmt/core.h>
#include <csignal>
#include <iostream>
#include <cstdio>
#include <list>
#include <set>
#include <unordered_map>
#include <optional>
#include <string>

#ifdef __linux__
#include <sys/prctl.h>
#endif

#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#include <imgui.h>
#include <backends/imgui_impl_sdl.h>
#include <backends/imgui_impl_opengl3.h>

#include "shared/file.h"
#include "shared/log.h"
#include "shared/ansi_color_constants.h"
#include "shared/parsing.h"
#include "core/console.h"
#include "guest/sh4/sh4.h"
#include "guest/sh4/sh4_debug.h"
#include "peripherals/modem.h"
#include "peripherals/keyboard.h"
#include "peripherals/region_free_dreamcast_disc.h"
#include "peripherals/vmu.h"
#include "serialization/input_timeline.h"
#include "media/gdrom_utilities.h"
#include "gpu/texture_manager.h"
#include "gpu/renderer.h"
#include "gpu/opengl3_renderer.h"
#include "systems/dreamcast/noop_renderer.h"

#include "gui/window_cpu_guest_sh4.h"
#include "gui/window_cpu_guest_arm7di.h"
#include "gui/imgui_container.h"
#include "gui/window_io_activity.h"
#include "gui/window_settings.h"
#include "gui/window_penguin_gamelib.h"
#include "gui/graph.h"
#include "gpu/holly.h"

#include "local/settings.h"

#include "apu/audio_sdl.h"

#include "shared/stopwatch.h"
#include "shared/argument_parser.h"

#include "shared/profiling.h"

#include "frontend/controllers.h"
#include "frontend/console_director.h"

#include "frontend/sdl2_frontend_support.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

using ControllerMappings = std::vector<std::pair<SDL_Joystick *, InputMapping>>;

extern bool dump_requested;

class PenguinTestApp : public SDL2_OpenGL_App {
private:
  serialization::CompleteInputState input_state;
  std::shared_ptr<Console> dreamcast;
  std::shared_ptr<ConsoleDirector> m_director;

  zoo::dreamcast::Renderer *m_renderer_new;
  gpu::Renderer *m_gpu_renderer;
  ControllerMappings m_inputs;

  std::unique_ptr<gui::ImGuiContainer> m_imgui_container;

  std::shared_ptr<zoo::local::Settings> m_settings;
  std::shared_ptr<zoo::local::GameLibrary> m_game_library;

  uint32_t gl_vram_tex;

public:
  PenguinTestApp(const ArgumentParser &arg_parser, const char *title)
    : SDL2_OpenGL_App(arg_parser, title)
  {
    const std::string home_dir_str = std::getenv("HOME");
    const std::string home_dir     = std::filesystem::path(home_dir_str);
    const std::string settings_dir = home_dir + std::string("/.config/zoo/");
    m_settings = zoo::local::safe_load_settings(settings_dir, "penguin.json");

    // Ensure we have a firmware folder
    if (!m_settings->has("dreamcast.firmware_dir")) {
      const std::string firmware_dir =
        home_dir + std::string("/.local/share/zoo/firmware/");
      std::filesystem::create_directories(firmware_dir);
      check_file_exists(firmware_dir);
      m_settings->set("dreamcast.firmware_dir", firmware_dir);
    }

    const std::filesystem::path firmware_dir =
      m_settings->get_or_default("dreamcast.firmware_dir", "");
    m_settings->set("dreamcast.bios_path", (firmware_dir / "dc_boot.bin").c_str());
    m_settings->set("dreamcast.flash_path", (firmware_dir / "dc_flash.bin").c_str());

    // Ensure we have a vmu flash folder defined in settings
    if (!m_settings->has("dreamcast.vmu_dir")) {
      const std::string vmu_flash_dir =
        home_dir + std::string("/.local/share/zoo/dreamcast_vmu/");
      std::filesystem::create_directories(vmu_flash_dir);
      check_file_exists(vmu_flash_dir);
      m_settings->set("dreamcast.vmu_dir", vmu_flash_dir);
    }
  }

  ~PenguinTestApp()
  {
    m_director->shutdown_threads();
    delete m_gpu_renderer;

    for (auto input : m_inputs) {
      SDL_JoystickClose(input.first);
    }
  }

  void init()
  {
    m_renderer_new = new zoo::dreamcast::NoopRenderer();
    dreamcast  = std::make_shared<Console>(m_settings, new apu::Audio_SDLImpl(), m_renderer_new);
    m_director = std::make_shared<ConsoleDirector>(dreamcast);

    dreamcast->power_reset();

    auto gdrom_path = m_arg_parser.get_string("-disc");
    if (gdrom_path.has_value()) {
      const auto disc = zoo::media::Disc::open(gdrom_path.value().c_str());

      const GDROMDiscMetadata metadata = gdrom_disc_metadata(disc.get());
      if (m_arg_parser.get_flag("-print-meta")) {
        // Note: We should print metadata from the original disc, not the
        // region-free-patch version.
        printf("DISC_META: Hardware ID     :: %s\n", metadata.hardware_id.c_str());
        printf("DISC_META: Maker ID        :: %s\n", metadata.maker_id.c_str());
        printf("DISC_META: Device Info     :: %s\n", metadata.device_info.c_str());
        printf("DISC_META: Area Symbols    :: %s\n", metadata.area_symbols.c_str());
        printf("DISC_META: Peripherals     :: %s\n", metadata.peripherals.c_str());
        printf("DISC_META: Product Number  :: %s\n", metadata.product_number.c_str());
        printf("DISC_META: Product Version :: %s\n", metadata.product_version.c_str());
        printf("DISC_META: Release Date    :: %s\n", metadata.release_date.c_str());
        printf("DISC_META: Boot Filename   :: %s\n", metadata.boot_filename.c_str());
        printf("DISC_META: Company Name    :: %s\n", metadata.company_name.c_str());
        printf("DISC_META: Software Name   :: %s\n", metadata.software_name.c_str());
      }

      // Check if this is a WinCE game, since we do not support these yet.
      if (metadata.peripherals[6] != '0') {
        printf("This game requires WinCE, which is not supported yet.\n");
        exit(1);
      }

      const auto region_free_disc = std::make_shared<RegionFreeDreamcastDisc>(disc);
      dreamcast->gdrom()->mount_disc(region_free_disc);
    }

    const std::string vmu_flash_dir(m_settings->get_or_default("dreamcast.vmu_dir", ""));
    for (int i = 0; i < 4; ++i) {
      char buffer[128];
      snprintf(buffer, sizeof(buffer), "%s/vmuflash.%u.bin", vmu_flash_dir.c_str(), i);
      m_director->attach_controller(i);
      m_director->attach_vmu(i, buffer);
    }

    glViewport(
      0, 0, int(ImGui::GetIO().DisplaySize.x), int(ImGui::GetIO().DisplaySize.y));

    glGenTextures(1, &gl_vram_tex);
    glBindTexture(GL_TEXTURE_2D, gl_vram_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    m_gpu_renderer = new gpu::BaseOpenGL3Renderer(dreamcast.get());

    const auto &sh4_mode_string = m_arg_parser.get_string("-sh4").value_or("native");
    if (sh4_mode_string == "native") {
      m_director->set_cpu_execution_mode(cpu::SH4::ExecutionMode::Native);
    } else if (sh4_mode_string == "bytecode") {
      m_director->set_cpu_execution_mode(cpu::SH4::ExecutionMode::Bytecode);
    } else if (sh4_mode_string == "interp") {
      m_director->set_cpu_execution_mode(cpu::SH4::ExecutionMode::Interpreter);
    } else {
      assert(false &&
             "Please provide -sh4 {native,bytecode,interp}. 'native' is default.");
    }

    m_director->launch_threads();
  }

  void tick_logic() final
  {
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    m_gpu_renderer->render_frontend(int(ImGui::GetIO().DisplaySize.x),
                                    int(ImGui::GetIO().DisplaySize.y));

    // Keeping some of this because it keeps the display properly sized
    if (true) {
      ProfileZoneNamed("ImGuiRender");

      ImGui_ImplOpenGL3_NewFrame();
      ImGui_ImplSDL2_NewFrame(m_window);
      ImGui::NewFrame();

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

    test_logic();
  }

  u32 m_last_screenshot_vblank   = 0;
  u64 m_last_time                = 0;
  bool m_a_button_pressed        = false;
  u32 m_last_button_press_vblank = 0;
  void test_logic()
  {
    const u32 screenshot_vblank_interval = 60 * 2;
    const u32 screenshot_vblank_min      = 60 * 5;

    const u64 current_time = m_director->console()->current_time();

    // Take a screenshot periodically
    const u32 current_vblank_count = m_director->console()->get_vblank_in_count();
    if (current_vblank_count > m_last_screenshot_vblank + screenshot_vblank_interval &&
        current_vblank_count > screenshot_vblank_min) {
      char buffer[256];
      snprintf(buffer, sizeof(buffer), "screenshot-%u.ppm", current_vblank_count);
      m_gpu_renderer->save_screenshot(buffer);
      m_last_screenshot_vblank = current_vblank_count;
    }

    // Every second we press the A button periodically
    if (current_vblank_count > 5 * 30 &&
        current_vblank_count > m_last_button_press_vblank + 120) {
      if (!m_a_button_pressed) {
        input_state.controllers[0].button_down(maple::Controller::Button::Start);
        m_a_button_pressed = true;
      } else {
        input_state.controllers[0].button_up(maple::Controller::Button::Start);
        m_a_button_pressed = false;
      }
      m_director->set_input_state(input_state);
      m_last_button_press_vblank = current_vblank_count;
    }

    // Exit after requested time
    const std::string stop_after =
      m_arg_parser.get_string("-stop-after").value_or("1800");
    if (current_vblank_count > unsigned(std::stoi(stop_after))) {
      m_is_exiting = true;
    }

    // Check if the scheduler is stuck since last tick (halted, etc.)
    if (m_last_time > 0 && current_time == m_last_time && m_director->is_halted()) {
      printf("Scheduler stuck at %lu\n", current_time);
      m_is_exiting = true;
    }

    m_last_time = current_time;
  }

  void handle_sdl2_event(const SDL_Event &event) final {}
};

int
main(int argc, char *argv[])
{
  const ArgumentParser arg_parser(argc, argv);
  PenguinTestApp app(arg_parser, "penguin-test");
  app.init();

  while (!app.is_exiting()) {
    app.tick();
  }

  return 0;
}
