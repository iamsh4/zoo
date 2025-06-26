#pragma once

#include "shared/types.h"

namespace zoo::ps1::gpu {

enum class GPUOperation
{
  // GP0 Control Operations
  GP0_Nop,
  GP0_ClearCache,
  GP0_FillRectangle,
  GP0_DrawModeSetting,
  GP0_SetDrawingAreaTopLeft,
  GP0_SetDrawingAreaBottomRight,
  GP0_SetDrawingOffset,
  GP0_SetTextureWindow,
  GP0_SetMaskBit,

  // GP0 Data Transfer Operations
  GP0_CopyVramToVram,
  GP0_CopyCpuToVram,
  GP0_CopyVramToCpu,

  // GP0 Geometry Operations
  GP0_TexturedPolygon,
  GP0_MonochromePolygon,
  GP0_ShadedPolygon,
  GP0_MonochromeRectangle,
  GP0_TexturedRectangle,

  // GP1 Control Operations
  GP1_SoftReset,
  GP1_ResetCommandBuffer,
  GP1_AcknowledgeInterrupt,
  GP1_DisplayEnable,
  GP1_DMADirection,
  GP1_SetDispayVRAMStart,
  GP1_SetDisplayHorizontalRange,
  GP1_SetDisplayVerticalRange,
  GP1_DisplayMode,
};

struct GPUOpcode {
  u8 opcode;
  GPUOperation operation;
  u8 num_words;
};

enum Opcodes : u8
{
  // GP0 (Draw state, Memory Transfer, Draw Commands)

  // TODO : this is a huge list which has to be kept in sync in three places total. Can be
  // simplified since many of these commands are similar and appear in ranges.

  GP0_Nop = 0x00,
  GP0_ClearCache = 0x01,
  GP0_FillRectangle = 0x02,
  GP0_DrawModeSetting = 0xe1,
  GP0_SetDrawingAreaTopLeft = 0xe3,
  GP0_SetDrawingAreaBottomRight = 0xe4,
  GP0_SetDrawingOffset = 0xe5,
  GP0_SetTextureWindow = 0xe2,
  GP0_SetMaskBit = 0xe6,
  GP0_CopyRectangleV2V = 0x80, /*!< VRAM -> VRAM */
  GP0_CopyRectangleC2V = 0xa0, /*!< CPU -> VRAM */
  GP0_CopyRectangleV2C = 0xc0, /*!< VRAM -> CPU */

  GP0_TexturedPolygon3_OpaqueTextureBlending = 0x24,
  GP0_TexturedPolygon3_OpaqueTexture = 0x25,
  GP0_TexturedPolygon3_SemiTransparentTextureBlending = 0x26,
  GP0_TexturedPolygon3_SemiTransparentTexture = 0x27,
  GP0_TexturedPolygon4_OpaqueTextureBlending = 0x2c,
  GP0_TexturedPolygon4_OpaqueTexture = 0x2d,
  GP0_TexturedPolygon4_SemiTransparentTextureBlending = 0x2e,
  GP0_TexturedPolygon4_SemiTransparentTexture = 0x2f,

  GP0_MonochromePolygon3_Opaque = 0x20,
  GP0_MonochromePolygon3_SemiTransparent = 0x22,
  GP0_MonochromePolygon4_Opaque = 0x28,
  GP0_MonochromePolygon4_SemiTransparent = 0x2a,

  GP0_ShadedPolygon3_Opaque = 0x30,
  GP0_ShadedPolygon3_SemiTransparent = 0x32,
  GP0_ShadedPolygon4_Opaque = 0x38,
  GP0_ShadedPolygon4_SemiTransparent = 0x3a,

  GP0_MonochromeRectangle_VariableSizeOpaque = 0x60,
  GP0_MonochromeRectangle_VariableSizeTranslucent = 0x62,
  GP0_MonochromeRectangle_DotOpaque = 0x68,

  GP0_TexturedRectangle_VariableSizeOpaqueTextureBlending = 0x64,
  GP0_TexturedRectangle_VariableSizeOpaqueRawTexture = 0x65,
  GP0_TexturedRectangle_VariableSizeSemiTransparentRawTexture = 0x66,
  GP0_TexturedRectangle_16x16OpaqueTextureBlending = 0x7c,

  GP0_ShadedTexturedPolygon_FourPointOpaqueTexBlend = 0x3c,
  GP0_ShadedTexturedPolygon_FourPointSemiTransparentTexBlend = 0x3e,

  GP0_MonochromeLineOpaque = 0x40,

  GP0_ShadedLineOpaque = 0x50,

  // GP1 (Display configuration)

  GP1_SoftReset = 0x00,
  GP1_ResetCommandBuffer = 0x01,
  GP1_AcknowledgeInterrupt = 0x02,
  GP1_DisplayEnable = 0x03,
  GP1_DMADirection = 0x04,
  GP1_SetDispayVRAMStart = 0x05,
  GP1_SetDisplayHorizontalRange = 0x06,
  GP1_SetDisplayVerticalRange = 0x07,
  GP1_DisplayMode = 0x08,
};

struct Command_GP0_DrawModeSetting {
  u32 texture_page_x_base : 4;
  u32 texture_page_y_base : 1;
  u32 semi_transparent : 2;
  u32 texture_page_colors : 2;
  u32 dither_en : 1;
  u32 drawing_allowed : 1;
  u32 texture_disable : 1;
  u32 texture_rect_x_flip : 1;
  u32 texture_rect_y_flip : 1;
};

struct Command_GP0_DrawingArea {
  u32 x_coord : 10;
  u32 y_coord : 9;
};

struct Command_GP0_DrawingOffset {
  u32 x_offset : 11;
  u32 y_offset : 11;
};

union Command_GP0_TextureWindowSetting {
  struct {
    u32 window_mask_x : 5;
    u32 window_mask_y : 5;
    u32 window_offset_x : 5;
    u32 window_offset_y : 5;
  };
  u32 raw;
};

union Command_GP0_MaskBitSetting {
  struct {
    u32 set_mask : 1;
    u32 check_mask : 1;
  };
  u32 raw;
};

union VertexXY {
  struct {
    i16 x : 11;
    i16 _pad0 : 5;
    i16 y : 11;
    i16 _pad1 : 5;
  };
  u32 raw;
};

union Color {
  struct {
    u32 r : 8;
    u32 g : 8;
    u32 b : 8;
    u32 upper : 8;
  };
  u32 raw;
};

// Texture coordinate and palette
union TexCoordPalette {
  struct {
    u32 x : 8;
    u32 y : 8;
    u32 clut : 16;
  };
  u32 raw;
};

union TexCoordTexPage {
  struct {
    u32 x : 8;
    u32 y : 8;
    u32 texpage : 16;
  };
  u32 raw;
};

////////////////////////////////////////////////

// 0x20-0x2a
struct Command_GP0_MonochromePolygon {
  Color color;
  VertexXY vertex1;
  VertexXY vertex2;
  VertexXY vertex3;
  VertexXY vertex4;
};

// 0x24-0x2f
struct Command_GP0_TexturedPolygon {
  Color color;
  VertexXY vertex1;
  TexCoordPalette texpal1;
  VertexXY vertex2;
  TexCoordTexPage texpage2;
  VertexXY vertex3;
  TexCoordTexPage tex3; // Page unused
  VertexXY vertex4;
  TexCoordTexPage tex4; // Page unused
};

// 0x30-0x3a
struct Command_GP0_ShadedPolygon {
  Color color1;
  VertexXY vertex1;
  Color color2;
  VertexXY vertex2;
  Color color3;
  VertexXY vertex3;
  Color color4;
  VertexXY vertex4;
};

// 0x34-0x3e
struct Command_GP0_ShadedTexturedPolygon {
  Color color;
  VertexXY vertex1;
  TexCoordPalette texpal1;
  Color color2;
  VertexXY vertex2;
  TexCoordTexPage texpage2;
  Color color3;
  VertexXY vertex3;
  TexCoordTexPage tex3; // Page unused
  Color color4;
  VertexXY vertex4;
  TexCoordTexPage tex4; // Page unused
};

// 0x40-0x4a
struct Command_GP0_MonochromeLine {
  Color color1;
  VertexXY vertex1;
  VertexXY vertex2;
};

// 0x50-0x5a
struct Command_GP0_ShadedLine {
  Color color1;
  VertexXY vertex1;
  Color color2;
  VertexXY vertex2;
};

// 0x60-0x7a
struct Command_GP0_MonochromeRectangle {
  Color color;
  VertexXY vertex;
  struct {
    u16 width;
    u16 height;
  };
};

// 0x64-0x7f
struct Command_GP0_TexturedRectangle {
  Color color;
  VertexXY vertex;
  TexCoordPalette texpal;
  struct {
    u16 width;
    u16 height;
  };
};

/////////////////////////////////////////////

struct Command_GP0_CopyRectangle {
  u32 command;
  struct {
    u16 x;
    u16 y;
  } topleft;
  struct {
    u16 width;
    u16 height;
  } size;
  // (Data follows, usually throw DMA)
};

struct Command_GP0_CopyRectangleV2V {
  u32 command;
  struct {
    u16 x;
    u16 y;
  } source;
  struct {
    u16 x;
    u16 y;
  } dest;
  struct {
    u16 width;
    u16 height;
  } size;
};

struct Command_GP0_FillRectangle {
  Color color;
  struct {
    u16 x;
    u16 y;
  } topleft;
  struct {
    u16 width;
    u16 height;
  } size;
};

struct Command_GP0_ImageStore {
  u32 command;
  struct {
    u32 x : 16;
    u32 y : 16;
  } topleft;
  struct {
    u16 width;
    u16 height;
  } size;
};

union Command_GP1_DisplayMode {
  struct {
    u32 horizontal_res_1 : 2;
    u32 vertical_res : 1;
    u32 video_mode : 1;
    u32 display_area_color_depth : 1;
    u32 vertical_interlace_en : 1;
    u32 horizontal_res_2 : 1;
    u32 reverse_flag : 1;
  };
  u32 raw;
};

union Command_GP1_SetVRAMStart {
  struct {
    // 0-9   X (0-1023)    (halfword address in VRAM)  (relative to begin of VRAM)
    u32 offset_x : 10;
    // 10-18 Y (0-511)     (scanline number in VRAM)   (relative to begin of VRAM)
    u32 offset_y : 9;
  };
  u32 raw;
};

union Command_GP1_SetDisplayHorizontalRange {
  struct {
    // 0-11   X1 (260h+0)       ;12bit       ;\counted in 53.222400MHz units,
    u32 x_1 : 12;
    // 12-23  X2 (260h+320*8)   ;12bit       ;/relative to HSYNC
    u32 x_2 : 12;
  };
  u32 raw;
};

union Command_GP1_SetDisplayVerticalRange {
  struct {
    // 0-9   Y1 (NTSC=88h-(224/2), (PAL=A3h-(264/2))  ;\scanline numbers on screen,
    u32 y_1 : 10;
    // 10-19 Y2 (NTSC=88h+(224/2), (PAL=A3h+(264/2))  ;/relative to VSYNC
    u32 y_2 : 10;
  };
  u32 raw;
};

struct GP0OpcodeData {
  i32 words_per_extra_vertex = 0;
  u8 opcode = 0;
  bool uses_termination = false;

  enum Flags
  {
    //
    RenderPolygon = 1 << 0,
    RenderLine = 1 << 1,
    RenderRectangle = 1 << 2,
    //

    //
    Textured = 1 << 20,
    Shaded = 1 << 21, // Otherwise "monochrome"
    PolyLine = 1 << 22,
    Opaque = 1 << 23, // Otherwise "semi-transparent"
    SizeVariable = 1 << 25,
    Size1 = 1 << 26,
    Size8 = 1 << 27,
    Size16 = 1 << 28,
    TextureBlend = 1 << 29, // Otherwise "texture-raw"
  };
  u32 flags = 0;

  enum Word
  {
    ColorCommand,
    Vertex,
    TexCoordPallete,
    TexCoordPage,
    TexCoord,
    Color,
    WidthHeight,
    NotModeled,
  };
  std::vector<Word> words;

  GP0OpcodeData() {}
  GP0OpcodeData(u8 opcode, u32 flags, std::initializer_list<Word> words)
    : opcode(opcode),
      flags(flags),
      words(words)
  {
  }

  // static GP0OpcodeData standard(u8 opcode, i32 num_words)
  // {
  //   return GP0OpcodeData {
  //     .num_words = num_words,
  //     .words_per_extra_vertex = 0,
  //     .opcode = opcode,
  //     .uses_termination = false,
  //   };
  // }
  // static GP0OpcodeData polyline(u8 opcode, i32 num_words, i32 words_per_extra_vertex)
  // {
  //   return GP0OpcodeData {
  //     .num_words = num_words,
  //     .words_per_extra_vertex = words_per_extra_vertex,
  //     .opcode = opcode,
  //     .flags { .uses_termination = false },
  //   };
  // }
};

GP0OpcodeData decode_gp0_opcode(u32 opcode);

const char *gp0_opcode_name(u8 opcode);

}
