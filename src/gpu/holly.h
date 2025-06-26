#pragma once

#include <vector>

#include "fox/mmio_device.h"
#include "core/register_state.h"
#include "serialization/storage.h"
#include "shared/scheduler.h"
#include "gpu/display_list.h"
#include "gpu/renderer.h"
#include "gpu/vram.h"

#include "systems/dreamcast/renderer.h"

class Console;

namespace gui {
class GraphicsWindow;
}

namespace gpu {

class GraphicsMMIO;
class TileAccelerator;

/*!
 * Holly contains a PowerVR-based GPU. This GPU receives commands as a series of
 * DMAs, each delivering a little bit of information towards building up a primitive
 * to be drawn. These commands are eventually fed into a display list which is drawn
 * to the screen by tha TA system.
 *
 * This struct captures all of the stateful information of the GPU that exists
 * between DMAs so that draw commands can be formed.
 */
struct HollyRenderState {
  /* Draw command interpretation */

  /*!
   * @brief The most recent list type which was initiated. These are given at
   *        the start of a draw command to indicate the kind of data that will
   *        follow after.
   */
  ta_list_type current_list_type = ta_list_type::Undefined;

  /*!
   * @brief All data is given in triangle-strip form. This tracks the last 5
   *        vertices.
   */
  render::Vertex vertices[5];

  /*!
   * @brief The total number of coordinates drawn so far in this triangle strip.
   */
  int coord_count = 0;

  /*!
   * @brief Are we drawing a polygon-type primitive?
   */
  int in_polygon = 0;

  /*!
   * @brief Are we drawing a sprite-type primitive?
   */
  int in_sprite = 0;

  /*!
   * @brief Handle for texture referenced by draw command. Only valid if
   *        `textured`.
   */
  TextureKey texture_key;

  /*!
   * @brief Capture the intensity face color, if applicable.
   */
  Vec4f intensity_face_color;

  /*!
   * @brief Capture the intensity offset color, if applicable.
   */
  Vec4f intensity_offset_color;

  /*!
   * @brief Capture the flat shading face color, if applicable.
   */
  Vec4f flat_shading_base_color;

  /*!
   * @brief Capture the flat shading offset color, if applicable.
   */
  Vec4f flat_shading_offset_color;

  /* Keep around the last polygon or sprite global parameter data
   * since it holds useful data about how to interperet following vertex data */
  ta_param_word last_polysprite_globals_data[32];

  /** 1024-entries of palette data which is written via DMA. Certain textures
   * make use of this data to calculate color data. */
  u32 palette_ram[1024];

  /** 128-entries of fog data, with a somewaht complex encoding. Only applicable
   * in fog table mode. */
  u32 fog_table[128];

  /* DMA for Draw Commands */

  ta_param_word global_control_word;
  u8 dma_buffer[64];
  u32 current_buffer_size = 0;

  /* DMA for YUV Conversion Function */

  static const u32 bytes_per_yu420_macroblock = 64 + 64 + 256;
  u8 yuv420_buffer[bytes_per_yu420_macroblock];

  u32 yuv_converter_bytes_so_far = 0;
  u32 list_number;
  u32 list_polygon_number;
  u32 strip_len = 0;
  int queue_id;

  /** Number of times that START_RENDER has been initiated. This can be different from
   * vblanks. */
  u32 start_render_count;
};

struct SignalPulseGeneratorState {
  u32 m_current_line;
  i64 m_nanos_per_line;
  u32 m_vblank_in_count;
};

class Holly : public fox::MMIODevice, serialization::Serializer {
public:
  Holly(Console *console);
  ~Holly();

  void recalculate_line_timing();
  i64 get_nanos_per_line();

  void step_signal_pulse_generator();

  void start_render();
  void finish_render();

  // Memory i/o
  void register_regions(fox::MemoryTable *memory) override;

  u16 read_u16(u32 addr) override;
  u32 read_u32(u32 addr) override;

  void write_u16(u32 address, u16 value) override;
  void write_u32(u32 addr, u32 val) override;
  void write_u64(u32 addr, u64 val) override;

  void write_dma(u32 addr, u32 length, const uint8_t *src) override;

  // Serialization
  void serialize(serialization::Snapshot &) override;
  void deserialize(const serialization::Snapshot &) override;

  // TODO
  void reset();
  void render_to(render::FrameData *target);

  u32 get_vblank_in_count() const;
  u32 get_render_count() const;

  // DMA helpers (TODO : move)
  void handle_dma_data(const u8 *src, u32 length);
  void handle_polygon_dma(u32 addr, u32 length, const uint8_t *src);
  void handle_yuv_dma(u32 addr, u32 length, const uint8_t *src);
  void handle_direct_dma_32b(u32 addr, u32 length, const uint8_t *src);
  void handle_direct_dma_64b(u32 addr, u32 length, const uint8_t *src);

  void handle_softreset();
  void render_background(u32 vram_offset);

  u32 get_pal_ram_ctrl() const
  {
    return _regs.PAL_RAM_CTRL.raw;
  }
  u32 const *get_palette_ram() const
  {
    return m_gpu_state.palette_ram;
  }
  u32 get_text_control_stride() const
  {
    return _regs.TEXT_CONTROL.stride;
  }

  /** Before passing off data to a host frontend for rendering, perform any accounting of
   * which textures were updated and used in this frame so that the correct textures are
   * sent to the host, and others can eventually expire out of the cache.  */
  void prepare_frame_textures();

private:
  u32 vram_read(VRAMAddress32 addr);
  u32 vram_read(VRAMAddress64 addr);

  void vram_write(VRAMAddress32 addr, u32 val);
  void vram_write(VRAMAddress64 addr, u32 val);

  void step_spg_line();

  void print_region_array();
  void handle_interrupt_event();

  void handle_ta_end_of_list(const u8 *src, u32 length);
  void handle_ta_polygon(const u8 *src, u32 length);
  void handle_ta_user_tile_clip(const u8 *src, u32 length);
  void handle_ta_sprite(const u8 *src, u32 length);
  void handle_ta_vertex(const u8 *src, u32 length);
  void handle_ta_object_list_set(const u8 *src, u32 length);

  void debug_walk_frame();

  u64 m_time_list_start = -1;

  friend class ::gui::GraphicsWindow;

  Console *const m_console;
  zoo::dreamcast::Renderer *const m_renderer;

  std::atomic<bool> m_is_running { true };
  Log::Logger<Log::LogModule::GRAPHICS> log;

  HollyRenderState m_gpu_state;
  SignalPulseGeneratorState m_spg_state;

  std::mutex m_rq_lock;

  /*!
   * @brief Event used to schedule scanline state updates.
   */
  EventScheduler::Event m_event_spg;

  /*!
   * @brief Event used to schedule render completion.
   */
  EventScheduler::Event m_event_render;

  std::queue<u32> m_interrupt_queue;
  EventScheduler::Event m_event_interrupt;

  // All current rendering contexts. TA_ISP_BASE and PARAM_BASE are effectively
  // reading/writing to one of the entries in this array.
  std::vector<gpu::render::FrameData *> m_frame_data;

  // Follows TA_ISP_BASE, where we're currently emitting data
  gpu::render::FrameData *m_current_frame_data;

  // Follows PARAM_BASE, what we should actually render
  gpu::render::FrameData *m_render_frame_data;

  void ta_list_init();
  u32 ta_get_list_opb_slot_count(ta_list_type);
  void ta_begin_list_type(ta_list_type);

  // TRIANGLE STRIPS
  void ta_triangle_strip_vertex_append();
  void ta_list_flush_triangle_strip();

  u32 ta_read_current_opb_slot(unsigned tile);

  // Appends a word, returns the address it was written to, and increments
  // TA_ITP_CURRENT
  u32 ta_params_append(u32 word);

  // Create a new slot for current triangle list
  void ta_next_opb_slot(unsigned tile);

  u32 ta_get_opb_slot_address(unsigned tile);

  u32 vec4f_color_to_packed(Vec4f in);

  // TA Objects params are a combination of pcw/isp/tsp/tex words
  // and ta_vertex instances.
  struct ta_vertex {
    float x, y, z;
    float u, v;
    u32 base_color;
    u32 offset_color;
  };

  struct ta_state {
    // Current PCW
    ta_param_word globals_pcw = { 0 };
    ta_isp_word globals_isp;
    ta_tsp_word globals_tsp;
    ta_tex_word globals_tex;

    u32 num_tiles_x;
    u32 num_tiles_y;
    u32 num_tiles_total;

    u32 list_start_addresses[5];
    u32 list_opb_sizes[5];

    // The three most recently input vertices
    Vec3f strip_vertices[3];

    static constexpr unsigned kTATiles = 400;

    // VRAM address of start of current OPB for current tile, current list type
    u32 tile_opb_addr[kTATiles];

    // Current write index in the OPB for current tile, current list type
    u32 tile_opb_slot[kTATiles];

    // For an ongoing triangle strip, where the ISP parameters begin
    u32 current_tristrip_isp_base = 0;

    // The number of triangle vertices that have been put into parameter space
    // 0: reset/nothing yet, 6: max, forces a flush
    u32 current_tristrip_count;

    // Specified in multiples of 32 pixels
    u32 user_clip_x_min = 0;
    u32 user_clip_x_max = 0;
    u32 user_clip_y_min = 0;
    u32 user_clip_y_max = 0;
  };
  ta_state _ta_state;

  struct {

    struct {
      union {
        u32 raw = 0x17FD11DBu;
      };
    } DEVICE_ID;

    struct {
      union {
        u32 raw = 0x00000011u;
      };
    } DEVICE_REVISION;

    struct {
      union {
        u32 raw = 0x031D0000u;

        struct {
          u32 linecomp_val : 10; // Value compared to the current line (see mode)
          u32 _rsvd0 : 2;
          u32 hblank_int_mode : 2; // Mode of execution for the HBlank interrupt
          u32 _rsvd1 : 2;
          u32 hb_int : 10; // Horizontal position on the line that the interrupt will
                           // trigger
          u32 _rsvd2 : 6;
        };
      };
    } SPG_HBLANK_INT;

    struct {
      union {
        u32 raw = 0x00150104u;

        struct {
          u32 vbstart_int : 10; // VBlank-start interrupt line number
          u32 _rsvd0 : 6;
          u32 vbend_int : 10; // VBlank-end interrupt line number
          u32 _rsvd1 : 6;
        };
      };
    } SPG_VBLANK_INT;

    struct {
      union {
        u32 raw = 0x007E0345u;

        struct {
          u32 hbstart : 10; // HBlank starting position
          u32 _rsvd0 : 6;
          u32 hbend : 10; // HBlank end position
          u32 _rsvd1 : 6;
        };
      };
    } SPG_HBLANK;

    struct {
      union {
        u32 raw = 0x01500104u;

        struct {
          u32 vbstart : 10; /* vblank start line (default 0x104) */
          u32 _rsvd0 : 6;
          u32 vbend : 10; /* vblank end line (default 0x015) */
          u32 _rsvd1 : 6;
        };
      };
    } SPG_VBLANK;

    struct {
      union {
        u32 raw = 0x0;

        struct {
          u32 scanline : 10; // Scanline number
          u32 fieldnum : 1;  // Even/Odd field (when interlaced)
          u32 blank : 1;     // Display Active
          u32 hsync : 1;     // HSync signal
          u32 vsync : 1;     // VSync signal
          u32 _rsvd0 : 18;
        };
      };
    } SPG_STATUS;

    struct {
      union {
        u32 raw = 0x00000000u;

        struct {
          u32 mhsync_pol : 1;     // Polarity of some signals
          u32 mvsync_pol : 1;     // ..
          u32 mcsync_pol : 1;     // ..
          u32 spg_lock : 1;       // Sync VSync from an external source
          u32 interlace : 1;      //  Whether or not to use interlacing
          u32 force_field2 : 1;   // Force display on field 2 yes/no
          u32 NTSC : 1;           // 1 for NTSC, 0 in VGA Mode
          u32 PAL : 1;            // 1 for PAL, 0 in VGA Mode
          u32 sync_direction : 1; // Sync signal pin as input or output
          u32 csync_on_h : 1;     // Hsync or CSync signal on the HSync pin
          u32 _rsvd0 : 22;
        };
      };
    } SPG_CONTROL;

    struct {
      union {
        u32 raw = 0x01060359u;

        struct {
          u32 hcount : 10; // video clock cycles per line - 1 on vga
          u32 _rsvd0 : 6;
          u32 vcount : 10; // (number of lines per field-1) on vga, (num lines per field/2
                           // - 1) in interlace
          u32 _rsvd1 : 6;
        };
      };
    } SPG_LOAD;

    struct {
      union {
        u32 raw = 0x03F1933Fu;
      };
    } SPG_WIDTH;

    struct {
      union {
        u32 raw = 0x15F28997u;
      };
    } SDRAM_CFG;

    struct {
      union {
        u32 raw = 0x00000020u;
      };
    } SDRAM_REFRESH;

    struct {
      union {
        u32 raw = 0x0000007u;
      };
    } SOFTRESET;

    struct {
      union {
        u32 raw = 0x00000108u;
      };
    } VO_CONTROL;

    struct {
      union {
        u32 raw = 0x0000009Du;
      };
    } VO_STARTX;

    struct {
      union {
        u32 raw = 0x00000015u;
      };
    } VO_STARTY;

    struct {
      union {
        u32 raw = 0x00000400u;
      };
    } SCALER_CTL;

    struct {
      union {
        u32 raw = 0x00000000u;
      };
    } VO_BORDER_COLOR;

    struct {
      union {
        u32 raw = 0x00000000u;

        struct {
          u32 fb_enable : 1;
          u32 fb_line_double : 1;
          u32 fb_depth : 2;
          u32 fb_concat : 3;
          u32 _rsvd0 : 1;
          u32 fb_chroma_threshold : 8;
          u32 fb_stripsize : 6;
          u32 fb_strip_buf_en : 1;
          u32 vclk_div : 1;
          u32 _rsvd1 : 8;
        };
      };
    } FB_R_CTRL;

    struct {
      union {
        u32 raw = 0x00000000u;
      };
    } FB_R_SOF1;

    struct {
      union {
        u32 raw = 0x00000000u;
      };
    } FB_R_SOF2;

    struct {
      union {
        u32 raw = 0x00000000u;
      };
    } FB_R_SIZE;

    struct {
      union {
        u32 raw = 0;
      };
    } FB_W_CTRL;

    struct {
      union {
        u32 raw = 0;
      };
    } FB_W_SOF1;

    struct {
      union {
        u32 raw = 0;
      };
    } FB_W_SOF2;

    struct {
      union {
        u32 raw = 0;
      };
    } FB_W_LINESTRIDE;

    struct {
      union {
        u32 raw = 0;
        float depth;
      };
    } ISP_BACKGND_D;

    struct {
      union {
        u32 raw = 0;

        struct {
          u32 tag_offset : 3;
          u32 tag_address : 21;
          u32 skip : 3;
          u32 shadow : 1;
          u32 cache_bypass : 1;
          u32 _rsvd1 : 3;
        };
      };
    } ISP_BACKGND_T;

    struct {
      u32 raw;
    } PAL_RAM_CTRL;

    u32 REGION_BASE;
    u32 PARAM_BASE;

    u32 TA_OL_BASE;
    u32 TA_OL_LIMIT;

    u32 TA_ISP_BASE;
    u32 TA_ISP_LIMIT;

    u32 TA_LIST_INIT;
    u32 TA_LIST_CONT;
    std::atomic<u32> TA_ITP_CURRENT;

    u32 FPU_PARAM_CFG;
    float FPU_CULL_VAL;

    u32 TA_NEXT_OPB;
    u32 TA_NEXT_OPB_INIT;

    u32 TA_GLOB_TILE_CLIP;

    u32 TA_YUV_TEX_BASE;
    u32 TA_YUV_TEX_CNT;

    // TODO : Should probably make all the registers this way...
    struct {
      union {
        u32 raw;

        struct {
          u32 O_OPB : 2;
          u32 _rsvd0 : 2;
          u32 OM_OPB : 2;
          u32 _rsvd1 : 2;
          u32 T_OPB : 2;
          u32 _rsvd2 : 2;
          u32 TM_OPB : 2;
          u32 _rsvd3 : 2;
          u32 PT_OPB : 2;
          u32 _rsvd4 : 2;
          u32 OPB_MODE : 1;
          u32 _rsvd5 : 11;
        };
      };
    } TA_ALLOC_CTRL;

    union {
      u32 raw;
      struct {
        u32 yuv_u_size : 6;
        u32 _rsvd0 : 2;
        u32 yuv_v_size : 6;
        u32 _rsvd1 : 2;
        u32 yuv_tex : 1;
        u32 _rsvd2 : 7;
        u32 yuv_form : 1;
        u32 _rsvd3 : 7;
      };
    } TA_YUV_TEX_CTRL;

    union {
      u32 raw;
      struct {
        u32 stride : 5;
        u32 _rsvd0 : 3;
        u32 bank : 5;
        u32 _rsvd1 : 3;
        u32 index_endian : 1;
        u32 codebook_endian : 1;
      };
    } TEXT_CONTROL;

    union {
      u32 raw;
      struct {
        u32 blue : 8;
        u32 red : 8;
        u32 green : 8;
        u32 _rsvd : 8;
      };
    } FOG_COL_RAM;

    union {
      u32 raw;
      struct {
        u32 blue : 8;
        u32 red : 8;
        u32 green : 8;
        u32 _rsvd : 8;
      };
    } FOG_COL_VERT;

    union {
      u32 raw;
      struct {
        u32 fog_scale_exponent : 8;
        u32 fog_scale_mantissa : 8;
        u32 _rsvd : 16;
      };
    } FOG_DENSITY;

    union {
      u32 raw;
      struct {
        u32 blue : 8;
        u32 red : 8;
        u32 green : 8;
        u32 alpha : 8;
      };
    } FOG_CLAMP_MAX;

    union {
      u32 raw;
      struct {
        u32 blue : 8;
        u32 red : 8;
        u32 green : 8;
        u32 alpha : 8;
      };
    } FOG_CLAMP_MIN;

  } _regs;

  u64 m_vblank_in_nanos;
};

}
