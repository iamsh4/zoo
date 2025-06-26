#pragma once

#include "renderer.h"

namespace zoo::dreamcast {
class OpenGL3RendererNew : public Renderer {
public:
  void execute(const RendererExecuteContext&) override;
  void copy_fb(void *dest, FramebufferConfig *out_config) override;
private:
};
}