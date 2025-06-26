
#include <cstdio>
#include "opengl3_renderer.h"

namespace zoo::dreamcast {

void
OpenGL3RendererNew::execute(const RendererExecuteContext &ctx)
{
//   printf("Opengl3RendererNew::execute %llu\n", ctx.render_timestamp);
  for(const auto& cmd : ctx.commands) {
    if (std::holds_alternative<CmdScreenClipping>(cmd)) {
      printf("CmdScreenClipping\n");
    } else if (std::holds_alternative<CmdRenderTriangles>(cmd)) {
      printf("CmdRenderTriangles\n");
    } else if (std::holds_alternative<CmdSetPalette>(cmd)) {
      printf("CmdSetPalette\n");
    } else if (std::holds_alternative<CmdSetFogColorLookupTable>(cmd)) {
      printf("CmdSetFogColorLookupTable\n");
    } else if (std::holds_alternative<CmdSetFramebuffer>(cmd)) {
      printf("CmdSetFramebuffer\n");
    } else if (std::holds_alternative<CmdVRAMInvalidation>(cmd)) {
      printf("CmdVRAMInvalidation\n");
    }
  }
}

void
OpenGL3RendererNew::copy_fb(void *dest, FramebufferConfig *out_config)
{
  // TODO
}

}
