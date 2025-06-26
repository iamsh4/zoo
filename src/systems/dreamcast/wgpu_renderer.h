#pragma once

#include "gpu/display_list.h"
#include <webgpu.h>

#include "renderer.h"
#include "renderer/wgpu/renderer.h"

namespace zoo::dreamcast {

class WGPURenderer : public wgpu::Renderer {
public:
  WGPURenderer();
  ~WGPURenderer();

  void render(u32 *guest_pvr_ram, u32 *guest_pvr_regs, const std::vector<u32>& region_array_entry_addresses);
  void copy_fb(void *dest, FramebufferConfig* out_config);

private:
  void make_compute_resources();

  void sync_read_buffer(WGPUBuffer buffer,
                        uint32_t buffer_offset,
                        void *data,
                        size_t size);

  struct ComputeResources {
    WGPUBuffer pvr_ram          = {};
    WGPUBuffer pvr_regs         = {};
    WGPUBuffer dispatch_details = {};
    WGPUBuffer tile_state       = {};

    // ////////////////////////////////////////////

    WGPUBuffer readback      = {};
    WGPUBuffer query_resolve = {};

    WGPUBindGroupLayout bind_group_layout = {};
    WGPUBindGroup bind_group              = {};
    WGPUPipelineLayout pipeline_layout    = {};

    WGPUComputePipeline pipeline_render = {};
  } _compute;

  WGPUQuerySet _query_set = {};

  struct RenderPassResources {
    WGPUBuffer _triangles = {};
    WGPUBuffer _vertices  = {};

    WGPUTexture _color          = {};
    WGPUTextureView _color_view = {};

    WGPUTexture _depth          = {};
    WGPUTextureView _depth_view = {};

    WGPUBuffer _color_readback = {};

    WGPURenderPipeline pipeline = {};
  } _render;

  WGPUBuffer _map_buffer = {};

  WGPUShaderModule _shaders = {};

  std::vector<u8> _framebuffer;
  FramebufferConfig _framebuffer_config;
};

}