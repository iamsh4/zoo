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
#include <backends/imgui_impl_sdl.h>
#include <backends/imgui_impl_opengl3.h>

#include "frontend/sdl2_frontend_support.h"

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

SDL2_OpenGL_App::SDL2_OpenGL_App(const ArgumentParser &arg_parser, const char *title)
  : m_arg_parser(arg_parser),
    m_window(nullptr)
{
  init_sdl2();
  init_opengl(title);
  init_imgui();
}

SDL2_OpenGL_App::~SDL2_OpenGL_App()
{
  // Uninstall the signal handlers
  signal(SIGSEGV, SIG_DFL);
  signal(SIGBUS, SIG_DFL);

  // TODO : This doesn't actually do nice cleanup for OpenGL etc. Just let SDL2 handle it.
  // This assumes that the app has only one instance during the lifetime of the program.
  SDL_Quit();
}

void
SDL2_OpenGL_App::init_sdl2()
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
SDL2_OpenGL_App::init_opengl(const char *title)
{
  const int width = 1800, height = width * 3 / 4;

  m_window =
    SDL_CreateWindow(title,
                     SDL_WINDOWPOS_CENTERED,
                     SDL_WINDOWPOS_CENTERED,
                     width,
                     height,
                     SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  if (m_window == nullptr) {
    throw new std::runtime_error("Could not create SDL Window!");
  }

  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

  SDL_GLContext m_gl_context = SDL_GL_CreateContext(m_window);
  if (m_gl_context == NULL) {
    printf("GL Context creation failed failed: %s\n", SDL_GetError());
    exit(1);
  }

  if (glewInit() != GLEW_OK) {
    fprintf(stderr, "Failed to initialize OpenGL loader!\n");
    exit(2);
  }
}

void
SDL2_OpenGL_App::init_imgui()
{
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  m_imgui_font = io.Fonts->AddFontDefault();
  io.Fonts->Build();

  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  if (m_arg_parser.get_flag("-viewports").value_or(false)) {
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
  }
  io.ConfigDockingWithShift = true;

  ImGui_ImplSDL2_InitForOpenGL(m_window, m_gl_context);
  ImGui_ImplOpenGL3_Init();
}

void
SDL2_OpenGL_App::tick()
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
            glViewport(0, 0, event.window.data1, event.window.data2);
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

  // Tick application logic and rendering
  tick_logic();
}

void
SDL2_OpenGL_App::init()
{
  // more?
  post_init();
}
