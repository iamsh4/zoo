#pragma once

#include "gpu/holly.h"
#include "opengl_shader_program.h"
#include "shared/span.h"

namespace gpu {

namespace render {
struct FrameData;
}

class Renderer {
public:
  Renderer(Console *console) : m_console(console) {}
  virtual ~Renderer() {}

  /** Handle rendering for a frame of data from the console. */
  virtual void render_backend(const render::FrameData &rq) = 0;

  /** Generate a new frame for the front-end UI. */
  virtual void render_frontend(unsigned width, unsigned height) = 0;

  virtual void save_screenshot(const std::string &filename) = 0;

protected:
  Console *m_console;
};

};
