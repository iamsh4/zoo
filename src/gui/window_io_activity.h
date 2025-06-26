#pragma once
#include "frontend/console_director.h"
#include "gui/window.h"
#include "opengl_image.h"

struct ImFont;

namespace gui {

/** */
class IOActivityWindow : public Window {
public:
  IOActivityWindow(std::shared_ptr<ConsoleDirector> director);

private:
  std::shared_ptr<ConsoleDirector> m_director;
  std::unique_ptr<OpenGLImage> sysmem_texture;
  std::unique_ptr<OpenGLImage> texmem_texture;
  std::unique_ptr<OpenGLImage> aicamem_texture;

  void render();
};

}
