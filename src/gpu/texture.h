#pragma once

#include <memory>
#include "gpu/ta_commands.h"

class Console;

namespace gpu {

class TextureManager;

struct TextureKey {
  ta_tex_word tex_word;
  ta_tsp_word tsp_word;

  operator u64() const
  {
    return (u64(tex_word.raw) << 32) | u64(tsp_word.raw);
  }
};

struct Texture {
  bool is_host_allocated = false;
  bool is_dirty          = false;

  u32 last_updated_on_frame;
  u32 last_used_on_frame;

  /** Unique key describing where the texture is in VRAM, as well as it's format. This
   * can be used to key a texture in a map. */
  TextureKey key;

  u32 host_texture_id;

  /* Offset into the 64-bit area address of VRAM (i.e. not global address) */
  u32 dc_vram_address;
  u32 dc_bytes;
  u16 width;
  u16 height;
  u16 stride;
  std::unique_ptr<u32[]> data;
  u64 hash;
  ta_tsp_word tsp_word;
  ta_tex_word tex_word;
  u32 uuid;
};

namespace texture_logic {

void convert_argb1555(const u16 *input, u32 *output);
void convert_rgb565(const u16 *input, u32 *output);
void convert_argb4444(const u16 *input, u32 *output);

/**! Calculate offset into texture data to last mipmap level for a given texture size,
 * assuming VQ-encoded color. */
u32 vq_mipmap_offset(u32 size);

/**! Calculate offset into texture data to last mipmap level for a given texture size,
 * assuming non-VQ-encoded color. */
u32 nonvq_mipmap_offset(u32 tex_width);

/** Calculate content of this texture based on latest palette/texture VRAM data. */
void calculate_texture_data(Console *, std::shared_ptr<Texture> &);

u32 calculate_texture_bytes(ta_tex_word texture_format, u32 width, u32 height);

};

};
