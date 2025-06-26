#include <csignal>
#include <algorithm>
#include <fmt/core.h>
#include <vector>
#include <cstring>
#include <climits>
#include <mutex>
#include <unordered_map>
#include <chrono>

#include "shared/profiling.h"

#include "core/console.h"
#include "gpu/display_list.h"
#include "gpu/holly.h"
#include "core/registers.h"
#include "shared/stopwatch.h"

// TODO
#include "gpu/graphics_registers.h"
#include "gpu/tile_accelerator_registers.h"
#include "gpu/texture.h"

#if 0
#define DEBUG(args...) fmt::print(args)
#else
#define DEBUG(args...)
#endif

bool dump_requested = false;

namespace gpu {

const bool kNewRendererEnabled = false;

struct RenderStats {
  u32 num_polygons;
  u32 num_objects;
  u32 bytes_ta_fifo;
  u32 bytes_ta_yuv;
  u32 bytes_ta_tex;
};

RenderStats frame_stats = {};

const u32 kVram32BaseAddress = 0x0500'0000u;

const auto uv16_to_vec2f = [](u32 uv_data) {
  u32 u_data = (uv_data & 0xFFFF0000);
  u32 v_data = (uv_data & 0x0000FFFF) << 16;

  Vec2f uv;
  uv.x = reinterpret<float>(u_data);
  uv.y = reinterpret<float>(v_data);
  return uv;
};

const auto packed_color_argb_to_vec4 = [](u32 packed) {
  const u8 *packed_u8 = (u8 *)&packed;
  // ARGB -> RGBA
  return Vec4f {
    packed_u8[2] / 255.f, packed_u8[1] / 255.f, packed_u8[0] / 255.f, packed_u8[3] / 255.f
  };
};

static const std::unordered_map<u32, const char *> graphics_registers = {
  { 0x005F8000, "Device ID" },
  { 0x005F8004, "Revision Number" },
  { 0x005F8008, "Core & TA software reset" },

  { 0x005F8014, "Drawing start" },
  { 0x005F8018, "Test (writes prohibited)" },

  { 0x005F8020, "Base address for ISP" },

  { 0x005F802C, "Base address for Region Array" },
  { 0x005F8030, "Span Sorter control" },

  { 0x005F8040, "Border Area Color" },
  { 0x005F8044, "Frame Buffer Read Control" },
  { 0x005F8048, "Frame Buffer Write Control" },
  { 0x005F804C, "Frame Buffer Line Stride" },
  { 0x005F8050, "Read Start Address Field/Strip 1" },
  { 0x005F8054, "Read Start Address Field/Strip 2" },

  { 0x005F805C, "Frame Buffer XY Size" },
  { 0x005F8060, "Write Start Address Field/Strip 1" },
  { 0x005F8064, "Write Start Address Field/Strip 2" },
  { 0x005F8068, "Pixel Clip X Coordinate" },
  { 0x005F806C, "Pixel Clip Y Coordinate" },

  { 0x005F8074, "Intensity Volume Mode" },
  { 0x005F8078, "Comparison Value for Culling" },
  { 0x005F807C, "Parameter Read Control" },
  { 0x005F8080, "Pixel Sampling Control" },
  { 0x005F8084, "Comparison Value for Perp Polygons" },
  { 0x005F8088, "Background Surface Depth" },
  { 0x005F808C, "Background Surface Tag" },

  { 0x005F8098, "Translucent Polygon Sort Mode" },

  { 0x005F80A0, "Texture Memory Refresh Counter" },
  { 0x005F80A4, "Texture Memory Arbiter Control" },
  { 0x005F80A8, "Texture Memory Control" },

  { 0x005F80B0, "Color for LUT Fog" },
  { 0x005F80B4, "Color for Vertex Fog" },
  { 0x005F80B8, "Fog Scale Value" },
  { 0x005F80BC, "Color Clamping Max Value" },
  { 0x005F80C0, "Color Clamping Min Value" },

  { 0x005F80C4, "External Trigger HV" },
  { 0x005F80C8, "H-Blank Interrupt Control" },
  { 0x005F80CC, "V-Blank Interrupt Control" },
  { 0x005F80D0, "Sync Pulse Generator Control" },
  { 0x005F80D4, "H-Blank Control" },
  { 0x005F80D8, "HV Counter Load Value" },
  { 0x005F80DC, "V-Blank Control" },
  { 0x005F80E0, "Sync Width Control" },
  { 0x005F80E4, "Texturing Control" },
  { 0x005F80E8, "Video Output Control" },
  { 0x005F80EC, "Video Output Start X" },
  { 0x005F80F0, "Video Output Start Y" },
  { 0x005F80F4, "X & Y Scaler Control" },

  { 0x005F8108, "Palette RAM Control" },
  { 0x005F810C, "Sync Pulse Generator Status" },
  { 0x005F8110, "Frame Buffer Burst Control" },
  { 0x005F8114, "Frame Buffer Start Address" },
  { 0x005F8118, "Y Scaling Coeffecient" },
  { 0x005F811C, "Punch-Through Polygon Alpha" },

  { 0x005F8124, "Object List Write Address" },
  { 0x005F8128, "ISP/TSP Parameter Write Address" },
  { 0x005F812C, "Start Address Next Object Pointer" },
  { 0x005F8130, "ISP/TSP Parameter Address Limit" },
  { 0x005F8134, "Next Object Pointer Block Address" },
  { 0x005F8138, "ISP/TSP Parameter Current Write Address" },
  { 0x005F813C, "Global Tile Clip Control" },
  { 0x005F8140, "Object List Control" },
  { 0x005F8144, "TA Initialization" },
  { 0x005F8148, "YUV422 Texture Write Start Address" },
  { 0x005F814C, "YUV Converter Control" },
  { 0x005F8150, "YUV Converter Macro Block Counter" },
};

Holly::Holly(Console *const console)
  : m_console(console),
    m_renderer(console->renderer()),
    m_event_spg("gpu.spg_line_update",
                std::bind(&Holly::step_spg_line, this),
                console->scheduler()),
    m_event_render("gpu.render_completed",
                   std::bind(&Holly::finish_render, this),
                   console->scheduler()),
    m_event_interrupt("gpu.ta_interrupts",
                      std::bind(&Holly::handle_interrupt_event, this),
                      console->scheduler())
{
  for (int i = 0; i < 8; ++i) {
    m_frame_data.push_back(new render::FrameData {});
  }

  m_current_frame_data = m_frame_data[0];
  reset();
}

Holly::~Holly()
{
  m_is_running.store(false);

  for (auto frame_data : m_frame_data) {
    delete frame_data;
  }

  m_event_spg.cancel();
  m_event_render.cancel();
  m_event_interrupt.cancel();
}

void
Holly::handle_interrupt_event()
{
  while (!m_interrupt_queue.empty()) {
    m_console->interrupt_normal(Interrupts::Normal::Type(m_interrupt_queue.front()));
    m_interrupt_queue.pop();
  }
}

void
Holly::prepare_frame_textures()
{
  DEBUG("prepare :: display_lists = {}\n", m_render_frame_data->display_lists.size());

  const u64 start = epoch_nanos();

  // Collect all the distinct textures used in this frame
  std::unordered_set<std::shared_ptr<Texture>> textures_in_use_this_frame;
  for (const auto &dl : m_render_frame_data->display_lists) {
    if (dl.param_control_word.texture) {
      auto texture = m_console->texture_manager()->get_texture_handle(dl.texture_key);
      textures_in_use_this_frame.insert(texture);
    }
  }

  // Check to see if any of these textures need to be recalculated
  for (auto tex : textures_in_use_this_frame) {
    DEBUG(
      "prepare :: tex(host {}, guest 0x{:08x}, uuid={}, last_updated={}, last_used={}), "
      "start_render_count={}\n",
      (void *)tex.get(),
      tex->dc_vram_address,
      tex->uuid,
      tex->last_updated_on_frame,
      tex->last_used_on_frame,
      m_gpu_state.start_render_count);

    if (tex->last_updated_on_frame == m_gpu_state.start_render_count ||
        tex->hash == 0xFFFF'FFFF) {
      texture_logic::calculate_texture_data(m_console, tex);
      tex->is_dirty = true;
    }

    // This texture is used on this frame, update last_used.
    tex->last_used_on_frame = m_gpu_state.start_render_count;
  }

  const u64 end = epoch_nanos();
  m_console->metrics().increment(zoo::dreamcast::Metric::NanosTextureGeneration,
                                 end - start);
}

u32
Holly::get_render_count() const
{
  return m_gpu_state.start_render_count;
}

void
Holly::print_region_array()
{
  printf("StartRender : REGION_BASE(0x%08x)", _regs.REGION_BASE);
  const bool region_header_type = _regs.FPU_PARAM_CFG & (1 << 21);

  u32 addr = 0x0500'0000 + _regs.REGION_BASE;
  addr &= 0x0FFF'FFFB;
  u32 last_tile_x = 999, last_tile_y = 999;
  while (true) {
    const u32 control   = m_console->memory()->read<u32>(addr);
    const bool last     = control & (1 << 31);
    const bool z_clear  = control & (1 << 30);
    const bool autosort = region_header_type && ((control & (1 << 29)) == 0);
    const bool flush    = control & (1 << 28);
    const u32 tile_x    = (control >> 2) & 0x3f;
    const u32 tile_y    = (control >> 8) & 0x3f;

    u32 pointers[6];
    pointers[0] = m_console->memory()->read<u32>(addr + 4);
    pointers[1] = m_console->memory()->read<u32>(addr + 8);
    pointers[2] = m_console->memory()->read<u32>(addr + 12);
    pointers[3] = m_console->memory()->read<u32>(addr + 16);
    if (region_header_type) {
      pointers[4] = m_console->memory()->read<u32>(addr + 20);
    }

    bool first_for_this_tile = false;
    if (tile_x != last_tile_x || tile_y != last_tile_y) {
      first_for_this_tile = true;
      printf("\n Tile(%3u,%3u) -", tile_x, tile_y);
    }

    printf(" %s(%s%s%s) %s%s%s%s%s",
           !first_for_this_tile ? "~ " : "",
           autosort ? "S" : ".",
           z_clear ? "." : "C",
           flush ? "." : "F",
           pointers[0] & 0x8000'0000 ? "." : "0",
           pointers[1] & 0x8000'0000 ? "." : "1",
           pointers[2] & 0x8000'0000 ? "." : "2",
           pointers[3] & 0x8000'0000 ? "." : "3",
           region_header_type ? (pointers[4] & 0x8000'0000 ? "." : "4") : ".");

    addr += 4 * (region_header_type ? 6 : 5);
    last_tile_x = tile_x;
    last_tile_y = tile_y;

    if (last) {
      // Last region
      break;
    }
  }
  printf("\n");
}

void
Holly::debug_walk_frame()
{
  printf("Region array dump\n");

  const bool region_header_type = _regs.FPU_PARAM_CFG & (1 << 21);
  VRAMAddress32 ra_addr         = VRAMAddress32 { _regs.REGION_BASE };
  while (true) {
    const u32 control   = vram_read(ra_addr);
    const bool last     = control & (1u << 31);
    const bool z_clear  = control & (1u << 30);
    const bool autosort = region_header_type && ((control & (1u << 29)) == 0);
    const bool flush    = control & (1u << 28);
    const u32 tile_x    = (control >> 2) & 0x3f;
    const u32 tile_y    = (control >> 8) & 0x3f;

    u32 pointers[6];
    pointers[0] = vram_read(ra_addr + 4);
    pointers[1] = vram_read(ra_addr + 8);
    pointers[2] = vram_read(ra_addr + 12);
    pointers[3] = vram_read(ra_addr + 16);
    if (region_header_type) {
      pointers[4] = vram_read(ra_addr + 20);
    }

    printf("Region Array Entry (%u,%u) (%s%s%s)\n",
           tile_x * 32,
           tile_y * 32,
           autosort ? "S" : ".",
           z_clear ? "." : "C",
           flush ? "." : "F");

    for (unsigned list = 0; list < (region_header_type ? 5 : 4); ++list) {
      const bool empty_list = pointers[list] & 0x8000'0000;
      if (empty_list)
        continue;

      printf("  List %u\n", list);

      u32 opb_addr = pointers[list] & ((1u << 24) - 1);
      for (;;) {
        const u32 obj = vram_read(VRAMAddress32 { opb_addr });
        printf("    Object @ 0x%08x : 0x%08x\n", opb_addr, obj);

        if ((obj >> 29) == 0b111) {
          // OPB Link Type
          if (obj & (1u << 28)) {
            // End of list
            break;
          } else {
            // Follow 'next' pointer
            opb_addr = obj & ((1u << 24) - 1);
          }
        }

        else {
          // Otherwise it's some 'normal' kind of object
          opb_addr += sizeof(u32);
        }
      }
    }

    ra_addr += region_header_type ? 24 : 20;
    if (last) {
      // Last region
      break;
    }
  }
}

void
Holly::start_render()
{
  // Read REGION_ARRAY (Check for multiple passes, tile extents, etc.)
  // print_region_array();
  // debug_walk_frame();

  // LIST_INIT was previously called with TA_ISP_BASE set to some value which is
  // where the generated rendering data was stored. Find PARAM_BASE which matches?

  // STATS
  {
    using Metric = zoo::dreamcast::Metric;
    m_console->metrics().increment(Metric::CountRenderObjects, frame_stats.num_objects);
    m_console->metrics().increment(Metric::CountRenderPolygons, frame_stats.num_polygons);
    m_console->metrics().increment(Metric::CountStartRender, 1);
    m_console->metrics().increment(Metric::CountTaFifoBytes, frame_stats.bytes_ta_fifo);
    m_console->metrics().increment(Metric::CountTaYuvBytes, frame_stats.bytes_ta_yuv);
    m_console->metrics().increment(Metric::CountTaTextureBytes, frame_stats.bytes_ta_tex);
  }

  if constexpr (0) {
    printf(
      "Frame (%4u) :: %4u polys, %4u objs, %7u bytes TA FIFO, %7u bytes TA YUV, %7u bytes"
      "TA TEX\n",
      m_gpu_state.start_render_count,
      frame_stats.num_polygons,
      frame_stats.num_objects,
      frame_stats.bytes_ta_fifo,
      frame_stats.bytes_ta_yuv,
      frame_stats.bytes_ta_tex);
  }
  frame_stats = {};

#if 0
  // Update framebuffer memory usage page data
  u32 fb_write_size;
  if (_regs.FB_W_CTRL.raw & 0b111 < 4)
    fb_write_size = 2;
  else if (_regs.FB_W_CTRL.raw & 0b111 == 4)
    fb_write_size = 3;
  else
    fb_write_size = 4;

  // FB WRITE
  const u32 sof1_offset = _regs.FB_W_SOF1.raw & 0x7f'ffff;
  for (u32 pixel_pages = 0; pixel_pages < 640 * 480 * fb_write_size / 128;
       ++pixel_pages) {
    m_console->memory_usage().vram->set(0x0500'0000 + sof1_offset + pixel_pages * 128,
                                        dreamcast::GPU_FrameBufferWrite);
  }

  // FB READ
  const u32 sof1_r_offset = _regs.FB_R_SOF1.raw & 0x7f'ffff;
  for (u32 pixel_pages = 0; pixel_pages < 640 * 480 * fb_write_size / 128;
       ++pixel_pages) {
    m_console->memory_usage().vram->set(0x0500'0000 + sof1_r_offset + pixel_pages * 128,
                                        dreamcast::GPU_FrameBufferRead);
  }
#endif

  // Draw Background

  /////////////////////////////////////////////////////

  if (dump_requested) {
    FILE *f_vram = fopen("vram.dump", "wb");
    for (u32 addr = 0; addr < 8 * 1024 * 1024; addr += 4) {
      const u32 val = vram_read(VRAMAddress64 { addr });
      fwrite(&val, 1, sizeof(val), f_vram);
    }
    fclose(f_vram);

    FILE *freg = fopen("pvr_regs.dump", "wb");
    for (u32 addr = 0x005F8000; addr <= 0x005F9FFC; addr += 4) {
      const u32 val = m_console->memory()->read<u32>(addr);
      fwrite(&val, 1, sizeof(val), freg);
    }
    fclose(freg);

    printf("Wrote VRAM and PVR registers to disk\n");

    // Cancel so we don't dump again until next request
    dump_requested = false;
  }

  ProfileZone;
  // printf("Write to STARTRENDER (PARAM_BASE = 0x%08x REGION_BASE = 0x%08x)\n",
  //        _regs.PARAM_BASE,
  //        _regs.REGION_BASE);

  // Background data is stored in a special place. See "tag address", page 348.
  const u32 vram_offset =
    ((_regs.PARAM_BASE + _regs.ISP_BACKGND_T.tag_address * 4) & (8 * 1024 * 1024 - 1));
  render_background(vram_offset);

  // Add fog information to the frame
  m_current_frame_data->fog_data.fog_color_lookup_table = _regs.FOG_COL_RAM.raw;
  m_current_frame_data->fog_data.fog_color_per_vertex   = _regs.FOG_COL_VERT.raw;
  m_current_frame_data->fog_data.fog_density            = _regs.FOG_DENSITY.raw;
  m_current_frame_data->fog_data.fog_clamp_max          = _regs.FOG_CLAMP_MAX.raw;
  m_current_frame_data->fog_data.fog_clamp_min          = _regs.FOG_CLAMP_MIN.raw;
  m_current_frame_data->fog_table_data.clear();
  for (u32 val : m_gpu_state.fog_table) {
    float float_01 = float(val & 0xFF) / 255.0f;
    m_current_frame_data->fog_table_data.push_back(float_01);
  }

  // Copy palette RAM
  const unsigned palette_color_format = _regs.PAL_RAM_CTRL.raw & 0x3;
  for (unsigned i = 0; i < 1024; ++i) {

    const u16 palette_data = m_gpu_state.palette_ram[i];
    u32 result;
    if (palette_color_format == 0) {
      texture_logic::convert_argb1555(&palette_data, &result);
    } else if (palette_color_format == 1) {
      texture_logic::convert_rgb565(&palette_data, &result);
    } else if (palette_color_format == 2) {
      texture_logic::convert_argb4444(&palette_data, &result);
    } else {
      // unsupported
      result = 0xffff00ff;
    }

    // result = 0xffffffff;

    m_current_frame_data->palette_colors[i] = result;
  }

  // Resolve which texture handles are used in the current frame
  prepare_frame_textures();

  // Push data to the rendering backend
  m_console->texture_manager()->callback_pre_render();
  render_to(&m_console->get_frame_data());

  // New renderer stuf ------------------------------
  if (kNewRendererEnabled) {
    std::vector<u32> vram;
    vram.resize(8 * 1024 * 1024 / sizeof(uint32_t));
    m_console->memory()->dma_read(vram.data(), kVram32BaseAddress, 8 * 1024 * 1024);

    std::vector<u32> vregs;
    vregs.resize(0x4000 / sizeof(uint32_t));
    vregs[0x0020 / 4] = _regs.PARAM_BASE;
    vregs[0x002c / 4] = _regs.REGION_BASE;
    vregs[0x0044 / 4] = _regs.FB_R_CTRL.raw;
    vregs[0x0048 / 4] = _regs.FB_W_CTRL.raw;
    vregs[0x004c / 4] = _regs.FB_W_LINESTRIDE.raw;
    vregs[0x0050 / 4] = _regs.FB_R_SOF1.raw;
    vregs[0x0054 / 4] = _regs.FB_R_SOF2.raw;
    vregs[0x005c / 4] = _regs.FB_R_SIZE.raw;
    vregs[0x0060 / 4] = _regs.FB_W_SOF1.raw;
    vregs[0x0064 / 4] = _regs.FB_W_SOF2.raw;
    vregs[0x0088 / 4] = _regs.ISP_BACKGND_D.raw;
    vregs[0x008c / 4] = _regs.ISP_BACKGND_T.raw;

    const char *fb_pack_names[] = {
      "0555", "565", "4444", "1555", "888", "0888", "8888", "rsvd",
    };
    printf("startrender Linestride = %u bytes FB_W_CTRL mode %s\n",
           _regs.FB_W_LINESTRIDE.raw * 8,
           fb_pack_names[_regs.FB_W_CTRL.raw & 0b111]);

    std::vector<u32> ra_entry_addresses;
    {
      u32 addr   = 0x0500'0000 + _regs.REGION_BASE;
      u32 header = 0;
      do {
        ra_entry_addresses.push_back(addr);
        header = vram_read(VRAMAddress32(addr));
        addr += 4 * 6;
      } while ((header & 0x8000'0000) == 0);
    }

    // m_console->renderer()->render(vram.data(), vregs.data(), ra_entry_addresses);
    m_console->memory()->dma_write(kVram32BaseAddress, vram.data(), 8 * 1024 * 1024);
  }

  m_console->texture_manager()->callback_post_render();

  //////////////////////////////////////

  if (m_renderer) {
    zoo::dreamcast::RendererExecuteContext ctx;
    ctx.render_timestamp = m_console->current_time();
    m_renderer->execute(ctx);
  }

  //////////////////////////////////////

  // Assume rendering takes 10ms. TODO: This is innacurate if multiple renderings are
  // done on a single framebuffer (e.g. with pixel clipping, see pg 126)
  const u64 one_millisecond_in_nanos = 1'000'000;
  m_console->schedule_event(one_millisecond_in_nanos * 5, &m_event_render);

  // Advance frame counter
  m_gpu_state.start_render_count++;
}

std::mutex atomic_mut;
void
atomic(std::function<void()> func)
{
  return;
  std::lock_guard lock(atomic_mut);
  func();
  fflush(stdout);
}

void
Holly::render_to(render::FrameData *target)
{
  std::lock_guard rq_lock(m_rq_lock);
  std::lock_guard frontend_lock(m_console->render_lock());

  const u32 frame_data_number = (_regs.PARAM_BASE >> 20) & 7;
  atomic([&] {
    fmt::print(
      "render_to :: Moving TA FrameData (PARAM_BASE index {}) ({} lists) to SDL\n",
      frame_data_number,
      m_render_frame_data->display_lists.size());
  });
  *target       = std::move(*m_render_frame_data);
  target->dirty = true;
}

void
Holly::reset()
{
  _regs.TA_OL_BASE        = 0x00000000u;
  _regs.TA_OL_LIMIT       = 0x00000000u;
  _regs.TA_ISP_BASE       = 0x00000000u;
  _regs.TA_ISP_LIMIT      = 0x00000000u;
  _regs.TA_LIST_INIT      = 0x00000000u;
  _regs.TA_ITP_CURRENT    = 0x00000000u;
  _regs.TA_NEXT_OPB       = 0x00000000u;
  _regs.TA_NEXT_OPB_INIT  = 0x00000000u;
  _regs.TA_GLOB_TILE_CLIP = 0x00000000u;
  _regs.TA_ALLOC_CTRL.raw = 0x00000000u;
  _regs.FPU_PARAM_CFG     = 0;
  _regs.FPU_CULL_VAL      = 0.f;
  _regs.PARAM_BASE        = 0;
  _regs.REGION_BASE       = 0;

  m_gpu_state          = HollyRenderState();
  m_gpu_state.queue_id = 0;

  m_spg_state                  = { 0 };
  m_spg_state.m_nanos_per_line = 1000000u;

  m_event_spg.cancel();
  m_event_render.cancel();
  m_event_interrupt.cancel();

  m_console->schedule_event(1'000'000, &m_event_spg);

  m_interrupt_queue = {};
}

void
Holly::finish_render()
{
  m_console->interrupt_normal(Interrupts::Normal::EndOfRender_ISP);
  m_console->interrupt_normal(Interrupts::Normal::EndOfRender_TSP);
  m_console->interrupt_normal(Interrupts::Normal::EndOfRender_Video);
}

void
Holly::register_regions(fox::MemoryTable *const memory)
{
  // 0x04000000u, 0x00800000u, "PVR Texture Memory 64bit", texmem, 0x00000000u);

  // Polygon Converter through TA (-/W)
  //   0x1000'0000 - 0x107F'FFFF
  //   0x1200'0000 - 0x127F'FFFF
  //      00x
  // YUV Converter through TA     (-/W)
  //   0x1080'0000 - 0x10FF'FFFF
  //   0x1280'0000 - 0x12FF'FFFF
  //      08x
  // Texture Access through TA    (-/W)
  //   0x1100'0000 - 0x117F'FFFF
  //   0x1300'0000 - 0x137F'FFFF
  //      10x
  // Texture Memory 64-bit through PVR (R/W)
  //   0x0400'0000 - 0x047F'FFFF
  //   0x0600'0000 - 0x067F'FFFF
  //      40x
  // Texture Memory 32-bit through PVR (R/W)
  //   0x0500'0000 - 0x057F'FFFF
  //   0x0700'0000 - 0x077F'FFFF
  //      50x

  memory->map_mmio(0x0400'0000u, 0x0080'0000u, "tex64.0x0400_0000", this);
  memory->map_mmio(0x0600'0000u, 0x0080'0000u, "tex64.0x0600_0000", this);

  memory->map_mmio(
    0x5f8000u, 0x124u, "Graphics Registers (SPG, Framebuffer, Fog Control, etc.)", this);
  memory->map_mmio(0x5f8200u, 0x400u, "Graphics Registers (Fog Data)", this);
  memory->map_mmio(0x5f9000u, 0x1000u, "Graphics Registers (Palette RAM)", this);

  memory->map_mmio(0x1000'0000u, 0x0400'0000u, "Tile Accelerator (Work Area)", this);
  memory->map_mmio(
    0x005F8124u, 0x000000DCu, "Tile Accelerator (Control Registers)", this);
  memory->map_mmio(
    0x005F8600u, 0x00000A00u, "Tile Accelerator (Object List Pointer Data)", this);
}

void
Holly::handle_softreset()
{
  // TODO ?
}

u32
Holly::vram_read(VRAMAddress64 addr)
{
  return vram_read(addr.to32());
}

u32
Holly::vram_read(VRAMAddress32 addr)
{
  return m_console->memory()->read<u32>(kVram32BaseAddress + (addr.get() & 0x7f'ffff));
}

void
Holly::vram_write(VRAMAddress64 addr, u32 value)
{
  vram_write(addr.to32(), value);
}

void
Holly::vram_write(VRAMAddress32 addr, u32 value)
{
  m_console->memory()->write<u32>(kVram32BaseAddress + (addr.get() & 0x7f'ffff), value);
}

void
Holly::render_background(u32 vram_offset)
{
  atomic([&] { printf("RenderBackground : TA_ISP_BASE == %x\n", _regs.TA_ISP_BASE); });

  u32 data_u32[32];
  for (int i = 0; i < 32; ++i) {
    //  m_console->cpu()->mem_read<u32>(vram_addr + 4 * i);
    data_u32[i] = vram_read(VRAMAddress32 { vram_offset + 4 * i });
  }

  auto isp = *(ta_isp_word *)&data_u32[0];
  auto tsp = *(ta_tsp_word *)&data_u32[1];

  // Need to fake global parameter word
  ta_param_word global;
  global.type      = ta_para_type::Polygon;
  global.list_type = ta_list_type::Opaque;
  global.col_type  = ta_col_type::Packed;
  global.offset    = isp.opaque_or_translucent.offset;
  global.gouraud   = isp.opaque_or_translucent.gouraud;
  global.texture   = isp.opaque_or_translucent.texture;
  global.uv16      = isp.opaque_or_translucent.uv16;

  tsp.use_alpha = false;
  tsp.src_alpha = 1;
  tsp.dst_alpha = 0;

  // TODO : Handle texture

  if (!global.offset && !global.texture) {
    Vec3f pos_a(reinterpret<f32>(data_u32[3]),
                reinterpret<f32>(data_u32[4]),
                reinterpret<f32>(data_u32[5]));
    Vec4f col_a = packed_color_argb_to_vec4(data_u32[6]);

    Vec3f pos_b(reinterpret<f32>(data_u32[7]),
                reinterpret<f32>(data_u32[8]),
                reinterpret<f32>(data_u32[9]));
    Vec4f col_b = packed_color_argb_to_vec4(data_u32[10]);

    Vec3f pos_c(reinterpret<f32>(data_u32[11]),
                reinterpret<f32>(data_u32[12]),
                reinterpret<f32>(data_u32[13]));
    Vec4f col_c = packed_color_argb_to_vec4(data_u32[14]);

    Vec3f pos_d(pos_c.x + (pos_b.x - pos_a.x),
                pos_c.y + (pos_b.y - pos_a.y),
                pos_c.z + (pos_b.z - pos_a.z));
    Vec4f col_d = col_c; // TODO ?

    float bg_depth = _regs.ISP_BACKGND_D.depth;
    pos_a.z        = bg_depth;
    pos_b.z        = bg_depth;
    pos_c.z        = bg_depth;
    pos_d.z        = bg_depth;

    render::Vertex vertex_a { .position     = pos_a,
                              .uv           = { 0, 0 },
                              .base_color   = col_a,
                              .offset_color = { 0, 0, 0, 0 } };
    render::Vertex vertex_b { .position     = pos_b,
                              .uv           = { 0, 0 },
                              .base_color   = col_b,
                              .offset_color = { 0, 0, 0, 0 } };
    render::Vertex vertex_c { .position     = pos_c,
                              .uv           = { 0, 0 },
                              .base_color   = col_c,
                              .offset_color = { 0, 0, 0, 0 } };
    render::Vertex vertex_d { .position     = pos_d,
                              .uv           = { 0, 0 },
                              .base_color   = col_d,
                              .offset_color = { 0, 0, 0, 0 } };

    m_current_frame_data->background.isp_word           = isp;
    m_current_frame_data->background.param_control_word = global;
    m_current_frame_data->background.tsp_word           = tsp;

    m_current_frame_data->background.triangles.push_back(
      render::Triangle { vertex_a, vertex_b, vertex_c });
    m_current_frame_data->background.triangles.push_back(
      render::Triangle { vertex_c, vertex_d, vertex_b });

  } else {
    printf("Unsupported BG mode (Probably uses texturing)\n");
  }
}

u32
Holly::ta_get_list_opb_slot_count(ta_list_type list_num)
{
  const u32 alloc_enum = (_regs.TA_ALLOC_CTRL.raw >> (4 * list_num)) & 0x3;
  const u32 sizes[4]   = { 0, 8, 16, 32 };
  return sizes[alloc_enum];
}

void
Holly::ta_begin_list_type(ta_list_type list_type)
{
  if (m_gpu_state.current_list_type != ta_list_type::Undefined) {
    log.warn("TA_BEGIN_LIST_TYPE invoked while a list is already in progress");
    return;
  }

  // Make sure there is no ongoing triangle strip
  ta_list_flush_triangle_strip();

  if (_ta_state.list_opb_sizes[list_type] == 0) {
    log.error(
      "TA_BEGIN_LIST_TYPE invoked for a list type that is not setup in TA_ALLOC_CTRL!");
    return;
  }

  // All tile pointers start pointing the beginning object list area for this list type
  u32 addr = _ta_state.list_start_addresses[list_type];
  for (unsigned i = 0; i < _ta_state.num_tiles_total; ++i) {
    _ta_state.tile_opb_addr[i] = addr; // begins here
    _ta_state.tile_opb_slot[i] = 0;    // and we're pointing at slot 0
    addr += _ta_state.list_opb_sizes[list_type];
  }

  m_time_list_start                = m_console->current_time();
  m_gpu_state.current_list_type    = list_type;
  _ta_state.current_tristrip_count = 0;
}

void
Holly::ta_list_init()
{
  log.info("TA_LIST_INIT invoked");

  // Initialize internal registers
  _regs.TA_NEXT_OPB = _regs.TA_NEXT_OPB_INIT;

  // Initialize Initial OPB Area
  _ta_state.num_tiles_x     = ((_regs.TA_GLOB_TILE_CLIP >> 0) & 0x3f) + 1;
  _ta_state.num_tiles_y     = ((_regs.TA_GLOB_TILE_CLIP >> 16) & 0x0f) + 1;
  _ta_state.num_tiles_total = _ta_state.num_tiles_x * _ta_state.num_tiles_y;

  u32 addr = _regs.TA_OL_BASE;
  for (u32 list_num = ta_list_type::Opaque; list_num <= ta_list_type::PunchThrough;
       ++list_num) {

    const u32 list_opb_size =
      ta_get_list_opb_slot_count(ta_list_type(list_num)) * sizeof(uint32_t);

    // When TA_ALLOC_CTRL has zero for an OPB size for a list type, literally no
    // OPBs will be allocated for that list type in memory
    if (list_opb_size == 0)
      continue;

    _ta_state.list_opb_sizes[list_num]       = list_opb_size;
    _ta_state.list_start_addresses[list_num] = addr;

    const u32 kEndOfList = 0xf000'0000;
    for (unsigned tile_num = 0; tile_num < _ta_state.num_tiles_total; ++tile_num) {

      // log.verbose("TA list=%u tile=%03u opb_start=0x%06x opb_size=%u",
      //             list_num,
      //             tile_num,
      //             addr,
      //             list_opb_size);

      // Fill the entire OPB with end-of-list elements
      if (kNewRendererEnabled)
        for (unsigned i = 0; i < list_opb_size / sizeof(uint32_t); ++i) {
          vram_write(VRAMAddress32 { addr }, kEndOfList);
          addr += 4;
        }
    }
  }
  log.debug("OL Initialized, ended at 0x%08x", addr);

  _regs.TA_ITP_CURRENT          = _regs.TA_ISP_BASE;
  _regs.TA_NEXT_OPB             = _regs.TA_NEXT_OPB_INIT;
  m_gpu_state.current_list_type = ta_list_type::Undefined;
}

u32
Holly::ta_read_current_opb_slot(unsigned tile)
{
  const u32 vram_addr = ta_get_opb_slot_address(tile);
  return vram_read(VRAMAddress32 { vram_addr });
}

void
Holly::ta_next_opb_slot(unsigned tile)
{
  if (!kNewRendererEnabled)
    return;

  const unsigned opb_slot_count =
    ta_get_list_opb_slot_count(m_gpu_state.current_list_type);

  // If the entry is "empty" and we're not at the end of the list, then we don't
  // need to do anything. This OPB slot is valid for use
  if (_ta_state.tile_opb_slot[tile] != opb_slot_count - 1 &&
      ta_read_current_opb_slot(tile) == 0xf000'0000) {
    return;
  }

  // Advance to the next slot
  _ta_state.tile_opb_slot[tile]++;

  // Check what's there
  const u32 vram_addr = ta_get_opb_slot_address(tile);
  // const u32 obj       = vram_read(VRAMAddress32 { vram_addr });

  // If this would be the last slot on the list, we have to make a new OPB and point to it
  if (_ta_state.tile_opb_slot[tile] == opb_slot_count - 1) {
    const u32 current_opb_size = _ta_state.list_opb_sizes[m_gpu_state.current_list_type];

    const bool opb_down = (_regs.TA_ALLOC_CTRL.raw & 0x100000);
    if (opb_down) {

      // Check if TA_OL_LIMIT reached
      if (_regs.TA_OL_LIMIT == _regs.TA_NEXT_OPB) {
        log.error("TA_NEXT_OPB reached TA_OL_LIMIT");
        m_console->interrupt_error(Interrupts::Error::TA_ObjectListPointerOverflow);
        return;
      }

      // When OPB addresses grow downward, TA_NEXT_OPB is pointing at the most recently
      // allocated block, so we need to decrement first
      _regs.TA_NEXT_OPB -= current_opb_size;

      // Write in this last current OPB slot the new OPB address entry
      const u32 addr_mask  = 0x00ff'ffff;
      const u32 block_link = 0xe000'0000 | (_regs.TA_NEXT_OPB & addr_mask);
      vram_write(VRAMAddress32 { vram_addr }, block_link);

      // Clear the new OPB
      for (unsigned slot = 0; slot < opb_slot_count; ++slot)
        vram_write(VRAMAddress32 { u32(_regs.TA_NEXT_OPB + slot * sizeof(uint32_t)) },
                   0xf000'0000);

      // Now we point this tile to the new OPB
      _ta_state.tile_opb_addr[tile] = _regs.TA_NEXT_OPB;
      _ta_state.tile_opb_slot[tile] = 0;
    } else {
      // When OPB addresses grow upward, TA_OPB_NEXT is already pointing at the next block
      // to be used.

      if (_regs.TA_NEXT_OPB >= _regs.TA_OL_LIMIT) {
        log.error("TA_NEXT_OPB reached TA_OL_LIMIT");
        m_console->interrupt_error(Interrupts::Error::TA_ObjectListPointerOverflow);
        return;
      }

      // Write in this last current OPB slot the new OPB address entry
      const u32 addr_mask  = 0x00ff'ffff;
      const u32 block_link = 0xe000'0000 | (_regs.TA_NEXT_OPB & addr_mask);
      vram_write(VRAMAddress32 { vram_addr }, block_link);

      // Now we point this tile to the new OPB
      _ta_state.tile_opb_addr[tile] = _regs.TA_NEXT_OPB;
      _ta_state.tile_opb_slot[tile] = 0;

      // Clear the new OPB
      for (unsigned slot = 0; slot < opb_slot_count; ++slot)
        vram_write(VRAMAddress32 { u32(_regs.TA_NEXT_OPB + slot * sizeof(uint32_t)) },
                   0xf000'0000);

      _regs.TA_NEXT_OPB += current_opb_size;
    }
  }
}

u32
Holly::ta_get_opb_slot_address(unsigned tile)
{
  // XXX : Is opb_size not actually needed?
  // const u32 opb_size = _ta_state.list_opb_sizes[m_gpu_state.current_list_type];
  return _ta_state.tile_opb_addr[tile] + _ta_state.tile_opb_slot[tile] * sizeof(uint32_t);
}

void
Holly::ta_triangle_strip_vertex_append()
{
  _ta_state.current_tristrip_count++;
  const unsigned current_triangles = std::max(0u, _ta_state.current_tristrip_count - 2);

  // If there is no triangle yet, we can just return for now.
  if (current_triangles == 0) {
    return;
  }

  float tri_x_min = std::min({ _ta_state.strip_vertices[0].x,
                               _ta_state.strip_vertices[1].x,
                               _ta_state.strip_vertices[2].x });
  float tri_x_max = std::max({ _ta_state.strip_vertices[0].x,
                               _ta_state.strip_vertices[1].x,
                               _ta_state.strip_vertices[2].x });

  float tri_y_min = std::min({ _ta_state.strip_vertices[0].y,
                               _ta_state.strip_vertices[1].y,
                               _ta_state.strip_vertices[2].y });
  float tri_y_max = std::max({ _ta_state.strip_vertices[0].y,
                               _ta_state.strip_vertices[1].y,
                               _ta_state.strip_vertices[2].y });

  // Check every tile to see if this triangle is a part of it
  // TODO : only iterate tiles that overlap with the triangle bounding box

  if (kNewRendererEnabled)
    for (unsigned tile = 0; tile < _ta_state.num_tiles_total; ++tile) {
      const float tile_x_min = (tile % _ta_state.num_tiles_x) * 32;
      const float tile_x_max = tile_x_min + 32;
      const float tile_y_min = (tile / _ta_state.num_tiles_x) * 32;
      const float tile_y_max = tile_y_min + 32;

      // Check if the triangle bounding box is outside the tile
      if (tri_x_min >= tile_x_max || tri_x_max < tile_x_min || tri_y_min >= tile_y_max ||
          tri_y_max <= tile_y_min) {
        continue;
      }

      // Triangle overlaps, add to the OPB
      u32 obj = ta_read_current_opb_slot(tile);

      // Check if the current OPB object slot is for this triangle strip
      if ((obj & 0x001f'ffff) != _ta_state.current_tristrip_isp_base) {
        // We were not pointing at the right strip object, need to allocate a new one
        ta_next_opb_slot(tile);
        obj = _ta_state.current_tristrip_isp_base & 0x001f'ffff; // offset into PARAM_BASE

        u32 skip_index = 0;
        if (_ta_state.globals_pcw.texture)
          skip_index += 4;
        if (_ta_state.globals_pcw.offset)
          skip_index += 2;
        if (_ta_state.globals_pcw.uv16)
          skip_index += 1;

        const u32 skip_values[] = {
          0b001, 0b001, 0b001, 0b001, 0b011, 0b010, 0b100, 0b011
        };
        obj |= skip_values[skip_index] << 21;
      }

      // Update the mask for this ongoing triangle strip. (t0=bit30 -> t5=bit25)
      obj |= 1u << (30 - (current_triangles - 1));

      // printf("appended triangle to tile %u, obj was 0x%08x now 0x%08x did %u\n", tile,
      // obj_before, obj, did);
      vram_write(VRAMAddress32 { ta_get_opb_slot_address(tile) }, obj);
    }

  // Everything from here and below is basically just to figure out if the object entry
  // needs to be closed.

  // The object can only represent up to 6 triangles (or even less if configured).
  // Additionally, if this ISP strip is done, but the actual triangle strip being
  // input to the TA is /not/ done, then we will have to start a new set of ISP
  // params.
  const unsigned kStripLengths[] = { 1, 2, 4, 6 };
  const unsigned striplen        = kStripLengths[_ta_state.globals_pcw.strip_len];
  const unsigned striplen_met    = current_triangles == striplen;
  if (current_triangles == 6 || striplen_met) {
    ta_list_flush_triangle_strip();

    // Need to make the last two vertices the first two of the next strip
    _ta_state.current_tristrip_count = 2;
  }
}

void
Holly::ta_list_flush_triangle_strip()
{
  // This function is used to complete an ongoing Object in the OPB for a triangle
  // strip (assuming one is in progress). After this is called, the current Object
  // is considered complete and the next Object can be started in the OPB.
  if (_ta_state.current_tristrip_count == 0)
    return;

  log.info("Tile has %u triangles in strip", _ta_state.current_tristrip_count);

  // If the triangle strip being input to the TA is actually not complete, we need
  // to start a new set of object parameters for the next ISP strip.

  // _ta_state.current_tristrip_isp_base
  const u32 new_strip_isp_base = ta_params_append(_ta_state.globals_isp.raw);
  ta_params_append(_ta_state.globals_tsp.raw);
  ta_params_append(_ta_state.globals_tex.raw);
  _ta_state.current_tristrip_isp_base = new_strip_isp_base;

  // Reset strip counter
  _ta_state.current_tristrip_count = 0;
}

u16
Holly::read_u16(u32 address)
{
  u16 value = 0;

  // 8MiB area mirrored in two locations (0x0400'0000 and 0x0600'0000)
  address &= ~u32(0x0200'0000);
  const bool read_to_64b_area = (address >= 0x0400'0000 && address <= 0x047F'FFFF);
  if (read_to_64b_area) {
    const u32 vram_32b_aligned_offset = address & 0x7f'fffc;
    const u32 val32 = vram_read(VRAMAddress64 { vram_32b_aligned_offset });
    value           = (val32 >> ((address & 2) * 8)) & 0xFFFF;
  } else {
    throw std::runtime_error("Unimplemented read_u32");
  }

  return value;
}

void
Holly::write_u16(u32 address, u16 value)
{
  // 8MiB area mirrored in two locations (0x0400'0000 and 0x0600'0000)
  address &= ~u32(0x0200'0000);
  const bool is_64b_area = (address >= 0x0400'0000 && address <= 0x047F'FFFF);
  if (is_64b_area) {

    // Calculate 4B-aligned address
    const VRAMAddress64 vram_addr { address & 0x7f'fffc };
    u32 addr32 = vram_addr.to32().get();

    // We're pointing at the correct 32b word now. Need to potentially walk forward
    // 16b to get to the correct half-word.
    if (address & 2) {
      addr32 += 2;
    }

    m_console->memory()->write<u16>(kVram32BaseAddress + addr32, value);

  } else {
    throw std::runtime_error("Unimplemented write_u16");
  }
}

u32
Holly::read_u32(u32 address)
{
#define READ(_name)                                                                      \
  case _name ::address: {                                                                \
    const u32 val = _regs._name.raw;                                                     \
    std::string reg_name;                                                                \
    if (is_register(address | 0xa000'0000, reg_name)) {                                  \
      log.verbose("Read from '%s' (0x%08x) returning value 0x%08x",                      \
                  reg_name.c_str(),                                                      \
                  address,                                                               \
                  val);                                                                  \
    }                                                                                    \
    return val;                                                                          \
  }

#define READ_WARN(_name, message)                                                        \
  case _name ::address:                                                                  \
    log.warn("Read from '%s'. Warning: '%s'", #_name, message);                          \
    return _regs._name.raw;

#define READ_COMPLEX(_name)                                                              \
  case _name ::address:                                                                  \
    return _regs._name.raw;

#define READ_ATOMIC(_name)                                                               \
  case _name ::address:                                                                  \
    return _regs._name.load();

  ///////////////////////////////////////////

  if (address >= 0x0400'0000 && address <= 0x047F'FFFF) {
    const u32 offset = address & 0x007F'FFFF;
    return vram_read(VRAMAddress64 { offset });
  }

  if (address >= 0x0600'0000 && address <= 0x067F'FFFF) {
    const u32 offset = address & 0x007F'FFFF;
    return vram_read(VRAMAddress64 { offset });
  }

  if (address >= 0x005F8200 && address <= 0x005F83FC) {
    const u32 index = (address - 0x005F8200) / 4;
    return m_gpu_state.fog_table[index];
  }

  switch (address) {
    READ(DEVICE_ID);
    READ(DEVICE_REVISION);

    case PARAM_BASE::address:
      return _regs.PARAM_BASE;

    case REGION_BASE::address:
      return _regs.REGION_BASE;

      READ(SPG_HBLANK);
      READ(SPG_VBLANK);
      READ(SPG_HBLANK_INT);
      READ(SPG_VBLANK_INT);
      READ(SPG_CONTROL);
      READ(SPG_LOAD);
      READ(SPG_WIDTH);
      READ(SPG_STATUS);

      READ(SDRAM_CFG);
      READ(SDRAM_REFRESH);

      READ(SOFTRESET);

      READ(VO_CONTROL);
      READ(VO_STARTX);
      READ(VO_STARTY);
      READ(SCALER_CTL);

      READ(FOG_COL_RAM);
      READ(FOG_COL_VERT);
      READ(FOG_DENSITY);
      READ(FOG_CLAMP_MAX);
      READ(FOG_CLAMP_MIN);

      READ(VO_BORDER_COLOR);

      READ_WARN(FB_R_CTRL, "Framebuffer read logic not implemented");
      READ(FB_R_SOF1);
      READ(FB_R_SOF2);

      READ(PAL_RAM_CTRL);

      READ(FB_W_CTRL);
      READ(FB_W_SOF1);
      READ(FB_W_SOF2);
      READ(FB_W_LINESTRIDE);

    case TA_NEXT_OPB::address:
      return _regs.TA_NEXT_OPB;

    case TA_LIST_CONT::address:
      return 0;

    case TA_LIST_INIT::address:
      return _regs.TA_LIST_INIT;

      READ_ATOMIC(TA_ITP_CURRENT);
      READ_COMPLEX(TA_ALLOC_CTRL);

    case TA_YUV_TEX_CNT::address:
      printf("TA_YUV_CNT read will return 0x%x\n", _regs.TA_YUV_TEX_CNT);
      return _regs.TA_YUV_TEX_CNT;

    default: {
      const auto it = graphics_registers.find(address);
      if (it != graphics_registers.end()) {
        log.warn("Unhandled u32 read from Graphics Register \"%s\"", it->second);
        printf("Unhandled u32 read from Graphics Register \"%s\"\n", it->second);
      } else {
        log.warn("Unhandled u32 read from unlabeled Graphics Register @0x%08x", address);
        printf("Unhandled u32 read from unlabeled Graphics Register @0x%08x\n", address);
      }
      return 0u;
    }
  }
}

#define WRITE(_name)                                                                     \
  case _name ::address:                                                                  \
    _regs._name.raw = val;                                                               \
    break;

#define WRITE_WARN(_name, _message)                                                      \
  case _name ::address:                                                                  \
    _regs._name.raw = val;                                                               \
    log.warn("Write of value 0x%08X -> '%s' (0x%08X). Warning: '%s'",                    \
             val,                                                                        \
             #_name,                                                                     \
             _name ::address,                                                            \
             _message);                                                                  \
    break;

void
Holly::write_u32(u32 address, u32 val)
{
  ProfileZone;

  ///////////??///////////////

  std::string reg_name;
  if (is_register(address | 0xa000'0000, reg_name)) {
    log.info("Write to '%s' (0x%08x) with value 0x%08x", reg_name.c_str(), address, val);
  }

  // 0x0400'0000u, 0x0080'0000u
  if (address >= 0x0400'0000 && address <= 0x047F'FFFF) {
    const u32 offset = address & 0x007F'FFFF;
    frame_stats.bytes_ta_tex += 4;
    vram_write(VRAMAddress64 { offset }, val);
    return;
  }

  //   0x0600'0000 - 0x077F'FFFF
  if (address >= 0x0600'0000 && address <= 0x067F'FFFF) {
    const u32 offset = address & 0x007F'FFFF;
    frame_stats.bytes_ta_tex += 4;
    vram_write(VRAMAddress64 { offset }, val);
    return;
  }

  if (address >= 0x005F8200 && address <= 0x005F83FC) {
    const u32 index              = (address - 0x005F8200) / 4;
    m_gpu_state.fog_table[index] = val;
    return;
  }

  if (address >= 0x005F9000 && address <= 0x005F9FFC) {
    // Need to handle DMA ?
    const u32 palette_index = (address - 0x005F9000) / sizeof(u32);
    const u32 old_value     = m_gpu_state.palette_ram[palette_index];
    if (val != old_value) {
      m_gpu_state.palette_ram[palette_index] = val;
    }
    return;
  }

  switch (address) {
    case REGION_BASE::address:
      _regs.REGION_BASE = val & 0x7f'fffc;
      break;

    case FPU_PARAM_CFG::address:
      _regs.FPU_PARAM_CFG = val;
      break;

    case FPU_CULL_VAL::address:
      _regs.FPU_CULL_VAL = reinterpret<float>(val & 0x7FFFFFFF);
      break;

    case STARTRENDER::address: {
      start_render();
      return;
    }

      WRITE(SPG_HBLANK);
      WRITE(SPG_VBLANK);

    case FB_R_SIZE::address:
      _regs.FB_R_SIZE.raw = val;
      break;

    case SPG_CONTROL::address:
      _regs.SPG_CONTROL.raw = val;
      recalculate_line_timing();
      break;

    case SPG_LOAD::address:
      _regs.SPG_LOAD.raw = val;
      recalculate_line_timing();
      break;

      WRITE(SPG_WIDTH);
      WRITE(SPG_HBLANK_INT);
      WRITE(SPG_VBLANK_INT);
      WRITE(VO_CONTROL);

    // TODO : What should we actually reset when SDRAM/Pipeline/TA are reset?
    case SOFTRESET::address:
      _regs.SOFTRESET.raw = val;
      handle_softreset();
      break;

      WRITE(FB_R_SOF1);
      WRITE(FB_R_SOF2);

      WRITE(FB_W_CTRL);
      WRITE(FB_W_SOF1);
      WRITE(FB_W_SOF2);
      WRITE(FB_W_LINESTRIDE);

    case FB_R_CTRL::address:
      if ((val >> 22) & 1) {
        printf("warning: no support for strip buffers\n");
      }
      _regs.FB_R_CTRL.raw = val;
      recalculate_line_timing();
      break;

      WRITE(SDRAM_CFG);
      WRITE(SDRAM_REFRESH);

      WRITE(VO_BORDER_COLOR);

      // TODO : ~STARTX or STARTY time into line/frame, trigger the interrupts for better
      // timing.
      WRITE_WARN(VO_STARTX, "Starting X logic not implemented");
      WRITE_WARN(VO_STARTY, "Starting Y logic not implemented");

    case SCALER_CTL::address:
      _regs.SCALER_CTL.raw = val;
      break;

    case 0x005F8088: {
      _regs.ISP_BACKGND_D.raw = val & ~0xF;
      break;
    }

    // TODO : Implement PT_ALPHA_REF
    case 0x005F811C: {
      // float val_f32 = (val & 0xFF) * (1.0 / 255.0f);
      log.error("PT_ALPHA_REF is not implemented");
      break;
    }

    case 0x005F808C: {
      _regs.ISP_BACKGND_T.raw = val;
      break;
    }

    case PARAM_BASE::address: {
      _regs.PARAM_BASE    = val & 0xF00000;
      m_render_frame_data = m_frame_data[(_regs.PARAM_BASE >> 20) & 7];
      break;
    }

    case PAL_RAM_CTRL::address:
      _regs.PAL_RAM_CTRL.raw = val & 0b11;
      break;

    case TA_LIST_INIT::address:
      if (val & 0x80000000u) {
        ta_list_init();
      }
      break;

    case TA_LIST_CONT::address:
      if (val & 0x80000000) {
        log.verbose("TA_LIST_CONT triggered");
        m_gpu_state.current_list_type = ta_list_type::Undefined;
      }
      break;

    case TA_ALLOC_CTRL::address: {
      _regs.TA_ALLOC_CTRL.raw = val;
      return;
    }

    case TA_OL_BASE::address:
      _regs.TA_OL_BASE = val;
      break;

    case TA_ISP_BASE::address: {
      _regs.TA_ISP_BASE = val;

      // TA_ISP_BASE is in 1MB blocks, and there are only 8MB of texture memmory.
      m_current_frame_data               = m_frame_data[(_regs.TA_ISP_BASE >> 20) & 7];
      m_current_frame_data->frame_number = m_spg_state.m_vblank_in_count;

      atomic([&] { printf(" - Internal List number set to  0\n"); });
      m_gpu_state.list_number = 0;

      break;
    }

    case TA_YUV_TEX_BASE::address:
      _regs.TA_YUV_TEX_BASE                  = 0x00FFFFF8 & val;
      _regs.TA_YUV_TEX_CNT                   = 0;
      m_gpu_state.yuv_converter_bytes_so_far = 0;
      break;

    case TA_YUV_TEX_CTRL::address: {
      auto &reg(_regs.TA_YUV_TEX_CTRL);
      reg.raw = val;
      break;
    }

    case TEXT_CONTROL::address:
      _regs.TEXT_CONTROL.raw = val;
      break;

    // TODO : Implement limit interrupt logic. There is a doc page that describes the full
    // behavior with CFG.
    case TA_OL_LIMIT::address:
      _regs.TA_OL_LIMIT = val;
      break;

    case TA_ISP_LIMIT::address:
      _regs.TA_ISP_LIMIT = val;
      break;

    case TA_NEXT_OPB_INIT::address:
      _regs.TA_NEXT_OPB_INIT = val;
      break;

    case TA_GLOB_TILE_CLIP::address:
      _regs.TA_GLOB_TILE_CLIP = val;
      break;

    case FOG_COL_RAM::address:
      _regs.FOG_COL_RAM.raw = val & 0x00FFFFFF;
      break;

    case FOG_COL_VERT::address:
      _regs.FOG_COL_VERT.raw = val & 0x00FFFFFF;
      break;

    case FOG_DENSITY::address:
      _regs.FOG_DENSITY.raw = val & 0x0000FFFF;
      break;

    case FOG_CLAMP_MAX::address:
      _regs.FOG_CLAMP_MAX.raw = val;
      break;

    case FOG_CLAMP_MIN::address:
      _regs.FOG_CLAMP_MIN.raw = val;
      break;

    default:
      // log.info("Unhandled write to Graphics Register @0x%08x <- 0x%08x", address, val);
      break;
  }
}

void
Holly::write_u64(u32 addr, u64 val)
{
  // Some BIOS/Games will directly write 64b words to VRAM, for example the opening
  // studio logos in Re-Volt. It's sufficient to treat this as two 32b writes at
  // this layer.
  write_u32(addr, val & 0xFFFFFFFF);
  write_u32(addr + 4, val >> 32);
}

void
Holly::recalculate_line_timing()
{
  // Compute pixel clock and line nanos based on _regs.
  // The base clock is 135Mhz / 2.
  u64 pixel_clock_freq = 13500000;

  // Scale PCLK = (VCLK / 2) or (VCLK)
  if (_regs.FB_R_CTRL.vclk_div)
    pixel_clock_freq *= 2;

  // SPG_LOAD : "Specify 'number of video clock cycles per line - 1' for the CRT."
  u64 line_vclk_hz = pixel_clock_freq / (_regs.SPG_LOAD.hcount + 1);

  // If we are in interlace mode, we run through two lines in the time normally taken for
  // one.
  if (_regs.SPG_CONTROL.interlace)
    line_vclk_hz *= 2;

  m_spg_state.m_nanos_per_line = 1000000000ll / line_vclk_hz;
}

i64
Holly::get_nanos_per_line()
{
  return m_spg_state.m_nanos_per_line;
}

void
Holly::step_spg_line()
{
  // 1. Increment line once (or twice depending on if we're in interlaced)
  m_spg_state.m_current_line++;

  if (m_spg_state.m_current_line >= (u32)(_regs.SPG_LOAD.vcount + 1)) {
    m_spg_state.m_current_line = 0;
    if (_regs.SPG_CONTROL.interlace) {
      _regs.SPG_STATUS.fieldnum = 1u - _regs.SPG_STATUS.fieldnum;
    }
  }

  // 2. Fire HBlank interrupts if configured.
  if (_regs.SPG_HBLANK_INT.hblank_int_mode == 0) {
    if (m_spg_state.m_current_line == _regs.SPG_HBLANK_INT.linecomp_val) {
      m_console->interrupt_normal(Interrupts::Normal::HBlankIn);
    }
  } else if (_regs.SPG_HBLANK_INT.hblank_int_mode == 1) {
    if (m_spg_state.m_current_line % _regs.SPG_HBLANK_INT.linecomp_val == 0) {
      m_console->interrupt_normal(Interrupts::Normal::HBlankIn);
    }
  } else if (_regs.SPG_HBLANK_INT.hblank_int_mode == 2) {
    m_console->interrupt_normal(Interrupts::Normal::HBlankIn);
  } else {
    throw std::runtime_error(
      "HBLank Interrupt mode is set to a reserved mode. This shouldn't happen.");
  }

  // 3. Figure Vblank in/out interrupts as configured.
  bool in_vblank_area;
  if (_regs.SPG_VBLANK.vbstart < _regs.SPG_VBLANK.vbend) {
    in_vblank_area = (m_spg_state.m_current_line >= _regs.SPG_VBLANK.vbstart);
    in_vblank_area &= (m_spg_state.m_current_line < _regs.SPG_VBLANK.vbend);
  } else {
    in_vblank_area = (m_spg_state.m_current_line <= _regs.SPG_VBLANK.vbend);
    in_vblank_area |= (m_spg_state.m_current_line > _regs.SPG_VBLANK.vbstart);
  }

  _regs.SPG_STATUS.vsync    = in_vblank_area ? 1 : 0;
  _regs.SPG_STATUS.hsync    = 0;
  _regs.SPG_STATUS.scanline = m_spg_state.m_current_line;

  // Fire interrupt for start of VBlank
  if (m_spg_state.m_current_line == _regs.SPG_VBLANK_INT.vbstart_int) {
    ProfileZoneNamed("vb_start");
    // log.info("VBlank Period Start");
    m_console->interrupt_normal(Interrupts::Normal::VBlankIn);
    m_console->metrics().increment(zoo::dreamcast::Metric::CountGuestVsync, 1);
    m_vblank_in_nanos = m_console->current_time();

    if (const auto callback = m_console->get_vblank_in_callback(); callback) {
      callback();
    }
    m_spg_state.m_vblank_in_count++;
  }

  // Fire interrupt for end of VBlank
  if (m_spg_state.m_current_line == _regs.SPG_VBLANK_INT.vbend_int) {
    log.info("VBlank Period End");
    m_console->trace_zone(
      "vblank", TraceTrack::SPG, m_vblank_in_nanos, m_console->current_time());
    m_console->interrupt_normal(Interrupts::Normal::VBlankOut);
  }

  // Schedule next execution of this function.
  m_console->schedule_event(get_nanos_per_line(), &m_event_spg);
}

u32
Holly::get_vblank_in_count() const
{
  return m_spg_state.m_vblank_in_count;
}

void
Holly::serialize(serialization::Snapshot &snapshot)
{
  const u8 *const vram = m_console->memory()->root() + kVram32BaseAddress;
  snapshot.add_range("vram32", kVram32BaseAddress, 8 * 1024 * 1024, vram);

  static_assert(sizeof(_regs) == 212);
  snapshot.add_range("holly.registers", sizeof(_regs), &_regs);

  static_assert(sizeof(m_gpu_state) == 5564);
  snapshot.add_range("holly.gpu", sizeof(m_gpu_state), &m_gpu_state);

  static_assert(sizeof(m_spg_state) == 24);
  snapshot.add_range("holly.spg", sizeof(m_spg_state), &m_spg_state);

  static_assert(sizeof(_ta_state) == 3328);
  snapshot.add_range("holly.ta_state", sizeof(_ta_state), &_ta_state);  

  m_event_spg.serialize(snapshot);
  m_event_render.serialize(snapshot);
  m_event_interrupt.serialize(snapshot);
}

void
Holly::deserialize(const serialization::Snapshot &snapshot)
{
  snapshot.apply_all_ranges("vram32", [&](const serialization::Storage::Range *range) {
    m_console->memory()->dma_write(kVram32BaseAddress, range->data, range->length);
  });

  snapshot.apply_all_ranges("holly.registers", &_regs);
  snapshot.apply_all_ranges("holly.gpu", &m_gpu_state);
  snapshot.apply_all_ranges("holly.spg", &m_spg_state);
  snapshot.apply_all_ranges("holly.ta_state", &_ta_state);  

  m_event_spg.deserialize(snapshot);
  m_event_render.deserialize(snapshot);
  m_event_interrupt.deserialize(snapshot);
}

u32
Holly::vec4f_color_to_packed(Vec4f in)
{
  const auto scale = [](float x) {
    return uint32_t(std::clamp(x, 0.0f, 1.0f) * 255);
  };

  // Packed color is in ARGB
  uint32_t result = 0;
  result |= scale(in.w) << 24;
  result |= scale(in.x) << 16;
  result |= scale(in.y) << 8;
  result |= scale(in.z) << 0;
  return result;
}

void
Holly::handle_polygon_dma(u32 addr, u32 length, const uint8_t *src)
{
  const ta_param_word *const control_word = (const ta_param_word *)src;

  // Read data to current spot in buffer.
  memcpy(&m_gpu_state.dma_buffer[m_gpu_state.current_buffer_size],
         src,
         32); // Should always be 32 bytes

  // Check if we're finishing the second part of a 64-byte transfer
  if (m_gpu_state.current_buffer_size == 32) {
    handle_dma_data(m_gpu_state.dma_buffer, 64);
    m_gpu_state.current_buffer_size = 0;
    return;
  }

  // Is this a 32 byte or 64 byte transfer? Most are 32, so let's assume that by default
  bool is_32byte_transfer = true;

  // For a description of what is a 32byte vs 64-byte transfer, see the format
  // descriptions, and table in section 3.7.6.2. Examples begin in section 3.7.5.2

  // GLOBAL PARAMETERS
  if (control_word->type == ta_para_type::Polygon /* or volume */) {
    // IntensityTwo is always a 32byte transfer: It's purpose is shorter DMA.
    if (control_word->col_type == ta_col_type::IntensityOne && control_word->volume)
      is_32byte_transfer = false;
    if (control_word->col_type == ta_col_type::IntensityOne && control_word->offset)
      is_32byte_transfer = false;

    // According to pg 185, modifier volume globals are 32 bytes.
  }

  //
  if (control_word->type == ta_para_type::Polygon ||
      control_word->type == ta_para_type::Sprite) {
    m_gpu_state.global_control_word = *control_word;
  }

  // Section 8.6, pg 395
  if (control_word->type == ta_para_type::Vertex) {
    const ta_param_word global_control = m_gpu_state.global_control_word;

    // 5, 6
    if (global_control.col_type == ta_col_type::Floating && global_control.texture)
      is_32byte_transfer = false;

    // 16, 17
    if (global_control.texture && global_control.volume &&
        global_control.col_type == ta_col_type::Packed)
      is_32byte_transfer = false;

    // 19, 20, 22, 23
    if (global_control.texture && global_control.volume)
      is_32byte_transfer = false;

    // All sprite vertex data are 64 bytes
    if (global_control.type == ta_para_type::Sprite)
      is_32byte_transfer = false;

    // pg 188 - modifier veretx data is 64 bytes!!!
    if (global_control.list_type == ta_list_type::OpaqueModifier ||
        global_control.list_type == ta_list_type::TransModifier)
      is_32byte_transfer = false;
  }

  // If 32 byte, go ahead to translation logic
  if (is_32byte_transfer) {
    // atomic([&] { printf("DMA -- 32-byte\n"); });
    handle_dma_data(m_gpu_state.dma_buffer, 32);
    m_gpu_state.current_buffer_size = 0;
  } else {
    // This is a 64-byte transfer...
    m_gpu_state.current_buffer_size = 32;
  }
}

void
Holly::handle_yuv_dma(u32 addr, u32 length, const uint8_t *src)
{
  const auto &yuv_ctrl(_regs.TA_YUV_TEX_CTRL);
  const bool input_is_yuv420 = yuv_ctrl.yuv_form == 0;

  // YUV DMA described in 2.6.4.2
  // YUV Macro Block storage formats (YUV420/YUV422) described in 3.8.1

  // printf("YUV DMA: addr 0x%x, length 0x%x, yuv_tex %d yuv texbase 0x%08x\n",
  //        addr,
  //        length,
  //        yuv_ctrl.yuv_tex,
  //        _regs.TA_YUV_TEX_BASE);

  if (input_is_yuv420) {

    memcpy(
      &m_gpu_state.yuv420_buffer[m_gpu_state.yuv_converter_bytes_so_far], src, length);
    m_gpu_state.yuv_converter_bytes_so_far += length;

    if (m_gpu_state.yuv_converter_bytes_so_far ==
        HollyRenderState::bytes_per_yu420_macroblock) {
      if (yuv_ctrl.yuv_tex == 1) {
        printf("Unsupported tex_format=1\n");
        return;
      }

      // Calculate the upper left of the output macro block (Assuming scan-ordered pixels)
      const u32 macroblocks_per_row = yuv_ctrl.yuv_u_size + 1;
      const u32 pixels_per_row      = macroblocks_per_row * 16;

      // Starting top-left pixel (x,y) in the output texture for this macro block.
      const u32 macroblock_start_x = (_regs.TA_YUV_TEX_CNT % macroblocks_per_row) * 16;
      const u32 macroblock_start_y = (_regs.TA_YUV_TEX_CNT / macroblocks_per_row) * 16;

      u32 macroblock_output_start =
        2 * (pixels_per_row * macroblock_start_y + macroblock_start_x);

      const u8 *const u_data = &m_gpu_state.yuv420_buffer[0];
      const u8 *const v_data = &m_gpu_state.yuv420_buffer[64];

      for (u32 y = 0; y < 16; ++y) {
        // Process two nearby pixels at a time in the output
        for (u32 x = 0; x < 16; x += 2) {
          // U + V Data
          const u32 y2 = y / 2;
          const u32 x2 = x / 2;

          const u8 U = u_data[x2 + 8 * y2];
          const u8 V = v_data[x2 + 8 * y2];

          // Y data is captured in 4 8x8 sub-blocks making up the 16x16 macro block.
          u32 y_start = 64 + 64;
          if (y >= 8)
            y_start += 128;
          if (x >= 8)
            y_start += 64;
          const u8 *const y_data = &m_gpu_state.yuv420_buffer[y_start];

          const u8 subblock_y = y % 8;
          const u8 subblock_x = x % 8;

          const u8 Y0 = y_data[subblock_x + 8 * subblock_y];
          const u8 Y1 = y_data[subblock_x + 1 + 8 * subblock_y];

          const u16 texture_offset = y * pixels_per_row + x;
          // macroblock_output_start[texture_offset]     = (Y0 << 8) | U;
          // macroblock_output_start[texture_offset + 1] = (Y1 << 8) | V;
          const u32 data_low  = (Y0 << 8) | U;
          const u32 data_high = (Y1 << 8) | V;
          const u32 data      = (data_high << 16) | data_low;

          const u32 vram_addr =
            _regs.TA_YUV_TEX_BASE + macroblock_output_start + 2 * texture_offset;

          // TODO : coalesce to 2 32-bit DMAs
          VRAMAddress64 dest { vram_addr };
          // printf("yuv write 64b-area 0x%08x 32-area 0x%08x\n", dest.get(), dest.to32().get());
          vram_write(dest, data);
        }
      }

      // Reset the byte counter for the next macro block to come in, increment macro
      // blocks converted.
      m_gpu_state.yuv_converter_bytes_so_far = 0;
      _regs.TA_YUV_TEX_CNT++;
    }
  } else {
    printf("Input is in YUV422!\n");
    return;
  }

  const u32 total_yuv_macroblocks =
    (_regs.TA_YUV_TEX_CTRL.yuv_u_size + 1) * (_regs.TA_YUV_TEX_CTRL.yuv_v_size + 1);

  if (_regs.TA_YUV_TEX_CNT == total_yuv_macroblocks) {
    vram_write(VRAMAddress64 { _regs.TA_YUV_TEX_BASE }, 0u);
    m_interrupt_queue.push(Interrupts::Normal::EndOfTransfer_YUV);
    m_event_interrupt.cancel();
    m_console->schedule_event(1000, &m_event_interrupt);
    _regs.TA_YUV_TEX_CNT = 0;
  }
}

void
Holly::handle_direct_dma_32b(u32 addr, u32 length, const uint8_t *src)
{
  m_console->memory()->dma_write(kVram32BaseAddress + (addr & 0x007fffff), src, length);
}

void
Holly::handle_direct_dma_64b(u32 addr, u32 length, const uint8_t *src)
{
  u32 *src_u32 = (u32 *)src;
  VRAMAddress64 vram64 { addr & 0x007f'ffff };

  // TODO : coalesce to two 32b region DMAs
  for (u32 i = 0; i < length; i += 4) {
    vram_write(vram64, *src_u32);
    src_u32++;
    vram64 += 4;
  }
}

void
Holly::write_dma(u32 addr, u32 length, const uint8_t *src)
{
  // Page 18, Table 2-3

  // Polygon Converter through TA (-/W)
  //   0x1000'0000 - 0x107F'FFFF
  //   0x1200'0000 - 0x127F'FFFF
  //      00x
  // YUV Converter through TA     (-/W)
  //   0x1080'0000 - 0x10FF'FFFF
  //   0x1280'0000 - 0x12FF'FFFF
  //      08x
  // Texture Access through TA    (-/W)
  //   0x1100'0000 - 0x117F'FFFF
  //   0x1300'0000 - 0x137F'FFFF
  //      10x
  // Texture Memory 64-bit through PVR (R/W)
  //   0x0400'0000 - 0x047F'FFFF
  //   0x0600'0000 - 0x067F'FFFF
  //      40x
  // Texture Memory 32-bit through PVR (R/W)
  //   0x0500'0000 - 0x057F'FFFF
  //   0x0700'0000 - 0x077F'FFFF
  //      50x

  // So the only bits that matter to determine the mask
  // 0x1000'0000
  // 0x0400'0000
  // 0x0100'0000
  // 0x0080'0000

  // Every section is mirrored an extra time
  const u32 mask        = 0x1580'0000;
  const bool is_ta_poly = (addr & mask) == 0x1000'0000;
  const bool is_ta_yuv  = (addr & mask) == 0x1080'0000;
  const bool is_ta_tex  = (addr & mask) == 0x1100'0000;
  const bool is_pvr_64  = (addr & mask) == 0x0400'0000;
  const bool is_pvr_32  = (addr & mask) == 0x0500'0000;

  if (is_ta_poly) {
    handle_polygon_dma(addr, length, src);
    frame_stats.bytes_ta_fifo += length;
  } else if (is_ta_yuv) {
    handle_yuv_dma(addr, length, src);
    frame_stats.bytes_ta_yuv += length;
  } else if (is_ta_tex) {
    // Need to look at 0x0200'0000 bit to see if we use LMMODE0/1 for 32b/64b access
    const u32 LMMODE0   = m_console->system_bus()->get_SB_LMMODE0();
    const u32 LMMODE1   = m_console->system_bus()->get_SB_LMMODE1();
    const bool is_bus_a = (addr & 0x0200'0000) == 0;
    const bool is_32b   = (is_bus_a && LMMODE0) || (!is_bus_a && LMMODE1);
    if (is_32b) {
      handle_direct_dma_32b(addr, length, src);
    } else {
      handle_direct_dma_64b(addr, length, src);
    }
    frame_stats.bytes_ta_tex += length;
  } else if (is_pvr_64) {
    handle_direct_dma_64b(addr, length, src);
    frame_stats.bytes_ta_tex += length;
  } else if (is_pvr_32) {
    handle_direct_dma_32b(addr, length, src);
    frame_stats.bytes_ta_tex += length;
  }
}

void
Holly::handle_ta_end_of_list(const u8 *src, u32 length)
{
  m_gpu_state.in_polygon = 0;
  m_gpu_state.in_sprite  = 0;

  log.debug("End of list reached. Firing an interrupt for list_type %d",
            m_gpu_state.current_list_type);

  switch (m_gpu_state.current_list_type) {
    case ta_list_type::TransModifier:
      m_interrupt_queue.push(Interrupts::Normal::EndOfTransfer_TranslucentModifierVolume);
      m_console->trace_zone("Translucent Modifier List",
                            TraceTrack::TA,
                            m_time_list_start,
                            m_console->current_time());
      if (!m_event_interrupt.is_scheduled()) {
        m_console->schedule_event(1'0, &m_event_interrupt);
      }
      break;

    case ta_list_type::Translucent:
      m_interrupt_queue.push(Interrupts::Normal::EndOfTransfer_Translucent);
      m_console->trace_zone(
        "Translucent List", TraceTrack::TA, m_time_list_start, m_console->current_time());
      if (!m_event_interrupt.is_scheduled()) {
        m_console->schedule_event(1'0, &m_event_interrupt);
      }
      break;

    case ta_list_type::OpaqueModifier:
      m_interrupt_queue.push(Interrupts::Normal::EndOfTransfer_OpaqueModifierVolume);
      m_console->trace_zone("Opaque Modifier List",
                            TraceTrack::TA,
                            m_time_list_start,
                            m_console->current_time());
      if (!m_event_interrupt.is_scheduled()) {
        m_console->schedule_event(1'0, &m_event_interrupt);
      }
      break;

    case ta_list_type::Opaque:
      m_interrupt_queue.push(Interrupts::Normal::EndOfTransfer_Opaque);
      m_console->trace_zone(
        "Opaque List", TraceTrack::TA, m_time_list_start, m_console->current_time());
      if (!m_event_interrupt.is_scheduled()) {
        m_console->schedule_event(1'0, &m_event_interrupt);
      }
      break;

    case ta_list_type::PunchThrough:
      m_interrupt_queue.push(Interrupts::Normal::EndOfPunchThroughList);
      m_console->trace_zone("Punchthrough List",
                            TraceTrack::TA,
                            m_time_list_start,
                            m_console->current_time());
      if (!m_event_interrupt.is_scheduled()) {
        m_console->schedule_event(1'0, &m_event_interrupt);
      }
      break;

    default:
      break;
  }

  m_gpu_state.current_list_type = ta_list_type::Undefined;

  ////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////
  // New TA Logic
}

void
Holly::handle_ta_polygon(const u8 *src, u32 length)
{
  const u32 *const src_u32                = reinterpret_cast<const u32 *>(src);
  const float *const src_f32              = reinterpret_cast<const float *>(src);
  const ta_param_word *const control_word = (const ta_param_word *)src;

  m_gpu_state.list_number++;
  m_current_frame_data->display_lists.push_back(render::DisplayList {});
  render::DisplayList *const display_list = &m_current_frame_data->display_lists.back();
  frame_stats.num_objects++;

  display_list->texture_key = TextureKey {};

  ta_param_word pcw = reinterpret<ta_param_word>(src_u32[0]);
  ta_isp_word isp   = reinterpret<ta_isp_word>(src_u32[1]);
  ta_tsp_word tsp   = reinterpret<ta_tsp_word>(src_u32[2]);
  ta_tex_word tex   = reinterpret<ta_tex_word>(src_u32[3]);

  if (m_gpu_state.current_list_type == ta_list_type::Undefined) {
    ta_begin_list_type(pcw.list_type);
  }

  isp.opaque_or_translucent.culling_mode = (isp.raw >> 27) & 0b11;
  // printf("cull %u\n", (isp.raw >> 27) & 0b11);

  display_list->param_control_word = pcw;
  display_list->isp_word           = isp;
  display_list->tsp_word           = tsp;
  display_list->tex_word           = tex;

  // The current list type is only updated in certain conditions. Not every global
  // parameters' control word has a meaningful list_type. This is why we need to
  // track the current list type separately and update the display list here.
  display_list->param_control_word.list_type = m_gpu_state.current_list_type;

  // Keep this around so we can reference the 'object list' settings later when
  // we read the vertex data in the following transfer
  memcpy(m_gpu_state.last_polysprite_globals_data, src, length);

  // Beginning a polygon, reset our coord count
  m_gpu_state.coord_count = 0;

  if (control_word->group_en) {
    // TODO : Handle user tile clipping updates

    // NOTE: We have no need to emulate the strip_len field, but it's collected here
    // anyway for completeness.
    m_gpu_state.strip_len = std::max(1u, control_word->strip_len * 2u);
  }

  // This is polygon-type Global Params, but the list type here
  // allows us to know if it will be opaque/translucent/modifier
  // type data, which influences how we will later read the parameter control word
  if (control_word->list_type == ta_list_type::Opaque ||
      control_word->list_type == ta_list_type::Translucent ||
      control_word->list_type == ta_list_type::PunchThrough) {
    m_gpu_state.in_polygon = 1;
    m_gpu_state.in_sprite  = 0;
  } else {
    m_gpu_state.in_polygon = 0;
    m_gpu_state.in_sprite  = 0;
  }

  // TODO : Modifier Volumes

  // If this list of data which will follow these Global Params should be textured
  // Then we can extract the texture data from some following TSP words in this DMA
  if (display_list->param_control_word.texture) {
    // Stride and width need to be tracked independently. Read U Size (page 211)

    const TextureKey tex_key = { tex, tsp };
    m_gpu_state.texture_key  = tex_key;

    display_list->texture_key = tex_key;
    display_list->tsp_word    = tsp;
  }

  // For intensity-colored faces, pull out that data
  if (control_word->col_type == ta_col_type::IntensityOne) {
    if (control_word->offset) {
      // ARGB -> RGBA
      m_gpu_state.intensity_face_color =
        Vec4f { src_f32[9], src_f32[10], src_f32[11], src_f32[8] };
      m_gpu_state.intensity_offset_color =
        Vec4f { src_f32[13], src_f32[14], src_f32[15], src_f32[12] };
    } else {
      m_gpu_state.intensity_face_color =
        Vec4f { src_f32[5], src_f32[6], src_f32[7], src_f32[4] };
    }
  }

  ////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////
  // New TA Logic

  if (!kNewRendererEnabled) {
    return;
  }

  // 4 of the bits stored in ISP/TSP control word are copied from
  // the global parameter control word.
  isp.opaque_or_translucent.texture = pcw.texture;
  isp.opaque_or_translucent.offset  = pcw.offset;
  isp.opaque_or_translucent.gouraud = pcw.gouraud;
  isp.opaque_or_translucent.uv16    = pcw.uv16;

  const u32 strip_base_addr = ta_params_append(isp.raw);
  ta_params_append(tsp.raw);
  ta_params_append(tex.raw);

  _ta_state.current_tristrip_isp_base = strip_base_addr;
  _ta_state.current_tristrip_count    = 0;
  _ta_state.globals_pcw               = pcw;
  _ta_state.globals_isp               = isp;
  _ta_state.globals_tsp               = tsp;
  _ta_state.globals_tex               = tex;
}

u32
Holly::ta_params_append(u32 word)
{
  if (!kNewRendererEnabled) {
    return 0;
  }
  if (_regs.TA_ITP_CURRENT >= _regs.TA_ISP_LIMIT) {
    return 0;
  }

  const u32 store_addr = _regs.TA_ITP_CURRENT;
  vram_write(VRAMAddress32 { store_addr }, word);
  _regs.TA_ITP_CURRENT += 4;
  return store_addr;
}

void
Holly::handle_ta_user_tile_clip(const u8 *src, u32 length)
{
  _ta_state.user_clip_x_min = src[4] & 0x3f;
  _ta_state.user_clip_y_min = src[5] & 0x0f;
  _ta_state.user_clip_x_max = src[6] & 0x3f;
  _ta_state.user_clip_x_max = src[7] & 0x0f;
}

void
Holly::handle_ta_sprite(const u8 *src, u32 length)
{
  const u32 *const src_u32 = reinterpret_cast<const u32 *>(src);
  // const float *const src_f32              = reinterpret_cast<const float *>(src);
  // const ta_param_word *const control_word = (const ta_param_word *)src;

  m_current_frame_data->display_lists.push_back(render::DisplayList {});
  render::DisplayList *const display_list = &m_current_frame_data->display_lists.back();
  m_gpu_state.list_number++;
  frame_stats.num_objects++;

  display_list->texture_key = TextureKey {};

  auto pcw = reinterpret<ta_param_word>(src_u32[0]);
  auto isp = reinterpret<ta_isp_word>(src_u32[1]);
  auto tsp = reinterpret<ta_tsp_word>(src_u32[2]);

  display_list->param_control_word = pcw;
  display_list->isp_word           = isp;
  display_list->tsp_word           = tsp;

  if (m_gpu_state.current_list_type == ta_list_type::Undefined) {
    m_gpu_state.current_list_type = pcw.list_type;
  }

  // Keep this around so we can reference the 'object list' settings later when
  // we read the vertex data in the following transfer
  memcpy(m_gpu_state.last_polysprite_globals_data, src, length);

  m_gpu_state.in_polygon = 0;
  m_gpu_state.in_sprite  = 1;

  if (display_list->param_control_word.texture) {
    ta_tsp_word tsp = reinterpret<ta_tsp_word>(src_u32[2]);
    ta_tex_word tex = reinterpret<ta_tex_word>(src_u32[3]);

    const TextureKey tex_key = { tex, tsp };
    m_gpu_state.texture_key  = tex_key;

    display_list->tex_word    = tex;
    display_list->texture_key = tex_key;
  }
}

void
Holly::handle_ta_object_list_set(const u8 *src, u32 length)
{
  const ta_param_word *const pcw = (const ta_param_word *)src;

  // TODO

  // This is one of the four conditions that can cause a new list type to be eligible.
  if (m_gpu_state.current_list_type == ta_list_type::Undefined) {
    m_gpu_state.current_list_type = pcw->list_type;
  }
}

void
Holly::handle_ta_vertex(const u8 *src, u32 length)
{
  const u32 *const src_u32       = reinterpret_cast<const u32 *>(src);
  const float *const src_f32     = reinterpret_cast<const float *>(src);
  const ta_param_word *const pcw = (const ta_param_word *)src;

  Vec4f base_color;
  Vec4f offset_color;
  Vec2f uvs[4];

  render::Vertex new_vertex;

  // Current display list (iterator)
  auto dl = (m_current_frame_data->display_lists.end() - 1);

  if (m_gpu_state.in_sprite) {

    // Sprite coords and colors
    Vec3f position_a { src_f32[1], src_f32[2], src_f32[3] };
    Vec3f position_b { src_f32[4], src_f32[5], src_f32[6] };
    Vec3f position_c { src_f32[7], src_f32[8], src_f32[9] };
    Vec3f position_d { src_f32[10],
                       src_f32[11],
                       src_f32[9] }; // TODO: Just copying C.z to D.z for now

    base_color =
      packed_color_argb_to_vec4(m_gpu_state.last_polysprite_globals_data[4].raw);
    offset_color =
      packed_color_argb_to_vec4(m_gpu_state.last_polysprite_globals_data[5].raw);

    // UVs
    uvs[0] = uv16_to_vec2f(src_u32[13]);
    uvs[1] = uv16_to_vec2f(src_u32[14]);
    uvs[2] = uv16_to_vec2f(src_u32[15]);

    // Compute the "D" UV
    uvs[3] = { uvs[0].x + (uvs[2].x - uvs[1].x), uvs[0].y + (uvs[2].y - uvs[1].y) };

    render::Vertex vertex_a { .position     = position_a,
                              .uv           = uvs[0],
                              .base_color   = base_color,
                              .offset_color = offset_color };
    render::Vertex vertex_b { .position     = position_b,
                              .uv           = uvs[1],
                              .base_color   = base_color,
                              .offset_color = offset_color };
    render::Vertex vertex_c { .position     = position_c,
                              .uv           = uvs[2],
                              .base_color   = base_color,
                              .offset_color = offset_color };
    render::Vertex vertex_d { .position     = position_d,
                              .uv           = uvs[3],
                              .base_color   = base_color,
                              .offset_color = offset_color };

    // Append triangles
    {
      // When the TA creates list for the ISP/CORE, it's supposed to duplicate this data
      // into the TSP words. Let's do that so all this data is consistent.
      dl->isp_word.opaque_or_translucent.texture = dl->param_control_word.texture;
      dl->isp_word.opaque_or_translucent.offset  = dl->param_control_word.offset;
      dl->isp_word.opaque_or_translucent.gouraud = dl->param_control_word.gouraud;
      dl->isp_word.opaque_or_translucent.uv16    = dl->param_control_word.uv16;

      frame_stats.num_polygons += 2;

      // Push the actual triangles
      dl->triangles.push_back({ .vertices = { vertex_a, vertex_b, vertex_c } });
      dl->triangles.push_back({ .vertices = { vertex_c, vertex_d, vertex_a } });
    }
  }

  ////////////////////////////////////////////
  else if (m_gpu_state.in_polygon) {

    const ta_param_word global_control = dl->param_control_word;

    ////////////////////////////////////////
    // Construct position

    // All vertex data is /always/ located in these locations
    Vec3f new_position(src_f32[1], src_f32[2], src_f32[3]);

    ////////////////////////////////////////
    // Construct UVs

    // All textured-polygon formats except 'two-volumes' /always/ their
    // data located in these positions. Whether or not it is in two words
    // depends on whether the uv16 flag is set in the global params.

    if (global_control.texture && global_control.uv16) {
      uvs[0] = uv16_to_vec2f(src_u32[4]);
    } else {
      uvs[0] = { src_f32[4], src_f32[5] };
    }

    if (global_control.texture) {
      auto texture =
        m_console->texture_manager()->get_texture_handle(m_gpu_state.texture_key);

      const bool is_palette =
        _ta_state.globals_tex.pixel_fmt == tex_pixel_fmt::Palette4 ||
        _ta_state.globals_tex.pixel_fmt == tex_pixel_fmt::Palette8;

      const bool is_stride_set = texture->tex_word.stride && texture->tex_word.scanline;

      // Stride/scanline are only in effect if it's not a palette texture
      if (is_stride_set && !is_palette) {
        const unsigned width = 8 << texture->tsp_word.size_u;
        uvs[0].x *= float(width) / float(_regs.TEXT_CONTROL.stride * 32);
      }
    }

    // TODO : Handle 'Two-Volumes' types of texture UVs

    ////////////////////////////////////////
    // Construct color

    // Vertex formats pg 186

    // Construct the shading color
    Vec4f shading_color(0, 0, 0, 1);
    Vec4f new_offset_color(0, 0, 0, 0);

    // All packed colors appear at the same place.
    if (global_control.col_type == ta_col_type::Packed) {
      uint8_t *packed_argb = (uint8_t *)&src_u32[6];
      shading_color.w      = packed_argb[3] / 255.0f; // Endianness ARGB -> 3210
      shading_color.x      = packed_argb[2] / 255.0f;
      shading_color.y      = packed_argb[1] / 255.0f;
      shading_color.z      = packed_argb[0] / 255.0f;

      if (global_control.texture) {
        // Packed, Textured :: Offset color data
        uint8_t *packed_argb = (uint8_t *)&src_u32[7];
        new_offset_color.w   = packed_argb[3] / 255.0f; // Endianness ARGB -> 3210
        new_offset_color.x   = packed_argb[2] / 255.0f;
        new_offset_color.y   = packed_argb[1] / 255.0f;
        new_offset_color.z   = packed_argb[0] / 255.0f;
      }
    }

    if (global_control.col_type == ta_col_type::Floating) {
      if (!(global_control.texture)) {
        // printf("Floating non-textured: length = %d\n", length);
        shading_color.w = src_f32[4];
        shading_color.x = src_f32[5];
        shading_color.y = src_f32[6];
        shading_color.z = src_f32[7];
      } else {
        shading_color.w = src_f32[8];
        shading_color.x = src_f32[9];
        shading_color.y = src_f32[10];
        shading_color.z = src_f32[11];

        new_offset_color.w = src_f32[12];
        new_offset_color.x = src_f32[13];
        new_offset_color.y = src_f32[14];
        new_offset_color.z = src_f32[15];
      }
    }

    if (global_control.col_type == ta_col_type::IntensityOne ||
        global_control.col_type == ta_col_type::IntensityTwo) {
      float base_intensity = src_f32[6];
      shading_color.w      = m_gpu_state.intensity_face_color.w;
      shading_color.x      = m_gpu_state.intensity_face_color.x * base_intensity;
      shading_color.y      = m_gpu_state.intensity_face_color.y * base_intensity;
      shading_color.z      = m_gpu_state.intensity_face_color.z * base_intensity;

      float offset_intensity = src_f32[7];
      new_offset_color.w     = m_gpu_state.intensity_offset_color.w;
      new_offset_color.x     = m_gpu_state.intensity_offset_color.x * offset_intensity;
      new_offset_color.y     = m_gpu_state.intensity_offset_color.y * offset_intensity;
      new_offset_color.z     = m_gpu_state.intensity_offset_color.z * offset_intensity;
    }

    // TODO : Handle all the various 'Two-Volumes' modes

    // Construct based on shading and other settings.
    Vec4f new_color  = shading_color;
    const auto clamp = [](float v, float min_v, float max_v) {
      return v < min_v ? min_v : v > max_v ? max_v : v;
    };
    new_color.x = clamp(new_color.x, 0, 1);
    new_color.y = clamp(new_color.y, 0, 1);
    new_color.z = clamp(new_color.z, 0, 1);
    new_color.w = clamp(new_color.w, 0, 1);

    new_vertex = render::Vertex { .position     = new_position,
                                  .uv           = uvs[0],
                                  .base_color   = new_color,
                                  .offset_color = new_offset_color };

    // Create ISP/TSP-compatible colors
    base_color   = new_color;
    offset_color = new_offset_color;

    // When the TA creates list for the ISP/CORE, it's supposed to duplicate this data
    // into the TSP words. Let's do that so all this data is consistent.
    dl->isp_word.opaque_or_translucent.texture = dl->param_control_word.texture;
    dl->isp_word.opaque_or_translucent.offset  = dl->param_control_word.offset;
    dl->isp_word.opaque_or_translucent.gouraud = dl->param_control_word.gouraud;
    dl->isp_word.opaque_or_translucent.uv16    = dl->param_control_word.uv16;

    ++m_gpu_state.coord_count;

    m_gpu_state.vertices[0] = m_gpu_state.vertices[1];
    m_gpu_state.vertices[1] = m_gpu_state.vertices[2];
    m_gpu_state.vertices[2] = new_vertex;

    // Flat shading polygon colors are "offset" by two vertices. e.g. vertex 2 uses
    // colors from vertex 0.
    if (!global_control.gouraud) {
      m_gpu_state.vertices[0].base_color   = new_vertex.base_color;
      m_gpu_state.vertices[0].offset_color = new_vertex.offset_color;
      m_gpu_state.vertices[1].base_color   = new_vertex.base_color;
      m_gpu_state.vertices[1].offset_color = new_vertex.offset_color;
    }

    if (m_gpu_state.coord_count >= 3) {
      dl->triangles.push_back(
        { m_gpu_state.vertices[0], m_gpu_state.vertices[1], m_gpu_state.vertices[2] });
      frame_stats.num_polygons += 1;
    }

    if (pcw->strip_end) {
      m_gpu_state.coord_count = 0;
    }

    /////////////////////////////////////////////////////////////
    /////////////////////////////////////////////////////////////
    // New TA Logic

    if (!kNewRendererEnabled) {
      return;
    }

    _ta_state.strip_vertices[0] = _ta_state.strip_vertices[1];
    _ta_state.strip_vertices[1] = _ta_state.strip_vertices[2];
    _ta_state.strip_vertices[2] = new_vertex.position;

    // Vertex parameters are always in this order..
    // (Parameter Control Word)
    // XYZ
    // UV word (or words)
    // Base Color word     < base color
    // Offset color word   < if offset
    // bumpmap word        < if bumpmap

    // TODO: Doesn't handle the two-volume case yet

    ta_params_append(src_u32[1]); // X
    ta_params_append(src_u32[2]); // Y
    ta_params_append(src_u32[3]); // Z

    // If there is texturing it's either 2 UVs packed into 32b or each taking up
    // 32b. The UVs are always in the same order.
    if (_ta_state.globals_pcw.texture) {
      ta_params_append(src_u32[4]);
      if (!_ta_state.globals_pcw.uv16) {
        ta_params_append(src_u32[5]);
      }
    }

    const u32 isp_base_color = vec4f_color_to_packed(base_color);
    ta_params_append(isp_base_color);

    // Note: offset color is only legal on textured polygons
    if (_ta_state.globals_pcw.offset && _ta_state.globals_pcw.texture) {
      const u32 isp_offset_color = vec4f_color_to_packed(offset_color);
      ta_params_append(isp_offset_color);
    }

    // Params have been added, now we add to object lists for each tile
    ta_triangle_strip_vertex_append();

    if (pcw->strip_end) {
      ta_list_flush_triangle_strip();
    }
  }
}

/** Handle a write/DMA to the TA. This will not actually copy anything,
 *  but rather interprets the data and enqueues render commands. */
void
Holly::handle_dma_data(const u8 *src, u32 length)
{
  const ta_param_word *control_word = (const ta_param_word *)src;

  /* There are 3 classes of high level data that can be sent (Page 152):
   * 1) Control Params    - End of a list, user tile clip, or object list set
   * 2) Global Params     - Define upcoming polygon/sprite parameters
   * 3) Vertex Parameters - The actual geometry data
   */

  std::lock_guard rq_lock(m_rq_lock);

  switch (control_word->type) {
    case ta_para_type::EndOfList:
      // printf("TA EndOfList : list_type %u\n", m_gpu_state.current_list_type);
      handle_ta_end_of_list(src, length);
      break;
    case ta_para_type::UserTileClip:
      // printf("TA UserTileClip\n");
      handle_ta_user_tile_clip(src, length);
      break;
    case ta_para_type::ObjectListSet:
      // printf("TA ObjectListSet\n");
      handle_ta_object_list_set(src, length);
      break;
    case ta_para_type::Polygon:
      handle_ta_polygon(src, length);
      // printf("TA Polygon %u\n", m_gpu_state.current_list_type);
      break;
    case ta_para_type::Sprite:
      handle_ta_sprite(src, length);
      // printf("TA Sprite %u\n", m_gpu_state.current_list_type);
      break;
    case ta_para_type::Vertex:
      handle_ta_vertex(src, length);
      break;
  }
}

}
