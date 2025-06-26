// vim: expandtab:ts=2:sw=2

#ifdef __APPLE__
#include <mach/task.h>
#include <mach/mach_init.h>
#include <mach/mach_port.h>
#endif

#include <filesystem>
#include <fcntl.h>
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

#include <locale>

#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#include <imgui.h>
#include <backends/imgui_impl_sdl.h>
#include <backends/imgui_impl_opengl3.h>

#include "shared/log.h"
#include "shared/ansi_color_constants.h"
#include "shared/parsing.h"
#include "shared/file.h"
#include "core/console.h"
#include "guest/sh4/sh4.h"
#include "guest/sh4/sh4_debug.h"
#include "peripherals/modem.h"
#include "peripherals/keyboard.h"
#include "peripherals/vmu.h"
#include "serialization/input_timeline.h"
#include "peripherals/region_free_dreamcast_disc.h"

#include "systems/dreamcast/opengl3_renderer.h"

#include "media/gdrom_utilities.h"

#include "gpu/texture_manager.h"
#include "gpu/renderer.h"
#include "gpu/opengl3_renderer.h"

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

ControllerMappings
init_sdl_gamepads()
{
  ControllerMappings inputs;

  const int joystick_count = SDL_NumJoysticks();
  printf("SDL2: Found %d controllers\n", joystick_count);

  for (int i = 0; i < joystick_count; ++i) {

    SDL_Joystick *joystick = SDL_JoystickOpen(i);
    if (joystick == nullptr) {
      continue;
    }

    const char *joystick_name = SDL_JoystickName(joystick);

    // 1. Check if it's in the custom mapping
    const auto it = SDL2Joystick_SupportedInputs.find(joystick_name);
    if (it != SDL2Joystick_SupportedInputs.end()) {
      printf("Controller %d is mapped to '%s' (Custom Mapping)\n", i, joystick_name);
      inputs.push_back({ joystick, it->second });
    }

    // 2. Check if it's already a support SDL2 GameController
    else if (SDL_IsGameController(i)) {
      printf("Controller %d is mapped to '%s' (SDL2-supported)\n", i, joystick_name);

      InputMapping mapping;

      // Temporarily create a SDL2 GameController, ask SDL2 what the binds are from
      // the database, and map it to our bindings.
      auto *controller = SDL_GameControllerOpen(i);
      for (const auto &[sdl_button, maple_digital] : sdl2_digital_to_penguin) {
        int bind =
          SDL_GameControllerGetBindForButton(controller, sdl_button).value.button;
        mapping.digital[bind] = maple_digital;
      }
      for (const auto &[sdl_axis, maple_axis] : sdl2_axis_to_penguin) {
        int bind = SDL_GameControllerGetBindForAxis(controller, sdl_axis).value.axis;
        mapping.analog[bind] = maple_axis;
      }
      SDL_GameControllerClose(controller);

      inputs.push_back({ joystick, mapping });
    }

    // 3. SOL until we create a key-binding UI
    else {
      printf("Controller not supported: '%s'\n", joystick_name);
      continue;
    }
  }

  return inputs;
}

class PenguinApp : public SDL2_OpenGL_App {
private:
  serialization::CompleteInputState input_state;
  std::shared_ptr<Console> dreamcast;
  std::shared_ptr<ConsoleDirector> m_director;
  std::shared_ptr<serialization::Session> m_session;

  gpu::Renderer *m_gpu_renderer;
  zoo::dreamcast::Renderer *m_renderer_new;
  ControllerMappings m_inputs;

  std::unique_ptr<gui::ImGuiContainer> m_imgui_container;

  std::shared_ptr<zoo::local::Settings> m_settings;
  std::shared_ptr<zoo::local::GameLibrary> m_game_library;

  uint32_t gl_vram_tex;

  std::string m_pending_launch_file_path;
  void launch_game(std::string file_path)
  {
    //
  }

public:
  PenguinApp(const ArgumentParser &arg_parser, const char *title)
    : SDL2_OpenGL_App(arg_parser, title)
  {
    const char *home_dir_str = getenv("HOME");
    if (!home_dir_str) {
      printf("Could not find home folder, will not attempt to load settings file\n");
      throw std::runtime_error(
        "Could not find HOME environment variable, which is required.");
    }

    const std::string home_dir = std::filesystem::path(home_dir_str);
    const std::string settings_dir = home_dir + std::string("/.config/zoo/");
    m_settings = zoo::local::safe_load_settings(settings_dir, "penguin.json");

    m_game_library = std::make_shared<zoo::local::GameLibrary>();

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

  ~PenguinApp()
  {
    m_director->shutdown_threads();
    delete m_gpu_renderer;

    for (auto input : m_inputs) {
      SDL_JoystickClose(input.first);
    }
  }

  void init()
  {
    m_inputs = init_sdl_gamepads();
    printf("Using %lu joystick devices.\n", m_inputs.size());

    m_renderer_new = new zoo::dreamcast::OpenGL3RendererNew();
    dreamcast =
      std::make_shared<Console>(m_settings, new apu::Audio_SDLImpl(), m_renderer_new);
    m_director = std::make_shared<ConsoleDirector>(dreamcast);

    auto trace_path = m_arg_parser.get_string("-trace");
    if (trace_path.has_value()) {
      dreamcast->set_trace(std::make_unique<Trace>(trace_path.value().c_str()));
    }

    if (m_arg_parser.get_flag("-hide-windows")) {
      show_windows(false);
    }

    const bool loads_state = m_arg_parser.get_flag("-load").has_value();

    auto gdrom_path = m_arg_parser.get_string("-disc");
    if (gdrom_path.has_value()) {

      const auto disc = zoo::media::Disc::open(gdrom_path.value().c_str());
      const GDROMDiscMetadata metadata = gdrom_disc_metadata(disc.get());

      if (!loads_state) {
        const std::filesystem::path session_folder =
          std::filesystem::path("./.sessions") / metadata.product_number;
        std::filesystem::create_directories(session_folder);
        m_session = std::make_shared<serialization::FolderBasedSession>(session_folder);
        m_session->load();
        m_director->set_session(m_session);
      }

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

    if (loads_state) {
      std::filesystem::path snap_path = m_arg_parser.get_string("-load").value();
      if (!std::filesystem::exists(snap_path)) {
        fmt::println("Snapshot '{}' not found", snap_path.c_str());
        exit(1);
      }
      std::shared_ptr<serialization::Snapshot> snap = std::make_shared<serialization::Snapshot>();
      snap->load(snap_path);
      dreamcast->load_state(*snap);
    }

    const std::string vmu_flash_dir(m_settings->get_or_default("dreamcast.vmu_dir", ""));
    for (int i = 0; i < 4; ++i) {
      char buffer[128];
      snprintf(buffer, sizeof(buffer), "%s/vmuflash.%u.bin", vmu_flash_dir.c_str(), i);
      m_director->attach_controller(i);
      m_director->attach_vmu(i, buffer);
    }

    m_imgui_container = std::make_unique<gui::ImGuiContainer>();

    const auto workbench = std::make_shared<gui::JitWorkbenchWindow>(m_director);

    m_imgui_container->addWindow(std::make_shared<gui::AudioWindow>(m_director));
    m_imgui_container->addWindow(workbench);
    m_imgui_container->addWindow(
      std::make_shared<gui::JitCacheWindow>(m_director, workbench.get()));

    m_imgui_container->addWindow(std::make_shared<gui::CPUWindow>(
      "SH4",
      std::make_shared<gui::SH4CPUWindowGuest>(m_director.get()),
      workbench.get()));

    m_imgui_container->addWindow(std::make_shared<gui::CPUWindow>(
      "ARM7DI",
      std::make_shared<gui::ARM7DICPUWindowGuest>(m_director),
      workbench.get()));

    m_imgui_container->addWindow(std::make_shared<gui::CPUMMIOWindow>(m_director));
    m_imgui_container->addWindow(std::make_shared<gui::LoggerWindow>(m_director));
    m_imgui_container->addWindow(std::make_shared<gui::GraphicsWindow>(m_director));
    m_imgui_container->addWindow(std::make_shared<gui::IOActivityWindow>(m_director));

    auto mem_gui = std::make_shared<gui::MemoryEditor>(m_director->console()->memory());
    mem_gui->add_named_section("BIOS", 0, 2 * 1024 * 1024);
    mem_gui->add_named_section("Main RAM", 0x0c00'0000, 0x0c00'0000 + 16 * 1024 * 1024);
    mem_gui->add_named_section(
      "VRAM (32-Bit, Page 0)", 0x0500'0000, 0x0500'0000 + 1024 * 1024);
    mem_gui->add_named_section(
      "VRAM (32-Bit, Page 1)", 0x0510'0000, 0x0510'0000 + 1024 * 1024);
    mem_gui->add_named_section(
      "VRAM (32-Bit, Page 2)", 0x0520'0000, 0x0520'0000 + 1024 * 1024);
    mem_gui->add_named_section(
      "VRAM (32-Bit, Page 3)", 0x0530'0000, 0x0530'0000 + 1024 * 1024);
    mem_gui->add_named_section(
      "VRAM (32-Bit, Page 4)", 0x0540'0000, 0x0540'0000 + 1024 * 1024);
    mem_gui->add_named_section(
      "VRAM (32-Bit, Page 5)", 0x0550'0000, 0x0550'0000 + 1024 * 1024);
    mem_gui->add_named_section(
      "VRAM (32-Bit, Page 6)", 0x0560'0000, 0x0560'0000 + 1024 * 1024);
    mem_gui->add_named_section(
      "VRAM (32-Bit, Page 7)", 0x0570'0000, 0x0570'0000 + 1024 * 1024);
    m_imgui_container->addWindow(mem_gui);

    std::vector<gui::SettingsWindow::SettingsEntry> settings_entries = {
      gui::SettingsWindow::SettingsEntry { .name = "Game Library Directory",
                                           .key = "dreamcast.gamelib.scandir",
                                           .default_value = "/tmp/" },
    };

    m_imgui_container->addWindow(
      std::make_shared<gui::SettingsWindow>(m_settings, settings_entries));

    // Game Library and launch callbacks
    const auto launch_callback = [&](std::string file_path) {
      m_pending_launch_file_path = file_path;
    };
    m_imgui_container->addWindow(std::make_shared<gui::PenguinGameLibWindow>(
      m_settings, m_game_library, launch_callback));

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

    // Start with the console paused.
    if (m_arg_parser.get_flag("-paused")) {
      m_director->pause(true);
    }

    ////////////////////////////////////////////////////////////////////////////

    // VBlank-limiting logic.
    const bool vblank_limiting_enabled =
      !m_arg_parser.get_flag("-no-limit").value_or(false);
    m_director->set_flag(ConsoleDirector::flags::VBLANK_LIMITING,
                         vblank_limiting_enabled);

    auto elf_path = m_arg_parser.get_string("-elf");
    if (elf_path.has_value()) {
      m_director->console()->load_elf(elf_path.value());
    }

    // Input capture

    m_director->launch_threads();
  }

  void launch_pending_game()
  {
    m_director->launch_game(m_pending_launch_file_path);
    m_pending_launch_file_path = {};
  }

  void tick_logic() final
  {
    if (m_pending_launch_file_path.size() > 0) {
      launch_pending_game();
    }

    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    m_gpu_renderer->render_frontend(int(ImGui::GetIO().DisplaySize.x),
                                    int(ImGui::GetIO().DisplaySize.y));

#if 0
    zoo::dreamcast::FramebufferConfig fb_config;
    std::vector<u8> fb_buffer;
    fb_buffer.resize(2 * 1024 * 1024);
    m_director->console()->renderer()->copy_fb(fb_buffer.data(), &fb_config);

    glBindTexture(GL_TEXTURE_2D, gl_vram_tex);
    switch (fb_config.fb_r_format) {
      case 0: // 0555 RGB
        glPixelStorei(GL_UNPACK_ROW_LENGTH, fb_config.linestride / 2);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_RGB,
                     fb_config.width,
                     fb_config.height,
                     0,
                     GL_RGB,
                     GL_UNSIGNED_SHORT_1_5_5_5_REV,
                     fb_buffer.data());
        break;
      case 1: // 565 RGB
        glPixelStorei(GL_UNPACK_ROW_LENGTH, fb_config.linestride / 2);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_RGB,
                     fb_config.width,
                     fb_config.height,
                     0,
                     GL_RGB,
                     GL_UNSIGNED_SHORT_5_6_5_REV,
                     fb_buffer.data());
        break;
      case 2: // 888 RGB
        glPixelStorei(GL_UNPACK_ROW_LENGTH, fb_config.linestride / 3);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_RGB,
                     fb_config.width,
                     fb_config.height,
                     0,
                     GL_RGB,
                     GL_UNSIGNED_BYTE,
                     fb_buffer.data());
        break;
      case 3: // 8888 RGBA
        glPixelStorei(GL_UNPACK_ROW_LENGTH, fb_config.linestride / 4);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_RGB,
                     fb_config.width,
                     fb_config.height,
                     0,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     fb_buffer.data());
        break;
      default:
        assert(false && "Unknown framebuffer format");
    }
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
#endif

    if (true) {
      ProfileZoneNamed("ImGuiRender");

      ImGui_ImplOpenGL3_NewFrame();
      ImGui_ImplSDL2_NewFrame(m_window);
      ImGui::NewFrame();

      if (0) {
        ImGui::Begin("Screen");
        const float display_width = 800;
        const float display_height = 800 * 3 / 4;
        ImVec2 uv_tl { 0, 0 };
        ImVec2 uv_br { 1, 1 };
        ImGui::Image((void *)(size_t)gl_vram_tex,
                     ImVec2 { display_width, display_height },
                     uv_tl,
                     uv_br);
        ImGui::End();
      }

      m_imgui_container->draw(m_draw_windows);

      {
        static uint64_t last_query = epoch_nanos();
        static auto data = m_director->console()->metrics().next();

        if (epoch_nanos() - last_query > 1'000'000'000 / 4) {
          last_query = epoch_nanos();
          data = m_director->console()->metrics().next();
        }

        // Create an imgui window attached to the bottom left corner of the screen
        // with no title, semi-transparent background. We will place host-side metrics
        // like frame-rate, CPU usage, etc. here.
        ImGui::SetNextWindowPos(ImVec2(10, ImGui::GetIO().DisplaySize.y - 90));
        ImGui::SetNextWindowSize(ImVec2(600, 70));
        ImGui::SetNextWindowBgAlpha(0.35f);
        ImGui::Begin("Host Metrics",
                     nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

        {
          using Metric = zoo::dreamcast::Metric;

          const float host_us = data.get(Metric::HostNanos) / 1000.f;
          const float guest_us = data.get(Metric::GuestNanos) / 1000.f;
          const float limiting_us = data.get(Metric::HostNanosLimiting) / 1000.f;

          const float realtime = guest_us / host_us;

          auto str = fmt::format(
            "Host {:6.3Lf} us Guest {:6.3Lf} us Limit {:6.3Lf} us ({:.2f} Realtime)",
            host_us,
            guest_us,
            limiting_us,
            realtime);
          ImGui::Text("%s", str.c_str());

          str = fmt::format(
            "SH4 {:6.3Lf} us ARM7DI {:6.3Lf} us AICA {:6.3Lf} us TextureGen {:6.3Lf} us",
            data.get(Metric::NanosSH4) / 1000.f,
            data.get(Metric::NanosARM7DI) / 1000.f,
            data.get(Metric::NanosAICASampleGeneration) / 1000.f,
            data.get(Metric::NanosTextureGeneration) / 1000.f);
          ImGui::Text("%s", str.c_str());

          float rend_sec = data.get(Metric::CountStartRender) / guest_us * 1.e6f;
          float frames = data.get(Metric::CountStartRender);
          str = fmt::format(
            "Obj/f {:4} - Tri/f {:5} - R/s {:.2f} - FIFO/YUV/TEX /f {}k/{}k/{}k",
            int(data.get(Metric::CountRenderObjects) / frames),
            int(data.get(Metric::CountRenderPolygons) / frames),
            rend_sec,
            int(data.get(Metric::CountTaFifoBytes) / frames) / 1024,
            int(data.get(Metric::CountTaYuvBytes) / frames) / 1024,
            int(data.get(Metric::CountTaTextureBytes) / frames) / 1024);
          ImGui::Text("%s", str.c_str());
        }

        ImGui::End();
      }

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

  void handle_sdl2_event(const SDL_Event &event) final
  {
    switch (event.type) {
      case SDL_KEYDOWN: {
        bool controller_button_pressed = true;
        switch (event.key.keysym.sym) {
          case SDLK_UP:
            input_state.controllers[0].button_down(maple::Controller::Button::DpadUp);
            break;
          case SDLK_DOWN:
            input_state.controllers[0].button_down(maple::Controller::Button::DpadDown);
            break;
          case SDLK_LEFT:
            input_state.controllers[0].button_down(maple::Controller::Button::DpadLeft);
            break;
          case SDLK_RIGHT:
            input_state.controllers[0].button_down(maple::Controller::Button::DpadRight);
            break;
          case SDLK_q:
            input_state.controllers[0].trigger_left = 1.0f;
            break;
          case SDLK_e:
            input_state.controllers[0].trigger_right = 1.0f;
            break;
          case SDLK_c:
            input_state.controllers[0].button_down(maple::Controller::Button::X);
            break;
          case SDLK_v:
            input_state.controllers[0].button_down(maple::Controller::Button::Y);
            break;
          case SDLK_z:
            input_state.controllers[0].button_down(maple::Controller::Button::A);
            break;
          case SDLK_x:
            input_state.controllers[0].button_down(maple::Controller::Button::B);
            break;
          case SDLK_RETURN:
            input_state.controllers[0].button_down(maple::Controller::Button::Start);
            break;
          default:
            controller_button_pressed = false;
            break;
        }

        if (controller_button_pressed) {
          m_director->set_input_state(input_state);
        }

        switch (event.key.keysym.sym) {
          case SDLK_F8:
            // dreamcast->cpu()->debug_save_core("penguin.core");
            break;
          case SDLK_F9:
            m_director->pause_toggle();
            break;
          case SDLK_BACKSLASH: {
            printf("save state\n");
            m_director->save_state();
          } break;
          case SDLK_SLASH: {
            printf("load current\n");
            m_director->load_current();
          } break;
          case SDLK_COMMA: {
            printf("load previous\n");
            m_director->load_previous();
          } break;
          case SDLK_PERIOD: {
            printf("load next\n");
            m_director->load_next();
          } break;
          case SDLK_F10:
            // dump_requested = true;
            m_director->console()->dump_ram(
              "/tmp/dreamcast.ram.bin", 0x0C000000u, 0x01000000u);
            // m_director->console()->dump_ram(
            //   "/tmp/dreamcast.wavemem.bin", 0x00800000u, 0x00200000u);
            break;
          default:
            break;
        }
      } break;

      case SDL_KEYUP: {
        bool controller_button_pressed = true;
        switch (event.key.keysym.sym) {
          case SDLK_UP:
            input_state.controllers[0].button_up(maple::Controller::Button::DpadUp);
            break;
          case SDLK_DOWN:
            input_state.controllers[0].button_up(maple::Controller::Button::DpadDown);
            break;
          case SDLK_LEFT:
            input_state.controllers[0].button_up(maple::Controller::Button::DpadLeft);
            break;
          case SDLK_RIGHT:
            input_state.controllers[0].button_up(maple::Controller::Button::DpadRight);
            break;
          case SDLK_q:
            input_state.controllers[0].trigger_left = 0.0f;
            break;
          case SDLK_e:
            input_state.controllers[0].trigger_right = 0.0f;
            break;
          case SDLK_z:
            input_state.controllers[0].button_up(maple::Controller::Button::A);
            break;
          case SDLK_x:
            input_state.controllers[0].button_up(maple::Controller::Button::B);
            break;
          case SDLK_c:
            input_state.controllers[0].button_up(maple::Controller::Button::X);
            break;
          case SDLK_v:
            input_state.controllers[0].button_up(maple::Controller::Button::Y);
            break;
          case SDLK_RETURN:
            input_state.controllers[0].button_up(maple::Controller::Button::Start);
            break;
          default:
            controller_button_pressed = false;
            break;
        }

        if (controller_button_pressed) {
          m_director->set_input_state(input_state);
        }
      } break;

      case SDL_JOYAXISMOTION: {
        int gamepad_index = event.jaxis.which;
        auto &[joystick, bind_map] = m_inputs[gamepad_index];

        const auto mapping = bind_map.analog.find(event.jaxis.axis);
        if (mapping != bind_map.analog.end()) {
          switch (mapping->second) {
            case JoystickX:
              input_state.controllers[gamepad_index].analog_x =
                float(event.jaxis.value) / 65535.0f + 0.5f;
              break;
            case JoystickY:
              input_state.controllers[gamepad_index].analog_y =
                float(event.jaxis.value) / 65535.0f + 0.5f;
              break;
            case TriggerLeft:
              input_state.controllers[gamepad_index].trigger_left =
                float(event.jaxis.value) / 65535.0f + 0.5f;
              break;
            case TriggerRight:
              input_state.controllers[gamepad_index].trigger_right =
                float(event.jaxis.value) / 65535.0f + 0.5f;
              break;
            default:
              break;
          }
        }
        m_director->set_input_state(input_state);
      } break;

      case SDL_JOYBUTTONDOWN: {
        int gamepad_index = event.jaxis.which;
        auto &[joystick, bind_map] = m_inputs[gamepad_index];

        const auto mapping = bind_map.digital.find(event.jbutton.button);
        if (mapping != bind_map.digital.end()) {
          input_state.controllers[gamepad_index].button_down(mapping->second);
        }
        m_director->set_input_state(input_state);
      } break;

      case SDL_JOYBUTTONUP: {
        int gamepad_index = event.jaxis.which;
        auto &[joystick, bind_map] = m_inputs[gamepad_index];

        const auto mapping = bind_map.digital.find(event.jbutton.button);
        if (mapping != bind_map.digital.end()) {
          input_state.controllers[gamepad_index].button_up(mapping->second);
        }
        m_director->set_input_state(input_state);
      } break;
    }
  }
};

int
main(int argc, char *argv[])
{
  std::locale::global(std::locale("en_US.UTF-8"));

  const ArgumentParser arg_parser(argc, argv);
  PenguinApp app(arg_parser, "penguin");
  app.init();

  while (!app.is_exiting()) {
    app.tick();
  }

  return 0;
}
