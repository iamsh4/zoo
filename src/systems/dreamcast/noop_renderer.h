#pragma once

#include "renderer.h"

namespace zoo::dreamcast {

class NoopRenderer : public Renderer {
public:
  NoopRenderer() = default;
  void execute(const RendererExecuteContext&) override
  {
  }

  void copy_fb(void *dest, FramebufferConfig *out_config) override {}
};

}