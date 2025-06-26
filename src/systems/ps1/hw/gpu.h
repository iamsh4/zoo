#pragma once

#include "fox/mmio_device.h"
#include "shared/scheduler.h"
#include "shared/types.h"
#include "systems/ps1/renderer.h"
#include "systems/ps1/hw/gpu_opcodes.h"

namespace zoo::ps1 {

class Console;

class GPU : public fox::MMIODevice {
public:
  union GPUSTAT_Bits {
    struct {
      u32 texture_page_x_base : 4;
      u32 texture_page_y_base : 1;
      u32 semi_transparent : 2;
      u32 texture_page_colors : 2;
      u32 dither_en : 1;
      u32 drawing_allowed : 1;
      u32 set_mask : 1;
      u32 obey_mask : 1;
      u32 interlate_field : 1;
      u32 reverse_flag : 1; // Does crazy stuff, no touch-y.
      u32 texture_disable : 1;
      u32 horizontal_res_2 : 1;
      u32 horizontal_res_1 : 2;
      u32 vertical_res : 1;
      u32 video_mode : 1;
      u32 display_area_color_depth : 1;
      u32 vertical_interlace_en : 1;
      u32 display_disabled : 1;
      u32 interrupt_request : 1;
      u32 dma_request : 1;
      u32 ready_to_receive_cmd : 1;
      u32 ready_to_send_vram_to_cpu : 1;
      u32 ready_to_receive_dma_block : 1;
      u32 dma_direction : 2;
      u32 drawing_even_odd : 1;
    };
    u32 raw;
  };

  struct GPUState {
    bool texture_rect_x_flip;
    bool texture_rect_y_flip;
    u8 texture_window_x_mask;
    u8 texture_window_y_mask;
    u8 texture_window_x_offset;
    u8 texture_window_y_offset;
    u16 drawing_area_left;
    u16 drawing_area_top;
    u16 drawing_area_right;
    u16 drawing_area_bottom;
    i16 drawing_x_offset;
    i16 drawing_y_offset;
    u16 diplay_vram_x_start;
    u16 diplay_vram_y_start;
    u16 display_horiz_start;
    u16 display_horiz_end;
    u16 display_line_start;
    u16 display_line_end;
  };

  enum TextureColorMode
  {
    CLUT4 = 0,
    CLUT8 = 1,
    Direct16 = 2,
  };

  enum class GP0Mode
  {
    Command,
    DataRead,
  };

  enum DMADirection
  {
    DMADirection_Off = 0,
    DMADirection_FIFO = 1,
    DMADirection_CPUToGP0 = 2,
    DMADirection_VRAMToCPU = 3,
  };

  enum DisplayDepth
  {
    DisplayDepth_15Bits = 0,
    DisplayDepth_24Bits,
  };

  struct GPUCommandBuffer {
    gpu::GP0OpcodeData opcode_data;
    std::vector<u32> words;

    void reset()
    {
      words.clear();
      opcode_data = {};
    }
    void consume(u32 word);
    bool is_complete() const;

    template<typename T>
    T *as_cmd()
    {
      return reinterpret_cast<T *>(words.data());
    }

    u8 opcode() const
    {
      return opcode_data.opcode;
    }
  };

  struct GPUFrameDebugData {
    u32 frame;
    std::vector<GPUCommandBuffer> command_buffers;
  };

private:
  /////////////////

  Console *m_console;
  Renderer *m_renderer;

  GPUSTAT_Bits m_gpustat;
  GPUState m_state;

  u8 m_vram[1024 * 1024];
  u8 m_display_vram[1024 * 1024];
  GP0Mode m_gp0_mode = GP0Mode::Command;

  // Used to track the current state of GPUSTAT.bit31
  bool m_line_frame_toggle = 0;

  u32 m_copy_rect_x = 0;
  u32 m_copy_rect_y = 0;

  //
  u32 m_image_store_x = 0;
  u32 m_image_store_y = 0;
  u32 m_image_store_width = 0;
  u32 m_image_store_height = 0;
  u32 m_image_store_current_x = 0;
  u32 m_image_store_current_y = 0;

  GPUCommandBuffer m_command_buffer;

  const size_t num_debug_frames = 3;
  std::deque<GPUFrameDebugData> m_frame_debug_data;
  std::mutex m_frame_debug_data_mutex;
  void push_new_debug_data_frame(const GPUCommandBuffer &);

  u16 gen_texpage() const;

  u32 m_data_transfer_words = 0;

  void gp0_draw_mode_setting();
  void gp0_nop();
  void gp0_set_drawing_area_top_left();
  void gp0_set_drawing_area_bottom_right();
  void gp0_set_drawing_offset();
  void gp0_set_texture_window();
  void gp0_set_mask_bit();
  void gp0_monochrome_polygon();
  void gp0_clear_cache();
  void gp0_copy_rectangle();
  void gp0_copy_rectangle_v2v();
  void gp0_fill_rectangle();
  void gp0_image_store();

  void gp0_shaded_polygon();
  void gp0_monochrome_rectangle();
  void gp0_textured_polygon();
  void gp0_textured_rectangle();
  void gp0_shaded_textured_polygon();

  void gp0_monochrome_line();
  void gp0_shaded_line();

  void gp1_display_mode(u32 word);
  void gp1_soft_reset(u32 word);
  void gp1_dma_direction(u32 word);
  void gp1_set_display_vram_start(u32 word);
  void gp1_set_display_horizontal_range(u32 word);
  void gp1_set_display_vertical_range(u32 word);
  void gp1_display_enable(u32 word);
  void gp1_acknowledge_interrupt(u32 word);
  void gp1_reset_command_buffer(u32 word);

  u32 m_line_counter = 0;
  EventScheduler::Event m_hblank_callback;
  void hblank_callback();

  u32 m_vblank_count = 0;

  void update_renderer_gpu_state();

public:
  GPU(Console *, Renderer *);

  // clang-format off
  u8 read_u8(u32) override          { assert(false); throw std::runtime_error("unhandled read_u8"); }
  u16 read_u16(u32) override        { assert(false); throw std::runtime_error("unhandled read_u16"); }
  void write_u8(u32, u8) override   { assert(false); throw std::runtime_error("unhandled write_u8"); }
  void write_u16(u32, u16) override { assert(false); throw std::runtime_error("unhandled write_u16"); }
  // clang-format on

  u32 read_u32(u32 addr) override;
  void write_u32(u32 addr, u32 value) override;
  void register_regions(fox::MemoryTable *memory) override;

  void get_display_vram_bounds(u32 *tl_x, u32* tl_y, u32* br_x, u32* br_y);

  void reset();

  void gp0(u32 word);
  void gp1(u32 word);
  u32 gpuread();

  const u8 *vram_ptr();
  const u8 *display_vram_ptr();
  void dump_vram_ppm(const char *path);

  u32 frame_data(GPUFrameDebugData *out, u32 num_frames);
};

} // namespace zoo::ps1
