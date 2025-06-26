#pragma once

#include "shared/types.h"

namespace gpu {

enum ta_para_type
{
  /* Control Parameters */
  EndOfList     = 0,
  UserTileClip  = 1,
  ObjectListSet = 2,
  // 3 is reserved

  /* Global Parameters */
  Polygon = 4,
  Sprite  = 5,

  /* Vertex Parameters */
  Vertex = 7
};

enum ta_list_type : u32
{
  Opaque         = 0,
  OpaqueModifier = 1,
  Translucent    = 2,
  TransModifier  = 3,
  PunchThrough   = 4,
  Undefined      = 5,
};

enum ta_strip_length
{
  One  = 0,
  Two  = 1,
  Four = 2,
  Six  = 3
};

enum ta_user_clip
{
  Disable = 0,
  Inside  = 2,
  Outside = 3
};

enum ta_col_type
{
  Packed       = 0,
  Floating     = 1,
  IntensityOne = 2,
  IntensityTwo = 3
};

struct ta_param_word {
  union {
    u32 raw;

    struct {
      u32 uv16 : 1;     // 16 bit UV
      u32 gouraud : 1;  // Gouraud Shading
      u32 offset : 1;   // Offset (or Bump) Mapping
      u32 texture : 1;  // Texture Enable
      u32 col_type : 2; // Color Type
      u32 volume : 1;   // "Two Volumes" format
      u32 shadow : 1;   // Shadow Processing
      u32 _rsvd0 : 8;
      u32 user_clip : 2; // Tile Clipping mode
      u32 strip_len : 2; // Length of Strip for Partitioning
      u32 _rsvd1 : 3;
      u32 group_en : 1;           // Group Control Enable
      ta_list_type list_type : 3; // List Type
      u32 _rsvd2 : 1;
      u32 strip_end : 1; // End of Strip
      u32 type : 3;      // Para Type
    };
  };
};

enum tex_pixel_fmt
{
  ARGB1555 = 0,
  RGB565   = 1,
  ARGB4444 = 2,
  YUV422   = 3,
  BumpMap  = 4,
  Palette4 = 5,
  Palette8 = 6,
  Reserved = 7
};

struct ta_tex_word {
  union {
    u32 raw;

    struct {
      u32 address : 21; // Address of data in texture memory
      u32 _rsvd0 : 4;
      u32 stride : 1;    // Use TEXT_CONTROL for U-size
      u32 scanline : 1;  // Texture uses scanline, not twiddle, format
      u32 pixel_fmt : 3; // Pixel format
      u32 vq : 1;        // Texture uses VQ compression
      u32 mip : 1;       // Texture is MIP-mapped
    };

    struct {
      u32 _pad1 : 21;
      u32 palette : 6; // Palette selector (depends on pixel_fmt)
      u32 _pad0 : 5;
    };
  };
};

struct ta_isp_word {
  union {
    u32 raw;

    struct {
      u32 depth_compare_mode : 3;
      u32 culling_mode : 2;
      u32 z_write_disabled : 1;
      u32 texture : 1;
      u32 offset : 1;
      u32 gouraud : 1;
      u32 uv16 : 1;
      u32 cache_bypass : 1;
      u32 dcalc_ctrl : 1;
      u32 _rsvd0 : 20;
    } opaque_or_translucent;

    struct {
      u32 volume_instruction : 3;
      u32 culling_mode : 2;
      u32 _rsvd0 : 27;
    } modifier_volume;
  };
};
static_assert(sizeof(ta_isp_word) == sizeof(u32));

struct ta_tsp_word {
  union {
    u32 raw;

    struct {
      u32 size_v : 3;
      u32 size_u : 3;
      u32 instruction : 2;
      u32 mipmap_adjust : 4;
      u32 texture_ss : 1;
      u32 filter_mode : 2;
      u32 clamp_uv : 2;
      u32 flip_uv : 2;
      u32 no_tex_alpha : 1;
      u32 use_alpha : 1;
      u32 color_clamp : 1;
      u32 fog_mode : 2;
      u32 dst_select : 1;
      u32 src_select : 1;
      u32 dst_alpha : 3;
      u32 src_alpha : 3;
    };
  };
};

void ta_debug_param_word(uint32_t);
void ta_debug_tex_word(uint32_t);

}
