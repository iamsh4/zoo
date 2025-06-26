#include <thread>
#include <vector>

#include "shared/file.h"
#include "shared/types.h"
#include "wgpu_renderer.h"

#include <wgpu.h>

namespace zoo::dreamcast {

WGPURenderer::WGPURenderer()
{
  make_compute_resources();
  _framebuffer.resize(640 * 480 * 4);
}

WGPURenderer::~WGPURenderer()
{
  if (_render.pipeline) {
    wgpuRenderPipelineRelease(_render.pipeline);
  }
  if (_shaders) {
    wgpuShaderModuleRelease(_shaders);
  }
  if (_render._color_view) {
    wgpuTextureViewRelease(_render._color_view);
  }
  if (_render._color) {
    wgpuTextureRelease(_render._color);
  }
  if (_render._depth_view) {
    wgpuTextureViewRelease(_render._depth_view);
  }
  if (_render._depth) {
    wgpuTextureRelease(_render._depth);
  }
}

#define PACKED __attribute__((packed))

struct PACKED Vertex {
  float x, y, z, _0;
  float u, v, _1, _2;
  float r, g, b, a;
};
struct PACKED GuestTriangleData {
  Vertex vertices[3];
};

// Needed for list sorting and iteration...
// - control (32b uint)
//   - list number    (3b)
//   - triangle index (16b)
// - depth (32b float)

const uint32_t kTimestampQueryCount = 1024;

const uint32_t kTileSize         = 32;
// const uint32_t kMaxTiles         = 600;
// const uint32_t kMaxTriangles     = 32 * 1024; // Maximum total triangles per render call
// const uint32_t kMaxTileTriangles = 1024;
// const uint32_t kSuperTileDimensionSize = 2; // 4x4 tiles per super-tile

const uint32_t kBufferMinAlignment  = 256;
const uint32_t kTileStatePixelBytes = 32; // Bytes per pixel in tile state buffer

void
WGPURenderer::make_compute_resources()
{
  const std::string shader_source =
    read_file_to_string("resources/shaders/dreamcast/pvr_render.wgsl");

  _shaders = create_shader_module("PVR Render CS", shader_source);

  // Guest PVR RAM
  _compute.pvr_ram = create_buffer("PVR RAM",
                                   WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc |
                                     WGPUBufferUsage_Storage,
                                   8 * 1024 * 1024);

  // Guest PVR Registers
  _compute.pvr_regs = create_buffer("PVR Registers",
                                    WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc |
                                      WGPUBufferUsage_Storage,
                                    0x4000);

  // Dispatch Details (Enough space for 4K region array entries)
  _compute.dispatch_details =
    create_buffer("Dispatch Details",
                  WGPUBufferUsage_CopyDst | WGPUBufferUsage_Storage,
                  kBufferMinAlignment * 4096);

  // Guest PVR Tile State
  _compute.tile_state = create_buffer("Tile State",
                                      WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc |
                                        WGPUBufferUsage_Storage,
                                      600 * kTileSize * kTileSize * kTileStatePixelBytes);

  // Generic read-back to CPU
  _compute.readback = create_buffer("Generic readback buffer",
                                    WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead,
                                    32 * 1024 * 1024);

  // Query resolve buffer
  _compute.query_resolve =
    create_buffer("Query resolve buffer",
                  WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc,
                  kTimestampQueryCount * 8);

  // Bind group layout
  std::vector<WGPUBindGroupLayoutEntry> bgl_entries;
  bgl_entries.push_back({});
  bgl_entries[0].binding               = 0;
  bgl_entries[0].buffer.type           = WGPUBufferBindingType_Storage;
  bgl_entries[0].buffer.minBindingSize = wgpuBufferGetSize(_compute.pvr_ram);
  bgl_entries[0].visibility            = WGPUShaderStage_Compute;

  bgl_entries.push_back({});
  bgl_entries[1].binding               = 1;
  bgl_entries[1].buffer.type           = WGPUBufferBindingType_ReadOnlyStorage;
  bgl_entries[1].buffer.minBindingSize = wgpuBufferGetSize(_compute.pvr_regs);
  bgl_entries[1].visibility            = WGPUShaderStage_Compute;

  bgl_entries.push_back({});
  bgl_entries[2].binding               = 2;
  bgl_entries[2].buffer.type           = WGPUBufferBindingType_ReadOnlyStorage;
  bgl_entries[2].buffer.minBindingSize = 256;
  bgl_entries[2].visibility            = WGPUShaderStage_Compute;

  bgl_entries.push_back({});
  bgl_entries[3].binding               = 3;
  bgl_entries[3].buffer.type           = WGPUBufferBindingType_Storage;
  bgl_entries[3].buffer.minBindingSize = wgpuBufferGetSize(_compute.tile_state);
  bgl_entries[3].visibility            = WGPUShaderStage_Compute;

  WGPUBindGroupLayoutDescriptor bind_group_layout_desc = {};
  bind_group_layout_desc.label      = "Dreamcast PVR CS Bind Group Layout";
  bind_group_layout_desc.entryCount = bgl_entries.size();
  bind_group_layout_desc.entries    = bgl_entries.data();
  _compute.bind_group_layout =
    wgpuDeviceCreateBindGroupLayout(_device, &bind_group_layout_desc);

  // Bind group
  std::vector<WGPUBindGroupEntry> bind_group_entries;

  // Binning
  bind_group_entries.push_back({});
  bind_group_entries[0].binding = 0;
  bind_group_entries[0].buffer  = _compute.pvr_ram;
  bind_group_entries[0].size    = wgpuBufferGetSize(_compute.pvr_ram);
  bind_group_entries.push_back({});
  bind_group_entries[1].binding = 1;
  bind_group_entries[1].buffer  = _compute.pvr_regs;
  bind_group_entries[1].size    = wgpuBufferGetSize(_compute.pvr_regs);
  bind_group_entries.push_back({});
  bind_group_entries[2].binding = 2;
  bind_group_entries[2].buffer  = _compute.dispatch_details;
  bind_group_entries[2].size    = 256;
  bind_group_entries.push_back({});
  bind_group_entries[3].binding = 3;
  bind_group_entries[3].buffer  = _compute.tile_state;
  bind_group_entries[3].size    = wgpuBufferGetSize(_compute.tile_state);

  WGPUBindGroupDescriptor bind_group_desc = {};
  bind_group_desc.entryCount              = bind_group_entries.size();
  bind_group_desc.entries                 = bind_group_entries.data();
  bind_group_desc.label                   = "Dreamcast CS Bind Group";
  bind_group_desc.layout                  = _compute.bind_group_layout;
  _compute.bind_group = wgpuDeviceCreateBindGroup(_device, &bind_group_desc);
  if (!_compute.bind_group) {
    throw std::runtime_error("Failed to create compute bind group");
  }

  // Pipeline layout
  WGPUPipelineLayoutDescriptor pipeline_layout_desc = {};
  pipeline_layout_desc.label                        = "Dreamcast CS Pipeline Layout";
  pipeline_layout_desc.bindGroupLayoutCount         = 1;
  pipeline_layout_desc.bindGroupLayouts             = &_compute.bind_group_layout;
  _compute.pipeline_layout =
    wgpuDeviceCreatePipelineLayout(_device, &pipeline_layout_desc);

  // Pipelines
  WGPUComputePipelineDescriptor desc = {};
  desc.compute.module                = _shaders;
  desc.layout                        = _compute.pipeline_layout;

  desc.label               = "PVR - Process Region Array Entry";
  desc.compute.entryPoint  = "pvr_render_tile";
  _compute.pipeline_render = wgpuDeviceCreateComputePipeline(_device, &desc);

  // Query set
  WGPUQuerySetDescriptor query_set_desc = {};
  query_set_desc.type                   = WGPUQueryType_Timestamp;
  query_set_desc.count                  = kTimestampQueryCount;
  _query_set = wgpuDeviceCreateQuerySet(_device, &query_set_desc);
}

void
WGPURenderer::render(u32 *const guest_pvr_ram,
                 u32 *const guest_pvr_regs,
                 const std::vector<u32> &region_array_entry_addresses)
{
  // Upload PVR RAM
  wgpuQueueWriteBuffer(_queue, _compute.pvr_ram, 0, guest_pvr_ram, 8 * 1024 * 1024);

  // Upload PVR registers
  wgpuQueueWriteBuffer(_queue, _compute.pvr_regs, 0, guest_pvr_regs, 0x4000);

  const u32 num_entries = region_array_entry_addresses.size();
  printf("Region Array Entries: %u\n", num_entries);

#if 1
  auto_submit("Compute Phase 1", [&](WGPUCommandEncoder encoder) {
    auto_compute_pass("Rasterization Pass", encoder, [&](WGPUComputePassEncoder pass) {
      wgpuComputePassEncoderSetPipeline(pass, _compute.pipeline_render);
      wgpuComputePassEncoderSetBindGroup(pass, 0, _compute.bind_group, 0, nullptr);
      wgpuComputePassEncoderDispatchWorkgroups(pass, num_entries, 1, 1);
    });

    // Copy PVR RAM to readback buffer
    wgpuCommandEncoderCopyBufferToBuffer(
      encoder, _compute.pvr_ram, 0, _compute.readback, 0, 8 * 1024 * 1024);
  });
#endif

  // Synchronously wait for the submission to complete
  // sync_wait_idle();

  // Read PVR RAM back to the guest
  sync_read_buffer(_compute.readback, 0, guest_pvr_ram, 8 * 1024 * 1024);

  // Read PVR RAM to _framebuffer
  {
    const u32 FB_R_CTRL = guest_pvr_regs[0x44 / 4];
    const u32 FB_R_SOF1 = guest_pvr_regs[0x50 / 4];
    // const u32 FB_R_SIZE = guest_pvr_regs[0x5c / 4];

    const u32 fb_depth = (FB_R_CTRL >> 2) & 0x3;
    // const u32 bpp[4]   = { 2, 2, 3, 4 };

    const u32 fb_width  = 640; //(((FB_R_SIZE >> 0) & 0x3ff) + 1) * 4 / bpp[fb_depth];
    const u32 fb_height = 480; //((FB_R_SIZE >> 10) & 0x3ff) + 2;

    const u32 linestride_bytes = guest_pvr_regs[0x4c / 4] * 8;

    const char *fb_depth_str[] = { "0555", "565", "888", "0888" };
    printf("Renderer resolve Framebuffer: %ux%u, depth: %s\n",
           fb_width,
           fb_height,
           fb_depth_str[fb_depth]);

    const u32 fb_size = fb_height * linestride_bytes;
    if (_framebuffer.size() < fb_size) {
      _framebuffer.resize(fb_size);
    }
    memcpy(_framebuffer.data(), guest_pvr_ram + FB_R_SOF1 / 4, fb_size);

    _framebuffer_config.width       = fb_width;
    _framebuffer_config.height      = fb_height;
    _framebuffer_config.linestride  = linestride_bytes;
    _framebuffer_config.fb_r_format = fb_depth;
  }
}

void
WGPURenderer::copy_fb(void *dest, FramebufferConfig *out_config)
{
  memcpy(dest,
         _framebuffer.data(),
         _framebuffer_config.height * _framebuffer_config.linestride);

  if (out_config) {
    *out_config = _framebuffer_config;
  }
}

void
WGPURenderer::sync_read_buffer(WGPUBuffer buffer,
                           uint32_t buffer_offset,
                           void *data,
                           size_t size)
{
  struct Context {
    WGPUBuffer buffer;
    void *dest;
    size_t size;
    bool done;
  } context = { buffer, data, size, false };

  wgpuBufferMapAsync(
    buffer,
    WGPUMapMode_Read,
    0,
    size,
    [](WGPUBufferMapAsyncStatus status, void *userdata) {
      Context *context = static_cast<Context *>(userdata);
      if (status == WGPUBufferMapAsyncStatus_Success) {
        void *mapping = wgpuBufferGetMappedRange(context->buffer, 0, context->size);
        memcpy(context->dest, mapping, context->size);
        wgpuBufferUnmap(context->buffer);
      }
      context->done = true;
    },
    &context);

  do {
    wgpuDevicePoll(_device, false, nullptr);
    std::this_thread::yield();
  } while (!context.done);

  // sync_wait_idle();
}

} // namespace zoo::dreamcast
