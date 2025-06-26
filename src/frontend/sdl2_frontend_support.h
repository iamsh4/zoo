#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#include "shared/argument_parser.h"

struct ImFont;

class SDL2_OpenGL_App {
public:
  SDL2_OpenGL_App(const ArgumentParser &arg_parser, const char *title);
  ~SDL2_OpenGL_App();

  bool is_exiting() const
  {
    return m_is_exiting;
  }

  void tick();

  void init();

  void show_windows(bool draw_windows = true)
  {
    m_draw_windows = draw_windows;
  }

private:
  void init_sdl2();
  void init_opengl(const char *title);
  void init_imgui();

protected:
  const ArgumentParser &m_arg_parser;
  SDL_Window *m_window = nullptr;
  SDL_GLContext m_gl_context;

  ImFont *m_imgui_font = nullptr;
  bool m_is_exiting = false;

  bool m_draw_windows = true;

  virtual void handle_sdl2_event(const SDL_Event &) {}
  virtual void tick_logic() = 0;
  virtual void post_init() {}
};
