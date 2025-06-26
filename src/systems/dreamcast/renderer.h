#pragma once

#include <variant>

#include "gpu/display_list.h"
#include "shared/span.h"

namespace zoo::dreamcast {

struct FramebufferConfig {
  u32 width;
  u32 height;
  u32 linestride;
  u32 fb_r_format;
  u32 sof1;
  u32 sof2;
};

struct CmdScreenClipping {
  u32 x;
  u32 y;
  u32 width;
  u32 height;
  u32 inside; // 1:keep inside, 0:keep outside
};

struct CmdRenderTriangles {

};

/* 1024 32-bit palette updates. Only present when updated. */
struct CmdSetPalette {
  util::Span<u32> colors;
};

/* Update to Fog Lookup Table Data */
struct CmdSetFogColorLookupTable {
  util::Span<float> data;
};

struct CmdSetFramebuffer {
  FramebufferConfig framebuffer;
};

struct CmdVRAMInvalidation {
  u32 vram_offset;
  u32 size;
};

struct CmdExecRegionArray {
  u32 vram_address;
};

using Command = std::variant<CmdScreenClipping,
                             CmdRenderTriangles,
                             CmdSetPalette,
                             CmdSetFogColorLookupTable,
                             CmdSetFramebuffer,
                             CmdVRAMInvalidation,
                             CmdExecRegionArray>;

// RegionArray -> (Region, List Number) -> (Region, ListAddress)) -> PolyList

struct RendererExecuteContext {
  /** @brief Guest timestamp in which the render is requested. */
  uint64_t render_timestamp;

  u32 *guest_pvr_ram;
  u32 *guest_pvr_regs;
  util::Span<Command> commands;
};

class Renderer {
public:
  virtual ~Renderer() = default;

  virtual void execute(const RendererExecuteContext&) = 0;
  virtual void copy_fb(void *dest, FramebufferConfig *out_config) = 0;
};

}

// Opengl:
//   Wants to just get a buffer of triangles per list
//   Set of Region Array entries -> Passes

// Simulator:
//   Needs to see the raw triangles for building TA lists
//   - But! this can just be a post-process on top of the triangle lists
//     which means triangle list can be the default data structure
