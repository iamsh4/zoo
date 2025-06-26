#include <fmt/core.h>

#include "shared/bitmanip.h"
#include "shared/profiling.h"
#include "systems/ps1/hw/gpu.h"
#include "systems/ps1/hw/gpu_opcodes.h"
#include "systems/ps1/console.h"

namespace zoo::ps1 {

using namespace gpu;

// http://hitmen.c02.at/files/docs/psx/gpu.txt

// Port            Name    Expl.
// 1F801810h-Write GP0     Send GP0 Commands/Packets (Rendering and VRAM Access)
// 1F801814h-Write GP1     Send GP1 Commands (Display Control) (and DMA Control)
// 1F801810h-Read  GPUREAD Receive responses to GP0(C0h) and GP1(10h) commands
// 1F801814h-Read  GPUSTAT Receive GPU Status Register

GPU::GPU(Console *console, Renderer *renderer)
  : m_console(console),
    m_renderer(renderer),
    m_hblank_callback("gpu.hblank",
                      std::bind(&GPU::hblank_callback, this),
                      console->scheduler())
{
  m_gpustat.vertical_interlace_en = true;
  m_console->schedule_event(1 * 1000 * 1000, &m_hblank_callback);

  m_command_buffer.reset();
  m_frame_debug_data.push_front(GPUFrameDebugData { .frame = 0 });

  auto *registry = m_console->mmio_registry();

  registry->setup("GPU", "VBlank Count", &m_vblank_count);
}

void
GPU::hblank_callback()
{
  // NTSC: 263 scanlines per frame
  // NTSC: 3413 video cycles per scanline
  // Video clock : 53.222400MHz
  const u64 scanline_nanos = 64'127;

  m_line_counter = (m_line_counter + 1) % 263;

  if (m_gpustat.vertical_interlace_en && m_gpustat.vertical_res) {
    // changes once per frame
    if (m_line_counter == 0) {
      m_line_frame_toggle = !m_line_frame_toggle;
    }
  } else {
    // changes once per scanline
    m_line_frame_toggle = !m_line_frame_toggle;
  }

  m_gpustat.drawing_even_odd = m_line_frame_toggle;

  // additionally, always low in vsync region
  const bool in_vsync_region = m_line_counter < 23;
  if (in_vsync_region) {
    m_gpustat.drawing_even_odd = 0;
  }

  if (m_line_counter == 0) {
    m_vblank_count++;
    m_console->irq_control()->raise(interrupts::VBlank);
    printf("Raise vblank\n");

    m_renderer->sync_gpu_to_renderer(m_vram);
    m_renderer->sync_renderer_to_gpu(m_vram);

    memcpy(m_display_vram, m_vram, sizeof(m_vram));

    // Start the next debug data bucket for this next frame
    {
      std::lock_guard lock(m_frame_debug_data_mutex);
      m_frame_debug_data.push_front(GPUFrameDebugData { .frame = m_vblank_count });
      // Get rid of any buckets which are too old
      while (m_frame_debug_data.size() > num_debug_frames) {
        m_frame_debug_data.pop_back();
      }
    }
  }

  m_console->schedule_event_nanos(scanline_nanos, &m_hblank_callback);
}

void
GPU::register_regions(fox::MemoryTable *memory)
{
  // https://psx-spx.consoledev.net/graphicsprocessingunitgpu/#gpu-io-ports-dma-channels-commands-vram
  memory->map_mmio(0x1F80'1810, 8, "GPU IO Ports", this);
}

void
GPU::write_u32(u32 addr, u32 value)
{
  ProfileZone;
  switch (addr) {
    case 0x1f80'1810:
      printf("Write to GP0 (0x%08x) < 0x%08x\n", addr, value);
      gp0(value);
      break;

    case 0x1f80'1814:
      printf("Write to GP1 (0x%08x) < 0x%08x\n", addr, value);
      gp1(value);
      break;

    default:
      // Impossible, only two aligned addresses, MemoryTable enforces aligned access.
      assert(false);
      break;
  }
}

u32
GPU::read_u32(u32 addr)
{
  ProfileZone;
  u32 value = 0;

  switch (addr) {
    case 0x1f80'1810:
      value = gpuread();
      printf("Read from GPUREAD (0x%08x) > 0x%08x\n", addr, value);
      break;

    case 0x1f80'1814:
      // printf("Read from GPUSTAT (0x%08x) > 0x%08x\n", addr, value);
      // XXX : Properly handle 'DMA Ready'
      // XXX : Properly handle 'GPU ready to receive dma commands' (b28)

      GPUSTAT_Bits bits;
      bits.raw = 0;
      bits.ready_to_receive_dma_block = 1;
      bits.ready_to_send_vram_to_cpu = 1;
      bits.ready_to_receive_cmd = 1;
      bits.drawing_even_odd = m_gpustat.drawing_even_odd;
      value = bits.raw;
      break;

    default:
      // Impossible, only two aligned addresses, MemoryTable enforces aligned access.
      throw std::runtime_error("Unhandled GPU read_u32");
      break;
  }

  return value;
}

void
GPU::GPUCommandBuffer::consume(u32 word)
{
  words.push_back(word);
}

bool
GPU::GPUCommandBuffer::is_complete() const
{
  if (opcode_data.words.size() == 0) {
    // This is to handle initial/uninitialized state.
    return true;
  } else if (!opcode_data.uses_termination) {
    return opcode_data.words.size() == words.size();
  } else {
    // 'Polyline'. Need to confirm we got the initial required words and the
    // terminator
    const bool has_preliminary_words = words.size() >= opcode_data.words.size();
    // TODO : check that the terminator is at the correct position in the stream.
    // Currently a position of 0x55555555 could be 'terminator'
    const u32 TERMINATOR = 0x5555'5555;
    const bool has_terminator = words.size() > 0 && words[words.size() - 1] == TERMINATOR;
    return has_preliminary_words && has_terminator;
  }
}

void
GPU::gp0(u32 word)
{
  if (m_gp0_mode == GP0Mode::Command) {
    ProfileZoneNamed("GP0Command");

    // If command buffer is already complete
    const bool is_new_command = m_command_buffer.is_complete();
    if (is_new_command) {
      m_command_buffer.opcode_data = decode_gp0_opcode(word);
      m_command_buffer.words.clear();
    }

    m_command_buffer.consume(word);

    // If that was the last word for this command, then process the command.
    if (m_command_buffer.is_complete()) {

      push_new_debug_data_frame(m_command_buffer);

      // const auto cmd_flags = m_command_buffer.opcode_data.flags;
      // const bool cmd_polygon = cmd_flags & GP0OpcodeData::Flags::RenderPolygon;
      // const bool cmd_line = cmd_flags & GP0OpcodeData::Flags::RenderLine;
      // const bool cmd_rectangle = cmd_flags & GP0OpcodeData::Flags::RenderRectangle;
      // const bool cmd_shaded = cmd_flags & GP0OpcodeData::Flags::Shaded;
      // const bool cmd_textured = cmd_flags & GP0OpcodeData::Flags::Textured;
      // const bool cmd_polygon = cmd_flags & GP0OpcodeData::Flags::RenderPolygon;

      // if (cmd_polygon) {
      //   if (cmd_shaded) {
      //     if (cmd_textured)
      //       gp0_shaded_textured_polygon();
      //     else
      //       gp0_shaded_polygon();
      //   } else
      //     gp0_monochrome_polygon();
      // } else if (cmd_line) {
      //   if (cmd_shaded)
      //     gp0_shaded_line();
      //   else
      //     gp0_monochrome_line();
      // } else if (cmd_rectangle) {
      //   if (cmd_textured)
      //     gp0_textured_rectangle();
      //   else
      //     gp0_monochrome_rectangle();
      // } else

      switch (m_command_buffer.opcode_data.opcode) {
        case Opcodes::GP0_Nop:
          gp0_nop();
          break;
        case Opcodes::GP0_ClearCache:
          gp0_clear_cache();
          break;
        case Opcodes::GP0_FillRectangle:
          gp0_fill_rectangle();
          break;
        case Opcodes::GP0_DrawModeSetting:
          gp0_draw_mode_setting();
          break;
        case Opcodes::GP0_SetDrawingAreaTopLeft:
          gp0_set_drawing_area_top_left();
          break;
        case Opcodes::GP0_SetDrawingAreaBottomRight:
          gp0_set_drawing_area_bottom_right();
          break;
        case Opcodes::GP0_SetDrawingOffset:
          gp0_set_drawing_offset();
          break;
        case Opcodes::GP0_SetTextureWindow:
          gp0_set_texture_window();
          break;
        case Opcodes::GP0_SetMaskBit:
          gp0_set_mask_bit();
          break;
        case Opcodes::GP0_CopyRectangleV2C:
          gp0_image_store();
          break;

        case Opcodes::GP0_CopyRectangleV2V:
          gp0_copy_rectangle_v2v();
          break;

        case Opcodes::GP0_MonochromePolygon3_Opaque:
        case Opcodes::GP0_MonochromePolygon3_SemiTransparent:
        case Opcodes::GP0_MonochromePolygon4_Opaque:
        case Opcodes::GP0_MonochromePolygon4_SemiTransparent:
        case 0x21:
          gp0_monochrome_polygon();
          break;

        case Opcodes::GP0_TexturedPolygon3_OpaqueTextureBlending:
        case Opcodes::GP0_TexturedPolygon3_OpaqueTexture:
        case Opcodes::GP0_TexturedPolygon3_SemiTransparentTextureBlending:
        case Opcodes::GP0_TexturedPolygon3_SemiTransparentTexture:
        case Opcodes::GP0_TexturedPolygon4_OpaqueTextureBlending:
        case Opcodes::GP0_TexturedPolygon4_OpaqueTexture:
        case Opcodes::GP0_TexturedPolygon4_SemiTransparentTextureBlending:
        case Opcodes::GP0_TexturedPolygon4_SemiTransparentTexture:
          gp0_textured_polygon();
          break;

        case Opcodes::GP0_MonochromeRectangle_DotOpaque:
        case Opcodes::GP0_MonochromeRectangle_VariableSizeOpaque:
        case Opcodes::GP0_MonochromeRectangle_VariableSizeTranslucent:
          gp0_monochrome_rectangle();
          break;

        case Opcodes::GP0_TexturedRectangle_VariableSizeOpaqueTextureBlending:
        case Opcodes::GP0_TexturedRectangle_VariableSizeOpaqueRawTexture:
        case Opcodes::GP0_TexturedRectangle_VariableSizeSemiTransparentRawTexture:
        case Opcodes::GP0_TexturedRectangle_16x16OpaqueTextureBlending:
        case 0x67:
        case 0x75:
        case 0x7d:
          gp0_textured_rectangle();
          break;

        case Opcodes::GP0_ShadedPolygon3_Opaque:
        case Opcodes::GP0_ShadedPolygon3_SemiTransparent:
        case Opcodes::GP0_ShadedPolygon4_Opaque:
        case Opcodes::GP0_ShadedPolygon4_SemiTransparent:
          gp0_shaded_polygon();
          break;

        case Opcodes::GP0_CopyRectangleC2V:
          gp0_copy_rectangle();
          break;

        case 0x34:
        case Opcodes::GP0_ShadedTexturedPolygon_FourPointOpaqueTexBlend:
        case Opcodes::GP0_ShadedTexturedPolygon_FourPointSemiTransparentTexBlend:
          gp0_shaded_textured_polygon();
          break;

        case 0x42: // HACK, incorrect
        case Opcodes::GP0_MonochromeLineOpaque:
          gp0_monochrome_line();
          break;

        case 0x55: // xxx
        case Opcodes::GP0_ShadedLineOpaque:
          gp0_shaded_line();
          break;

        default:
          throw std::runtime_error(
            fmt::format("XXX : Unhandled GP0 command 0x{:08x} (opcode={:02x})\n",
                        m_command_buffer.words[0],
                        m_command_buffer.opcode_data.opcode));
          break;
      }
    }

  } else if (m_gp0_mode == GP0Mode::DataRead) {
    ProfileZoneNamed("GP0DataRead");

    // TODO : Transfer word
    auto *cmd = m_command_buffer.as_cmd<Command_GP0_CopyRectangle>();

    // 'y' is just vanilla lines, but x is tracked in 16b 'halfwords'
    const u32 current_y = cmd->topleft.y + m_copy_rect_y;
    const u32 current_x = cmd->topleft.x + m_copy_rect_x;

    u32 vram_addr = (1024 * sizeof(u16) * current_y) + current_x * sizeof(u16);
    memcpy(&m_vram[vram_addr], &word, sizeof(word));

    // Each word consumed carries two texels of data.
    m_copy_rect_x += 2;
    if (m_copy_rect_x == cmd->size.width) {
      m_copy_rect_x = 0;
      m_copy_rect_y++;
    }

    // Tick down the number of remaining words, go back to command mode if we're done.
    m_data_transfer_words--;
    if (m_data_transfer_words == 0) {
      m_copy_rect_x = 0;
      m_copy_rect_y = 0;

      m_gp0_mode = GP0Mode::Command;
      m_command_buffer.reset();
    }

  } else {
    assert(0);
  }
}

void
GPU::gp1(u32 word)
{
  ProfileZone;
  const u8 opcode = word >> 24;
  switch (opcode) {
    case Opcodes::GP1_SoftReset:
      gp1_soft_reset(word);
      break;
    case Opcodes::GP1_DisplayMode:
      gp1_display_mode(word);
      break;
    case Opcodes::GP1_DMADirection:
      gp1_dma_direction(word);
      break;
    case Opcodes::GP1_SetDispayVRAMStart:
      gp1_set_display_vram_start(word);
      break;
    case Opcodes::GP1_SetDisplayHorizontalRange:
      gp1_set_display_horizontal_range(word);
      break;
    case Opcodes::GP1_SetDisplayVerticalRange:
      gp1_set_display_vertical_range(word);
      break;
    case Opcodes::GP1_DisplayEnable:
      gp1_display_enable(word);
      break;
    case Opcodes::GP1_AcknowledgeInterrupt:
      gp1_acknowledge_interrupt(word);
      break;
    case Opcodes::GP1_ResetCommandBuffer:
      gp1_reset_command_buffer(word);
      break;

    case 0x10:
      // XXX : GPU info (caller will now read whatever isin GPUREAD, probably zero, which
      // is fine.)
      break;

    default:
      throw std::runtime_error(
        fmt::format("Unhandled GP1 opcode {} (word=0x{:08x})", opcode, word));
      break;
  }
}

void
GPU::gp0_draw_mode_setting()
{
  ProfileZone;
  printf(" - gp0(0x%02x) DrawModeSetting\n", m_command_buffer.opcode());

  auto *cmd = m_command_buffer.as_cmd<Command_GP0_DrawModeSetting>();
  m_gpustat.dither_en = cmd->dither_en;
  m_gpustat.drawing_allowed = cmd->drawing_allowed;
  m_gpustat.semi_transparent = cmd->semi_transparent;
  m_gpustat.texture_disable = cmd->texture_disable;
  m_gpustat.texture_page_colors = cmd->texture_page_colors;
  m_gpustat.texture_page_x_base = cmd->texture_page_x_base;
  m_gpustat.texture_page_y_base = cmd->texture_page_y_base;
  m_state.texture_rect_x_flip = cmd->texture_rect_x_flip;
  m_state.texture_rect_y_flip = cmd->texture_rect_y_flip;
}

void
GPU::gp0_nop()
{
  ProfileZone;
}

void
GPU::gp0_set_drawing_area_top_left()
{
  ProfileZone;
  auto *cmd = m_command_buffer.as_cmd<Command_GP0_DrawingArea>();
  printf(" - gp0(0x%02x) drawing_area_top_left(%u,%u)\n",
         m_command_buffer.opcode(),
         cmd->x_coord,
         cmd->y_coord);

  m_state.drawing_area_top = cmd->y_coord;
  m_state.drawing_area_left = cmd->x_coord;
  update_renderer_gpu_state();
}

void
GPU::gp0_set_drawing_area_bottom_right()
{
  ProfileZone;
  auto *cmd = m_command_buffer.as_cmd<Command_GP0_DrawingArea>();
  printf(" - gp0(0x%02x) drawing_area_bottom_right(%u,%u)\n",
         m_command_buffer.opcode(),
         cmd->x_coord,
         cmd->y_coord);
  m_state.drawing_area_bottom = cmd->y_coord;
  m_state.drawing_area_right = cmd->x_coord;
  update_renderer_gpu_state();
}

void
GPU::update_renderer_gpu_state()
{
  Renderer::GPUState gpu_state = {};
  // Drawing/clipping area
  gpu_state.drawing_area.top_left = m_state.drawing_area_left & 0xffff;
  gpu_state.drawing_area.top_left |= u32(m_state.drawing_area_top) << 16;
  gpu_state.drawing_area.bottom_right = m_state.drawing_area_right & 0xffff;
  gpu_state.drawing_area.bottom_right |= u32(m_state.drawing_area_bottom) << 16;
  // Drawing offset for all primitives
  gpu_state.drawing_offset = m_state.drawing_x_offset;
  gpu_state.drawing_offset |= u32(m_state.drawing_y_offset) << 16;

  m_renderer->update_gpu_state(Renderer::CmdUpdateGPUState { .gpu_state = gpu_state });
}

void
GPU::gp0_set_drawing_offset()
{
  ProfileZone;
  auto *cmd = m_command_buffer.as_cmd<Command_GP0_DrawingOffset>();
  // 11-bit signed -> 16-bit signed
  m_state.drawing_x_offset = ((i16)(cmd->x_offset << 5)) >> 5;
  m_state.drawing_y_offset = ((i16)(cmd->y_offset << 5)) >> 5;
  printf(" - gp0(0x%02x) drawing_offset(%d,%d)\n",
         m_command_buffer.opcode(),
         m_state.drawing_x_offset,
         m_state.drawing_y_offset);

  m_renderer->sync_gpu_to_renderer(m_vram);
  m_renderer->sync_renderer_to_gpu(m_vram);
}

void
GPU::gp0_set_texture_window()
{
  ProfileZone;
  auto *cmd = m_command_buffer.as_cmd<Command_GP0_TextureWindowSetting>();
  m_state.texture_window_x_mask = cmd->window_mask_x;
  m_state.texture_window_y_mask = cmd->window_mask_y;
  m_state.texture_window_x_offset = cmd->window_offset_x;
  m_state.texture_window_y_offset = cmd->window_offset_y;

  printf("window: xm %x ym %x xo %u yo %u\n",
         m_state.texture_window_x_mask,
         m_state.texture_window_y_mask,
         m_state.texture_window_x_offset,
         m_state.texture_window_y_offset);
}

void
GPU::gp0_set_mask_bit()
{
  ProfileZone;
  auto *cmd = m_command_buffer.as_cmd<Command_GP0_MaskBitSetting>();
  m_gpustat.set_mask = cmd->set_mask;
  m_gpustat.obey_mask = cmd->check_mask;
}

void
GPU::gp0_copy_rectangle_v2v()
{
  ProfileZone;
  auto *cmd = m_command_buffer.as_cmd<Command_GP0_CopyRectangleV2V>();

  // Complete any pending renderer work.
  // TODO : Be more efficient.
  m_renderer->sync_gpu_to_renderer(m_vram);
  m_renderer->sync_renderer_to_gpu(m_vram);

  // Do the actual blit/copy operations
  // TODO : These could also just be in the renderer...
  for (u16 i = 0; i < cmd->size.width; ++i)
    for (u16 j = 0; j < cmd->size.height; ++j) {
      // XXX : Handle mask bits
      const int src_xy[2] = { cmd->source.x + i, cmd->source.y + j };
      const int dst_xy[2] = { cmd->dest.x + i, cmd->dest.y + j };

      const bool src_valid = src_xy[0] < 1024 && src_xy[1] < 512;
      const bool dst_valid = dst_xy[0] < 1024 && dst_xy[1] < 512;
      if (src_valid && dst_valid) {
        const u32 vram_src = src_xy[1] * 1024 + src_xy[0];
        const u32 vram_dst = dst_xy[1] * 1024 + dst_xy[0];

        u16 *vram16 = (u16 *)m_vram;
        vram16[vram_dst] = vram16[vram_src];
      }
    }

  m_renderer->sync_gpu_to_renderer(m_vram);
  m_renderer->sync_renderer_to_gpu(m_vram);
}

void
GPU::gp0_monochrome_polygon()
{
  ProfileZone;
  auto *cmd = m_command_buffer.as_cmd<Command_GP0_MonochromePolygon>();

  const u8 opcode = m_command_buffer.opcode();
  const bool is_quad = opcode == 0x28 || opcode == 0x2a;

  printf("gpu: monopoly (quad=%u) r=%02x g=%02x b=%02x (0x%08x)\n",
         is_quad,
         cmd->color.r,
         cmd->color.g,
         cmd->color.b,
         cmd->color.raw);
  printf("     - (%d,%d) - (%d,%d) - (%d,%d) - (%d,%d)\n",
         cmd->vertex1.x,
         cmd->vertex1.y,
         cmd->vertex2.x,
         cmd->vertex2.y,
         cmd->vertex3.x,
         cmd->vertex3.y,
         cmd->vertex4.x,
         cmd->vertex4.y);

  m_renderer->push_triangle({
    .pos1 = { cmd->vertex1.x, cmd->vertex1.y },
    .pos2 = { cmd->vertex2.x, cmd->vertex2.y },
    .pos3 = { cmd->vertex3.x, cmd->vertex3.y },
    .color1 = cmd->color.raw,
    .color2 = cmd->color.raw,
    .color3 = cmd->color.raw,
    .opcode = opcode,
  });
  if (is_quad) {
    m_renderer->push_triangle({
      .pos1 = { cmd->vertex2.x, cmd->vertex2.y },
      .pos2 = { cmd->vertex3.x, cmd->vertex3.y },
      .pos3 = { cmd->vertex4.x, cmd->vertex4.y },
      .color1 = cmd->color.raw,
      .color2 = cmd->color.raw,
      .color3 = cmd->color.raw,
      .opcode = opcode,
    });
  }
}

void
GPU::gp0_textured_polygon()
{
  ProfileZone;
  auto *cmd = m_command_buffer.as_cmd<Command_GP0_TexturedPolygon>();

  const u8 opcode = m_command_buffer.opcode();
  const bool is_quad = opcode >= 0x2c && opcode <= 0x2f;

  printf("gpu: textured_polygon (quad=%u, colormode=0x%x)\n",
         is_quad,
         (cmd->texpage2.texpage >> 7) & 3);
  printf(" -- verts: %d %d %d %d %d %d %d %d\n",
         cmd->vertex1.x,
         cmd->vertex1.y,
         cmd->vertex2.x,
         cmd->vertex2.y,
         cmd->vertex3.x,
         cmd->vertex3.y,
         cmd->vertex4.x,
         cmd->vertex4.y);

  m_renderer->push_triangle({
    .pos1 = { cmd->vertex1.x, cmd->vertex1.y },
    .pos2 = { cmd->vertex2.x, cmd->vertex2.y },
    .pos3 = { cmd->vertex3.x, cmd->vertex3.y },
    .color1 = cmd->color.raw,
    .color2 = cmd->color.raw,
    .color3 = cmd->color.raw,
    .tex1 = { (i16)cmd->texpal1.x, (i16)cmd->texpal1.y },
    .tex2 = { (i16)cmd->texpage2.x, (i16)cmd->texpage2.y },
    .tex3 = { (i16)cmd->tex3.x, (i16)cmd->tex3.y },
    .tex_page = u16(cmd->texpage2.texpage),
    .clut_xy = u16(cmd->texpal1.clut),
    .opcode = opcode,
  });

  if (is_quad) {
    m_renderer->push_triangle({
      .pos1 = { cmd->vertex2.x, cmd->vertex2.y },
      .pos2 = { cmd->vertex3.x, cmd->vertex3.y },
      .pos3 = { cmd->vertex4.x, cmd->vertex4.y },
      .color1 = cmd->color.raw,
      .color2 = cmd->color.raw,
      .color3 = cmd->color.raw,
      .tex1 = { (i16)cmd->texpage2.x, (i16)cmd->texpage2.y },
      .tex2 = { (i16)cmd->tex3.x, (i16)cmd->tex3.y },
      .tex3 = { (i16)cmd->tex4.x, (i16)cmd->tex4.y },
      .tex_page = u16(cmd->texpage2.texpage),
      .clut_xy = u16(cmd->texpal1.clut),
      .opcode = opcode,
    });
  }
}

void
GPU::gp0_monochrome_rectangle()
{
  ProfileZone;
  auto *cmd = m_command_buffer.as_cmd<Command_GP0_MonochromeRectangle>();

  const u8 opcode = m_command_buffer.opcode();

  if (opcode == 0x60 || opcode == 0x62) {
    //
  } else if (opcode == 0x68 || opcode == 0x6a) {
    cmd->width = 1;
    cmd->height = 1;
  } else {
    assert(false);
  }

  i16 v1[2] = { i16(cmd->vertex.x), i16(cmd->vertex.y) };
  i16 v2[2] = { i16(cmd->vertex.x), i16(cmd->vertex.y + cmd->height) };
  i16 v3[2] = { i16(cmd->vertex.x + cmd->width), i16(cmd->vertex.y) };
  i16 v4[2] = { i16(cmd->vertex.x + cmd->width), i16(cmd->vertex.y + cmd->height) };

  m_renderer->push_triangle({
    .pos1 = { v1[0], v1[1] },
    .pos2 = { v2[0], v2[1] },
    .pos3 = { v3[0], v3[1] },
    .color1 = cmd->color.raw,
    .color2 = cmd->color.raw,
    .color3 = cmd->color.raw,
    .opcode = opcode,
  });
  m_renderer->push_triangle({
    .pos1 = { v2[0], v2[1] },
    .pos2 = { v3[0], v3[1] },
    .pos3 = { v4[0], v4[1] },
    .color1 = cmd->color.raw,
    .color2 = cmd->color.raw,
    .color3 = cmd->color.raw,
    .opcode = opcode,
  });
}

void
GPU::gp0_textured_rectangle()
{
  ProfileZone;
  auto *cmd = m_command_buffer.as_cmd<Command_GP0_TexturedRectangle>();
  const u8 opcode = m_command_buffer.opcode();

  i16 w, h;
  if (m_command_buffer.opcode_data.flags & GP0OpcodeData::Flags::Size1) {
    w = h = 1;
  } else if (m_command_buffer.opcode_data.flags & GP0OpcodeData::Flags::Size8) {
    w = h = 8;
  } else if (m_command_buffer.opcode_data.flags & GP0OpcodeData::Flags::Size16) {
    w = h = 16;
  } else if (m_command_buffer.opcode_data.flags & GP0OpcodeData::Flags::SizeVariable) {
    w = cmd->width;
    h = cmd->height;
  } else {
    assert(false);
    throw std::runtime_error("PS1 GPU: Unhandled TexturedRectangle Size");
  }

  u32 color = 0xff7f'7f7f;

  i16 x = cmd->vertex.x;
  i16 y = cmd->vertex.y;
  i16 u = cmd->texpal.x;
  i16 v = cmd->texpal.y;

  // printf("gpu: textured rectangle ()\n");
  // printf("textured_rectangle texpage 0x%04x\n", m_gpustat.raw & 0xffff);
  // printf("textured_rectangle x=%d y=%d w=%d h=%d\n", x, y, w, h);

  m_renderer->push_triangle({
    .pos1 = { x, y },
    .pos2 = { x, i16(y + h) },
    .pos3 = { i16(x + w), y },
    .color1 = color,
    .color2 = color,
    .color3 = color,
    .tex1 = { i16(u), i16(v) },
    .tex2 = { i16(u), i16(v + h) },
    .tex3 = { i16(u + w), i16(v) },
    .tex_page = gen_texpage(),
    .clut_xy = u16(cmd->texpal.clut),
    .opcode = opcode,
  });
  m_renderer->push_triangle({
    .pos1 = { i16(x), i16(y + h) },
    .pos2 = { i16(x + w), i16(y + h) },
    .pos3 = { i16(x + w), y },
    .color1 = color,
    .color2 = color,
    .color3 = color,
    .tex1 = { i16(u), i16(v + h) },
    .tex2 = { i16(u + w), i16(v + h) },
    .tex3 = { i16(u + w), i16(v) },
    .tex_page = gen_texpage(),
    .clut_xy = u16(cmd->texpal.clut),
    .opcode = opcode,
  });
}

u16
GPU::gen_texpage() const
{
  u16 texpage = 0;
  texpage |= m_gpustat.raw & 0xffff;
  // TODO : Rectangle flip
  return texpage;
}

void
GPU::gp0_shaded_textured_polygon()
{
  ProfileZone;
  auto *cmd = m_command_buffer.as_cmd<Command_GP0_ShadedTexturedPolygon>();
  const u8 opcode = m_command_buffer.opcode();
  // assert(opcode == 0x3e || opcode == 0x3c);
  bool is_quad = opcode >= 0x3c;

  // printf("gpu: shaded_textured_polygon (quad=%u, colormode=0x%x)\n",
  //        is_quad,
  //        (cmd->texpage2.texpage >> 7) & 3);
  // printf(" -- verts: %d %d %d %d %d %d %d %d\n",
  //        cmd->vertex1.x,
  //        cmd->vertex1.y,
  //        cmd->vertex2.x,
  //        cmd->vertex2.y,
  //        cmd->vertex3.x,
  //        cmd->vertex3.y,
  //        cmd->vertex4.x,
  //        cmd->vertex4.y);

  m_renderer->push_triangle({
    .pos1 = { cmd->vertex1.x, cmd->vertex1.y },
    .pos2 = { cmd->vertex2.x, cmd->vertex2.y },
    .pos3 = { cmd->vertex3.x, cmd->vertex3.y },
    .color1 = cmd->color.raw,
    .color2 = cmd->color.raw,
    .color3 = cmd->color.raw,
    .tex1 = { (i16)cmd->texpal1.x, (i16)cmd->texpal1.y },
    .tex2 = { (i16)cmd->texpage2.x, (i16)cmd->texpage2.y },
    .tex3 = { (i16)cmd->tex3.x, (i16)cmd->tex3.y },
    .tex_page = u16(cmd->texpage2.texpage),
    .clut_xy = u16(cmd->texpal1.clut),
    .opcode = opcode,
  });

  if (is_quad) {
    m_renderer->push_triangle({
      .pos1 = { cmd->vertex2.x, cmd->vertex2.y },
      .pos2 = { cmd->vertex3.x, cmd->vertex3.y },
      .pos3 = { cmd->vertex4.x, cmd->vertex4.y },
      .color1 = cmd->color.raw,
      .color2 = cmd->color.raw,
      .color3 = cmd->color.raw,
      .tex1 = { (i16)cmd->texpage2.x, (i16)cmd->texpage2.y },
      .tex2 = { (i16)cmd->tex3.x, (i16)cmd->tex3.y },
      .tex3 = { (i16)cmd->tex4.x, (i16)cmd->tex4.y },
      .tex_page = u16(cmd->texpage2.texpage),
      .clut_xy = u16(cmd->texpal1.clut),
      .opcode = opcode,
    });
  }
}

u32
gpu_color_to_u32(Color col)
{
  ProfileZone;
  u32 result = 0;
  result |= (col.b) << 0;
  result |= (col.g) << 8;
  result |= (col.r) << 16;
  return result;
}

void
GPU::gp0_shaded_polygon()
{
  ProfileZone;
  auto *cmd = m_command_buffer.as_cmd<Command_GP0_ShadedPolygon>();

  const u8 opcode = m_command_buffer.opcode();
  const bool is_quad = opcode == 0x38 || opcode == 0x3a;

  // printf("gpu: shadedpoly (quad=%u)\n", is_quad);
  // printf("      - coord=(%d, %d) color=(0x%02x,0x%02x,0x%02x)\n",
  //        cmd->vertex1.x,
  //        cmd->vertex1.y,
  //        cmd->color1.r,
  //        cmd->color1.g,
  //        cmd->color1.b);
  // printf("      - coord=(%d, %d) color=(0x%02x,0x%02x,0x%02x)\n",
  //        cmd->vertex2.x,
  //        cmd->vertex2.y,
  //        cmd->color2.r,
  //        cmd->color2.g,
  //        cmd->color2.b);
  // printf("      - coord=(%d, %d) color=(0x%02x,0x%02x,0x%02x)\n",
  //        cmd->vertex3.x,
  //        cmd->vertex3.y,
  //        cmd->color3.r,
  //        cmd->color3.g,
  //        cmd->color3.b);
  // if (is_quad) {
  //   printf("      - coord=(%d, %d) color=(0x%02x,0x%02x,0x%02x)\n",
  //          cmd->vertex4.x,
  //          cmd->vertex4.y,
  //          cmd->color4.r,
  //          cmd->color4.g,
  //          cmd->color4.b);
  // }

  m_renderer->push_triangle({
    .pos1 = { cmd->vertex1.x, cmd->vertex1.y },
    .pos2 = { cmd->vertex2.x, cmd->vertex2.y },
    .pos3 = { cmd->vertex3.x, cmd->vertex3.y },
    .color1 = gpu_color_to_u32(cmd->color1),
    .color2 = gpu_color_to_u32(cmd->color2),
    .color3 = gpu_color_to_u32(cmd->color3),
    .tex_page = gen_texpage(),
    .opcode = opcode,
  });
  if (is_quad) {
    m_renderer->push_triangle({
      .pos1 = { cmd->vertex2.x, cmd->vertex2.y },
      .pos2 = { cmd->vertex3.x, cmd->vertex3.y },
      .pos3 = { cmd->vertex4.x, cmd->vertex4.y },
      .color1 = gpu_color_to_u32(cmd->color2),
      .color2 = gpu_color_to_u32(cmd->color3),
      .color3 = gpu_color_to_u32(cmd->color4),
      .tex_page = gen_texpage(),
      .opcode = opcode,
    });
  }
}

void
GPU::gp0_clear_cache()
{
  ProfileZone;
  // XXX : Not implemented
}

void
GPU::gp0_copy_rectangle()
{
  ProfileZone;
  auto *cmd = m_command_buffer.as_cmd<Command_GP0_CopyRectangle>();

  u32 image_size = cmd->size.width * cmd->size.height;

  // Image size is given in terms of half-words per texel. If the image size has an odd
  // number of pixels, we need to round up because we transfer 32bits at a time.
  image_size = (image_size + 1) & ~1;

  // Setup data read mode for GP0.
  m_data_transfer_words = image_size / 2;
  m_gp0_mode = GP0Mode::DataRead;
}

void
GPU::gp0_fill_rectangle()
{
  ProfileZone;
  auto *cmd = m_command_buffer.as_cmd<Command_GP0_FillRectangle>();
  const u8 opcode = m_command_buffer.opcode();

  printf(" - gp0(0x%02x) fill_rectangle(%u,%u,%u,%u) color=(%u,%u,%u)\n",
         opcode,
         cmd->topleft.x,
         cmd->topleft.y,
         cmd->size.width,
         cmd->size.height,
         cmd->color.r,
         cmd->color.g,
         cmd->color.b);

  i16 top_left[2] = { (i16)cmd->topleft.x, (i16)cmd->topleft.y };
  i16 size[2] = { (i16)cmd->size.width, (i16)cmd->size.height };

  i16 points[4][2] = {
    { top_left[0], top_left[1] },                                   // TL
    { top_left[0], (i16)(top_left[1] + size[1]) },                  // BL
    { (i16)(top_left[0] + size[0]), top_left[1] },                  // TR
    { (i16)(top_left[0] + size[0]), (i16)(top_left[1] + size[1]) }, // BR
  };

  m_renderer->push_triangle({
    .pos1 = { points[0][0], points[0][1] },
    .pos2 = { points[1][0], points[1][1] },
    .pos3 = { points[2][0], points[2][1] },
    .color1 = gpu_color_to_u32(cmd->color),
    .color2 = gpu_color_to_u32(cmd->color),
    .color3 = gpu_color_to_u32(cmd->color),
    .opcode = opcode,
  });
  m_renderer->push_triangle({
    .pos1 = { points[1][0], points[1][1] },
    .pos2 = { points[3][0], points[3][1] },
    .pos3 = { points[2][0], points[2][1] },
    .color1 = gpu_color_to_u32(cmd->color),
    .color2 = gpu_color_to_u32(cmd->color),
    .color3 = gpu_color_to_u32(cmd->color),
    .opcode = opcode,
  });
}

void
GPU::gp0_monochrome_line()
{
  ProfileZone;
  // auto *cmd = m_command_buffer.as_cmd<Command_GP0_MonochromeLine>();
  // XXX
}

void
GPU::gp0_shaded_line()
{
  ProfileZone;
  // auto *cmd = m_command_buffer.as_cmd<Command_GP0_ShadedLine>();
  // XXX
}

void
GPU::gp0_image_store()
{
  ProfileZone;
  auto *cmd = m_command_buffer.as_cmd<Command_GP0_ImageStore>();

  m_image_store_x = cmd->topleft.x;
  m_image_store_y = cmd->topleft.y;
  m_image_store_width = cmd->size.width;
  m_image_store_height = cmd->size.height;

  m_image_store_current_x = 0;
  m_image_store_current_y = 0;

  // Make sure everything from the renderer is synced over here to the gpu
  m_renderer->sync_gpu_to_renderer(m_vram);
  m_renderer->sync_renderer_to_gpu(m_vram);

  printf("gp0_image_store (VRAM -> CPU) (%u,%u,%u,%u) : ready.\n",
         cmd->topleft.x,
         cmd->topleft.y,
         cmd->size.width,
         cmd->size.height);
}

u32
GPU::gpuread()
{
  // If we're outside of a valid transfer, return 0.
  if (m_image_store_width == 0) {
    return 0;
  }

  const auto advance_vram = [&] {
    m_image_store_current_x++;
    if (m_image_store_current_x == (m_image_store_x + m_image_store_width)) {
      m_image_store_current_x = m_image_store_x;
      m_image_store_current_y++;
    }
  };

  u32 val = 0;
  for (u8 i = 0; i < 2; ++i) {
    const u32 vram_address =
      2 * (1024 * m_image_store_current_y + m_image_store_current_x);

    u16 halfword;
    memcpy(&halfword, &m_vram[vram_address], sizeof(halfword));
    advance_vram();

    val |= u32(halfword) << 16 * i;
  }

  printf("gpuread x=%u y=%u w=%u h=%u\n",
         m_image_store_current_x,
         m_image_store_current_y,
         m_image_store_width,
         m_image_store_height);

  // Did we just finish?
  if (m_image_store_current_y == m_image_store_height) {
    m_image_store_width = 0;
    m_image_store_height = 0;
    m_image_store_x = 0;
    m_image_store_y = 0;
    m_image_store_current_x = 0;
    m_image_store_current_y = 0;
  }

  return val;
}

void
GPU::gp1_set_display_vram_start(u32 word)
{
  ProfileZone;
  Command_GP1_SetVRAMStart cmd { .raw = word };
  m_state.diplay_vram_x_start = cmd.offset_x;
  m_state.diplay_vram_y_start = cmd.offset_y;
}

void
GPU::gp1_soft_reset(u32)
{
  ProfileZone;
  m_command_buffer.reset();

  memset(&m_gpustat, 0, sizeof(m_gpustat));
  memset(&m_state, 0, sizeof(m_state));
  m_gpustat.display_disabled = true;
  m_gpustat.vertical_interlace_en = true;
  m_state.display_horiz_start = 0x200;
  m_state.display_horiz_end = 0xc00;
  m_state.display_line_start = 0x10;
  m_state.display_line_end = 0x100;
  m_gpustat.display_area_color_depth = DisplayDepth_15Bits;
  m_gpustat.interrupt_request = false;
  m_gpustat.dma_direction = 0;

  // XXX : Invalidate GPU cache
}

void
GPU::gp1_display_mode(u32 word)
{
  ProfileZone;
  Command_GP1_DisplayMode cmd { .raw = word };
  m_gpustat.display_area_color_depth = cmd.display_area_color_depth;
  m_gpustat.horizontal_res_1 = cmd.horizontal_res_1;
  m_gpustat.horizontal_res_2 = cmd.horizontal_res_2;
  m_gpustat.reverse_flag = 0 /*cmd.reverse_flag*/;
  m_gpustat.vertical_interlace_en = cmd.vertical_interlace_en;
  m_gpustat.vertical_res = cmd.vertical_res;
  m_gpustat.video_mode = cmd.video_mode;
}

void
GPU::gp1_dma_direction(u32 word)
{
  ProfileZone;
  m_gpustat.dma_direction = word & 0b11;
}

void
GPU::gp1_set_display_horizontal_range(u32 word)
{
  ProfileZone;
  Command_GP1_SetDisplayHorizontalRange cmd { .raw = word };
  m_state.display_horiz_start = cmd.x_1;
  m_state.display_horiz_end = cmd.x_2;
}

void
GPU::gp1_set_display_vertical_range(u32 word)
{
  ProfileZone;
  Command_GP1_SetDisplayVerticalRange cmd { .raw = word };
  m_state.display_line_start = cmd.y_1;
  m_state.display_line_end = cmd.y_2;
}

void
GPU::gp1_display_enable(u32 word)
{
  ProfileZone;
  // on=0, off=1
  m_gpustat.display_disabled = word & 1;
}

void
GPU::gp1_acknowledge_interrupt(u32 word)
{
  ProfileZone;
  m_gpustat.interrupt_request = 0;
}

void
GPU::gp1_reset_command_buffer(u32 word)
{
  ProfileZone;
  // XXX : Clear FIFO once we implement that
  m_data_transfer_words = 0;
  m_command_buffer.reset();
  m_gp0_mode = GP0Mode::Command;
}

void
GPU::reset()
{
  m_gpustat.raw = 0;
  memset(m_vram, 0, sizeof(m_vram));
  memset(&m_state, 0, sizeof(m_state));
  m_command_buffer.reset();
}

GP0OpcodeData
gpu::decode_gp0_opcode(u32 word)
{
  using Word = gpu::GP0OpcodeData::Word;
  using Flags = gpu::GP0OpcodeData::Flags;

  const u32 opcode = word >> 24;
  const bool quad = opcode & 0x08;
  const bool textured = opcode & 0x04;

  // Render Polygon
  if (opcode >= 0x20 && opcode <= 0x3f) {
    const bool shaded = (opcode & 0x30) == 0x30;
    const bool opaque = (opcode & 2) == 0;
    const bool tex_blend = textured && (opcode & 1) == 0;

    u32 flags = Flags::RenderPolygon;
    if (shaded)
      flags |= Flags::Shaded;
    if (textured)
      flags |= Flags::Textured;
    if (opaque)
      flags |= Flags::Opaque;
    if (tex_blend)
      flags |= Flags::TextureBlend;

    if (!shaded && !textured) {
      GP0OpcodeData data(opcode,
                         flags,
                         {
                           Word::ColorCommand,
                           Word::Vertex,
                           Word::Vertex,
                           Word::Vertex,
                         });
      if (quad) {
        data.words.push_back(Word::Vertex);
      }
      return data;
    } else if (!shaded && textured) {
      GP0OpcodeData data(opcode,
                         flags,
                         {
                           Word::ColorCommand,
                           Word::Vertex,
                           Word::TexCoordPallete,
                           Word::Vertex,
                           Word::TexCoordPage,
                           Word::Vertex,
                           Word::TexCoord,
                         });
      if (quad) {
        data.words.push_back(Word::Vertex);
        data.words.push_back(Word::TexCoord);
      }
      return data;
    } else if (shaded && !textured) {
      GP0OpcodeData data(opcode,
                         flags,
                         {
                           Word::ColorCommand,
                           Word::Vertex,
                           Word::Color,
                           Word::Vertex,
                           Word::Color,
                           Word::Vertex,
                         });
      if (quad) {
        data.words.push_back(Word::Color);
        data.words.push_back(Word::Vertex);
      }
      return data;
    } else if (shaded && textured) {
      GP0OpcodeData data(opcode,
                         flags,
                         {
                           Word::ColorCommand,
                           Word::Vertex,
                           Word::TexCoordPallete,
                           Word::Color,
                           Word::Vertex,
                           Word::TexCoordPage,
                           Word::Color,
                           Word::Vertex,
                           Word::TexCoord,
                         });
      if (quad) {
        data.words.push_back(Word::Color);
        data.words.push_back(Word::Vertex);
        data.words.push_back(Word::TexCoord);
      }
      return data;
    } else {
      assert(false);
    }
  }

  // Line Rendering
  else if (opcode >= 0x40 && opcode <= 0x5f) {
    const bool polyline = opcode & 0x08;
    const bool shaded = opcode >= 0x50;

    u32 flags = Flags::RenderLine;
    if (polyline)
      flags |= Flags::PolyLine;
    if (shaded)
      flags |= Flags::Shaded;

    if (!shaded && !polyline) {
      return GP0OpcodeData(
        opcode, flags, { Word::ColorCommand, Word::Vertex, Word::Vertex });
    } else if (!shaded && polyline) {
      GP0OpcodeData data(
        opcode, flags, { Word::ColorCommand, Word::Vertex, Word::Vertex });
      data.uses_termination = true;
      data.words_per_extra_vertex = 1;
      return data;
    } else if (shaded && !polyline) {
      return GP0OpcodeData(
        opcode, flags, { Word::ColorCommand, Word::Vertex, Word::Color, Word::Vertex });
    } else if (shaded && polyline) {
      GP0OpcodeData data(
        opcode, flags, { Word::ColorCommand, Word::Vertex, Word::Color, Word::Vertex });
      data.uses_termination = true;
      data.words_per_extra_vertex = 2;
      return data;
    } else {
      assert(false);
    }
  }

  // Rectangle Rendering
  else if (opcode >= 0x60 && opcode <= 0x7f) {
    const bool variable_size = opcode >= 0x60 && opcode <= 0x67;
    const bool size_1 = opcode >= 0x68 && opcode <= 0x6f;
    const bool size_8 = opcode >= 0x70 && opcode <= 0x77;
    const bool size_16 = opcode >= 0x78 && opcode <= 0x7f;
    const bool opaque = (opcode & 2) == 0;
    const bool tex_blend = textured && (opcode & 1) == 0;

    u32 flags = Flags::RenderRectangle;

    if (variable_size)
      flags |= Flags::SizeVariable;
    if (size_1)
      flags |= Flags::Size1;
    if (size_8)
      flags |= Flags::Size8;
    if (size_16)
      flags |= Flags::Size16;

    if (textured)
      flags |= Flags::Textured;
    if (opaque)
      flags |= Flags::Opaque;
    if (tex_blend)
      flags |= Flags::TextureBlend;

    if (!textured && !variable_size) {
      return GP0OpcodeData(opcode, flags, { Word::ColorCommand, Word::Vertex });
    } else if (!textured && variable_size) {
      return GP0OpcodeData(
        opcode, flags, { Word::ColorCommand, Word::Vertex, Word::WidthHeight });
    } else if (textured && !variable_size) {
      return GP0OpcodeData(
        opcode, flags, { Word::ColorCommand, Word::Vertex, Word::TexCoordPallete });
    } else if (textured && variable_size) {
      return GP0OpcodeData(
        opcode,
        flags,
        { Word::ColorCommand, Word::Vertex, Word::TexCoordPallete, Word::WidthHeight });
    } else {
      assert(false);
    }
  }

  switch (opcode) {
    case Opcodes::GP0_Nop:
    case Opcodes::GP0_ClearCache:
    case Opcodes::GP0_DrawModeSetting:
    case Opcodes::GP0_SetDrawingAreaTopLeft:
    case Opcodes::GP0_SetDrawingAreaBottomRight:
    case Opcodes::GP0_SetDrawingOffset:
    case Opcodes::GP0_SetTextureWindow:
    case Opcodes::GP0_SetMaskBit:
      return GP0OpcodeData(opcode, 0, { Word::NotModeled });

    case Opcodes::GP0_CopyRectangleC2V:
    case Opcodes::GP0_FillRectangle:
    case Opcodes::GP0_CopyRectangleV2C:
      return GP0OpcodeData(
        opcode, 0, { Word::NotModeled, Word::NotModeled, Word::NotModeled });

    case Opcodes::GP0_CopyRectangleV2V:
      return GP0OpcodeData(
        opcode,
        0,
        { Word::NotModeled, Word::NotModeled, Word::NotModeled, Word::NotModeled });

    default:
      throw std::runtime_error(
        fmt::format("Unknown GPU cmd opcode word-length 0x{:02x}", opcode));
      break;
  }
}

void
GPU::dump_vram_ppm(const char *path)
{
  ProfileZone;
  FILE *f = fopen(path, "wb");
  fprintf(f, "P6\n%u %u\n255\n", 1024, 512);

  u8 *vram_curr = m_vram;
  for (u32 y = 0; y < 512; ++y) {
    for (u32 x = 0; x < 1024; ++x) {

      u16 word = *vram_curr | (vram_curr[1] << 8);

      u8 rgb[3];
      rgb[0] = ((word >> 0) & 0x1f) << 3; // 5b -> 8bit per channel
      rgb[1] = ((word >> 5) & 0x1f) << 3;
      rgb[2] = ((word >> 10) & 0x1f) << 3;

      fwrite(rgb, 1, 3, f);
      vram_curr += 2;
    }
  }

  fclose(f);
  printf("Dumped vram\n");
}

void
GPU::push_new_debug_data_frame(const GPUCommandBuffer &command)
{
  std::lock_guard lock(m_frame_debug_data_mutex);

  // Ensure we have a bucket for putting commands into for the current frame
  if (m_frame_debug_data.front().frame != m_vblank_count) {
    m_frame_debug_data.push_front(GPUFrameDebugData { .frame = m_vblank_count });

    // Get rid of any buckets which are too old
    while (m_frame_debug_data.size() > num_debug_frames) {
      m_frame_debug_data.pop_back();
    }
  }

  m_frame_debug_data.front().command_buffers.push_back(command);
}

u32
GPU::frame_data(GPUFrameDebugData *out, u32 num_frames)
{
  if (!out || m_frame_debug_data.empty())
    return 0;

  std::lock_guard lock(m_frame_debug_data_mutex);

  // todo: most-recent frame is possibly in-flight, should indicate in the payload
  u32 total = 0;
  for (auto it = m_frame_debug_data.begin();
       total < num_frames && it != m_frame_debug_data.end();
       ++it, ++total, ++out) {
    *out = *it;
  }
  return total;
}

const u8 *
GPU::vram_ptr()
{
  return m_vram;
}

const u8 *
GPU::display_vram_ptr()
{
  return m_display_vram;
}

void
GPU::get_display_vram_bounds(u32 *tl_x, u32 *tl_y, u32 *br_x, u32 *br_y)
{
  // XXX : This is not correct, but I haven't worked out the actual bounds based on the GP1
  // state information. For now, we'll just use the drawing area (clipping) bounds since these
  // are usually set to the region for the currently drawn framebuffer (and really it should be
  // the previous drawing area...)

  *tl_x = m_state.drawing_area_left;
  *tl_y = m_state.drawing_area_top;

  const u32 width = m_state.drawing_area_right - m_state.drawing_area_left;
  const u32 height = m_state.drawing_area_bottom - m_state.drawing_area_top;

  *br_x = *tl_x + width;
  *br_y = *tl_y + height;
}

namespace gpu {
const char *
gp0_opcode_name(u8 opcode)
{
  if (opcode >= 0x20 && opcode < 0x40)
    return "Render Polygon";
  if (opcode >= 0x40 && opcode < 0x60)
    return "Render Line";
  if (opcode >= 0x60 && opcode < 0x80)
    return "Render Rectangle";

  switch (opcode) {
    case gpu::Opcodes::GP0_Nop:
      return "nop";
    case gpu::Opcodes::GP0_ClearCache:
      return "Clear Cache";
    case gpu::Opcodes::GP0_FillRectangle:
      return "Fill Rectangle";
    case gpu::Opcodes::GP0_DrawModeSetting:
      return "Draw Mode";
    case gpu::Opcodes::GP0_SetDrawingAreaTopLeft:
      return "Drawing Area Top-Left";
    case gpu::Opcodes::GP0_SetDrawingAreaBottomRight:
      return "Drawing Area Bottom-Right";
    case gpu::Opcodes::GP0_SetDrawingOffset:
      return "Drawing Offset";
    case gpu::Opcodes::GP0_SetTextureWindow:
      return "Texture Window";
    case gpu::Opcodes::GP0_SetMaskBit:
      return "Mask Bit Control";
    case gpu::Opcodes::GP0_CopyRectangleV2V:
      return "Copy Rectangle (VRAM-to-VRAM)";
    case gpu::Opcodes::GP0_CopyRectangleC2V:
      return "Copy Rectangle (CPU-to-VRAM)";
    case gpu::Opcodes::GP0_CopyRectangleV2C:
      return "Image Rectangle (VRAM-to-CPU)";
    default:
      return "no-name";
  }
}
}

}
