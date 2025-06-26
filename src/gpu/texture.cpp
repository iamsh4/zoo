#include <algorithm>

#include "core/console.h"
#include "gpu/texture.h"
#include "shared/error.h"
#include "shared/profiling.h"

#if defined(ZOO_ARCH_ARM)
#include <arm_neon.h>
#endif

namespace gpu::texture_logic {

class VRAMReader {
private:
  u32 kVRAM64BaseAddress = 0x0400'0000;
  Console *_console;
  const u8 *const m_vram32_base = nullptr;

public:
  VRAMReader(Console *console)
    : _console(console),
      m_vram32_base(_console->memory()->root() + 0x0500'0000)
  {
  }

  u8 read_u8(u32 vram_offset)
  {
    const u32 val32 = read_u32(vram_offset);
    // val32 = [DCBA] mem = [ABCD]
    return (val32 >> ((vram_offset & 3) * 8)) & 0xFF;
  }

  u16 read_u16(u32 vram_offset)
  {
    const u32 val32 = read_u32(vram_offset);
    // val32 = [DCBA] mem = [ABCD] val16 = [DC] or [BA]
    return (val32 >> ((vram_offset & 2) * 8)) & 0xFFFF;
  }

  u32 read_u32(u32 vram_offset)
  {
    const u32 vram32_offset = VRAMAddress64(vram_offset & 0x7F'FFFC).to32().get();

    u32 result;
    memcpy(&result, &m_vram32_base[vram32_offset], 4);
    return result;
  }
};

static uint16_t morton_table[256]; // Precomputed 8-bit Morton expansion

// Precompute the lookup table at startup
__attribute__((constructor)) void
init_morton_table()
{
  for (uint16_t i = 0; i < 256; ++i) {
    uint16_t x = 0, y = 0;
    for (uint16_t bit = 0; bit < 8; ++bit) {
      x |= ((i >> (2 * bit + 1)) & 1) << bit;
      y |= ((i >> (2 * bit)) & 1) << bit;
    }
    morton_table[i] = (y << 8) | x;
  }
}

// Deinterleaves even and odd bits from a 32-bit Morton code
void
deinterlace_bits(uint32_t input, uint16_t *even, uint16_t *odd)
{
  uint32_t y = input & 0x55555555;        // Mask even bits
  uint32_t x = (input >> 1) & 0x55555555; // Mask odd bits

  // Compact the bits using parallel bit extraction
  x = (x | (x >> 1)) & 0x33333333;
  x = (x | (x >> 2)) & 0x0F0F0F0F;
  x = (x | (x >> 4)) & 0x00FF00FF;
  x = (x | (x >> 8)) & 0x0000FFFF;

  y = (y | (y >> 1)) & 0x33333333;
  y = (y | (y >> 2)) & 0x0F0F0F0F;
  y = (y | (y >> 4)) & 0x00FF00FF;
  y = (y | (y >> 8)) & 0x0000FFFF;

  *even = (uint16_t)y;
  *odd  = (uint16_t)x;
}

// Detwiddle index function.
void
index_to_xy(unsigned width, unsigned height, unsigned index, unsigned *x, unsigned *y)
{
  unsigned offs_width = 0u, offs_height = 0u;

  if (width > height) {
    const unsigned per_block = height * height;
    width                    = height;
    offs_width               = (index / per_block) * height;
    index                    = index & (per_block - 1u);
  } else if (height > width) {
    const unsigned per_block = width * width;
    height                   = width;
    offs_height              = (index / per_block) * width;
    index                    = index & (per_block - 1u);
  }

  u16 x_offset = 0;
  u16 y_offset = 0;
#if 0
  for (unsigned i = 0; i < 10; ++i) {
    unsigned at_level = (index >> (i * 2)) & 3;
    x_offset += (at_level & 2u) ? (1u << i) : 0u;
    y_offset += (at_level & 1u) ? (1u << i) : 0u;
  }
#else
  // Same as above, but parallel bit extraction
  deinterlace_bits(index, &y_offset, &x_offset);
#endif

  *x = offs_width + x_offset;
  *y = offs_height + y_offset;
}

void
convert_argb1555(const u16 *input, u32 *output)
{
  const u16 word = *input;
  u32 a          = (word & 0x8000) ? 0xFF : 0x00;
  u32 r          = ((word >> 10) & 0x1F);
  u32 g          = ((word >> 5) & 0x1F);
  u32 b          = ((word >> 0) & 0x1F);

  r = (r << 3) | (r >> 2);
  g = (g << 3) | (g >> 2);
  b = (b << 3) | (b >> 2);

  const u32 rgba = (a << 24) | (b << 16) | (g << 8) | r;
  *output        = rgba;
}

void
convert_rgb565(const u16 *input, u32 *output)
{
  const u16 word = *input;
  const u32 a    = 0xff;
  u32 r          = ((word >> 11) & 0x1F);
  u32 g          = ((word >> 5) & 0x3F);
  u32 b          = ((word >> 0) & 0x1F);

  r = (r << 3) | (r >> 2);
  g = (g << 2) | (g >> 4);
  b = (b << 3) | (b >> 2);

  const u32 rgba = (a << 24) | (b << 16) | (g << 8) | r;
  *output        = rgba;
}

void
convert_argb4444(const u16 *input, u32 *output)
{
  const u16 word = *input;
  const u32 a_hi = ((word >> 12) & 0xF), a = (a_hi << 4) | a_hi;
  const u32 r_hi = ((word >> 8) & 0xF), r = (r_hi << 4) | r_hi;
  const u32 g_hi = ((word >> 4) & 0xF), g = (g_hi << 4) | g_hi;
  const u32 b_hi = ((word >> 0) & 0xF), b = (b_hi << 4) | b_hi;
  const u32 rgba = (a << 24) | (b << 16) | (g << 8) | r;
  *output        = rgba;
}

void
convert_yuv422(const u16 *input, u32 *output)
{
  // Described in section 3.6.1.2
  const u16 Y0U = input[0];
  const int Y0  = Y0U >> 8;
  const int U   = Y0U & 0xFF;

  const u16 Y1V = input[1];
  const int Y1  = Y1V >> 8;
  const int V   = Y1V & 0xFF;

  auto R = [&](u16 y) {
    return y + (V - 128) * 11.f / 8.f;
  };
  auto G = [&](u16 y) {
    return y - (U - 128) * 11.f / 8.f * 0.25f - (V - 128) * 11.f / 8.f * 0.5f;
  };
  auto B = [&](u16 y) {
    return y + (U - 128) * 11.f / 8.f * 1.25f;
  };

  auto as_u8 = [](float val) {
    return (u8)std::min(std::max(val, 0.f), 255.f);
  };

  output[0] =
    0xFF000000 | (as_u8(B(Y0)) << 16) | (as_u8(G(Y0)) << 8) | (as_u8(R(Y0)) << 0);
  output[1] =
    0xFF000000 | (as_u8(B(Y1)) << 16) | (as_u8(G(Y1)) << 8) | (as_u8(R(Y1)) << 0);
}

u32
vq_mipmap_offset(u32 size)
{
  switch (size) {
    case 1:
      return 0x0;
    case 2:
      return 0x1;
    case 4:
      return 0x2;
    case 8:
      return 0x6;
    case 16:
      return 0x16;
    case 32:
      return 0x56;
    case 64:
      return 0x156;
    case 128:
      return 0x556;
    case 256:
      return 0x1556;
    case 512:
      return 0x5556;
    case 1024:
      return 0x15556;
    default:
      return 0;
  }
}

u32
nonvq_mipmap_offset(u32 tex_width)
{
  switch (tex_width) {
    // This is 2bpp * (sum of the 1*1 + 2*2 + 4*4 + ...)
    // Up until the size of this texture. According to the docs,
    // This all starts at +6, but I don't know why, so just duplicating
    // the table on page 148.
    case 1:
      return 0x00006;
    case 2:
      return 0x00008;
    case 4:
      return 0x00010;
    case 8:
      return 0x00030;
    case 16:
      return 0x000B0;
    case 32:
      return 0x002B0;
    case 64:
      return 0x00AB0;
    case 128:
      return 0x02AB0;
    case 256:
      return 0x0AAB0;
    case 512:
      return 0x2AAB0;
    case 1024:
      return 0xAAAB0;
    default:
      _check(false, "Invalid texture width");
  }
}

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

void (*pixel_format_converters[8])(const u16 *, u32 *) = {
  convert_argb1555, convert_rgb565,   convert_argb4444,
  convert_yuv422,   convert_argb4444, /* TODO : Implement other formats. */
  convert_argb4444, convert_argb4444, convert_argb4444,
};

void
calculate_texture_data(Console *console, std::shared_ptr<Texture> &tex)
{
  ProfileZoneNamed("calc_texture_data");

  const ta_tex_word texture_format = tex->tex_word;
  const bool detwiddle             = !texture_format.scanline;

  const u32 width  = tex->width;
  const u32 height = tex->height;

  VRAMReader reader(console);

  u32 dc_ptr = tex->dc_vram_address;
  u32 x, y;

  auto pixel_converter = pixel_format_converters[texture_format.pixel_fmt];

  // Fetch and decode from VRAM
  switch (texture_format.pixel_fmt) {
    case tex_pixel_fmt::ARGB1555:
    case tex_pixel_fmt::RGB565:
    case tex_pixel_fmt::ARGB4444:
    case tex_pixel_fmt::YUV422:

      if (texture_format.vq) {
        ProfileZoneNamed("CreateTexture_VQ");

        // VQ-Compressed
        u16 code_book[256 * 4];
        for (int i = 0; i < 256 * 4; ++i) {
          code_book[i] = reader.read_u16(dc_ptr);
          dc_ptr += sizeof(u16);
        }

        // Hack: We don't actually parse the mipmaps that were provided, but rather just
        // take the provided highest level and let the display driver make the mipmaps
        // itself.
        if (texture_format.mip) {
          dc_ptr += vq_mipmap_offset(width);
        }

        const u32 index_width  = width / 2;
        const u32 index_height = height / 2;
        for (u32 index = 0; index < (index_width * index_height); ++index) {
          if (detwiddle) {
            index_to_xy(index_width, index_height, index, &x, &y);
          } else {
            x = index % index_width;
            y = index / index_height;
          }

          x *= 2;
          y *= 2;

          const u8 codebook_index = reader.read_u8(dc_ptr);
          dc_ptr += sizeof(u8);

          // Decode pixels in the output texture
          (*pixel_converter)(&code_book[4 * codebook_index + 3],
                             &tex->data.get()[(y + 1) * width + x + 1]);
          (*pixel_converter)(&code_book[4 * codebook_index + 2],
                             &tex->data.get()[y * width + x + 1]);
          (*pixel_converter)(&code_book[4 * codebook_index + 1],
                             &tex->data.get()[(y + 1) * width + x]);
          (*pixel_converter)(&code_book[4 * codebook_index + 0],
                             &tex->data.get()[y * width + x]);
        }

      } else {
        ProfileZoneNamed("CreateTexture_NonVQ");

        // Not VQ-Compressed

        // Textures normally start at the indicated position, but we need to advance
        // forward to the largest texture if this is mip-mapped.
        if (texture_format.mip) {
          dc_ptr += nonvq_mipmap_offset(width);
        }

        // In stride textures, stride defines the number of texels per row
        // u32 line_width = stride >= 32 ? stride : width;

        const u32 texture_base = dc_ptr;

        for (u32 index = 0; index < (width * height);) {
          if (detwiddle) {
            index_to_xy(width, height, index, &x, &y);
          } else {
            x = index % width;
            y = index / width;
          }

          if (texture_format.pixel_fmt != tex_pixel_fmt::YUV422) {
            const u16 word = reader.read_u16(dc_ptr);
            (*pixel_converter)(&word, &tex->data.get()[y * width + x]);
            dc_ptr += sizeof(u16);
            index++;
          } else {
            u32 yuv_ptr = dc_ptr;

            if (tex->stride >= 32) {
              yuv_ptr = texture_base + (y * tex->stride + x) * sizeof(u16);
            }

            if (detwiddle) {
              // At the bottom of the detwiddle pattern are blocks of four pixels arranged
              // in the following 2D spatial pattern, which means we need to modify how we
              // mux data to the converters in this case. 0 2 1 3 Importantly, YUV data is
              // decoded from spatial locations (0 and 2), and the output pixels are
              // written to spatial locations (0 and 2).

              u16 words[2];
              u32 outputs[2];

              yuv_ptr = texture_base + index * sizeof(u16);

              words[0] = reader.read_u16(yuv_ptr);
              words[1] = reader.read_u16(yuv_ptr + 2 * sizeof(u16));
              (*pixel_converter)(&words[0], outputs);
              tex->data.get()[y * width + x]     = outputs[0];
              tex->data.get()[y * width + x + 1] = outputs[1];

              if (index % 2 == 0)
                index += 1;
              else
                index += 3;
            } else {
              u16 words[2];
              words[0] = reader.read_u16(yuv_ptr);
              words[1] = reader.read_u16(yuv_ptr + 2);

              u32 outputs[2];
              (*pixel_converter)(&words[0], outputs);

              tex->data.get()[y * width + x]     = outputs[0];
              tex->data.get()[y * width + x + 1] = outputs[1];

              dc_ptr += 2 * sizeof(u16);
              index += 2;
            }
          }
        }
      }
      break;

      /* Use Reserved for framebuffer, which is 32-bit ARGB */
    case tex_pixel_fmt::Reserved: {
      ProfileZoneNamed("CreateTexture_Framebuffer");

      for (u32 index = 0; index < (width * height); ++index) {
        const u32 dword = console->memory()->read<u32>(dc_ptr);
        const u32 a     = 0x00u;
        const u32 r     = (dword >> 16) & 0xFFu;
        const u32 g     = (dword >> 8) & 0xFFu;
        const u32 b     = (dword >> 0) & 0xFFu;
        const u32 rgba  = (a << 24) | (b << 16) | (g << 8) | r;

        tex->data.get()[index] = rgba;
        dc_ptr += sizeof(u32);
      }
    } break;

    case tex_pixel_fmt::Palette4: {
      ProfileZoneNamed("CreateTexture_Pal4");
      const u32 palette_selector     = (texture_format.raw >> 21) & 0b111111u;
      const u32 palette_base_address = palette_selector << 4;

      // Note: Paletted textures store palette index for lookup in fragment shader
      for (u32 index = 0; index < (width * height); index += 2) {

        const u32 pal_index = reader.read_u8(dc_ptr);
        const u32 pal_lo    = pal_index & 0xF;
        const u32 pal_hi    = (pal_index >> 4) & 0xF;

        index_to_xy(width, height, index, &x, &y);
        tex->data.get()[y * width + x] = palette_base_address | pal_lo;

        index_to_xy(width, height, index + 1, &x, &y);
        tex->data.get()[y * width + x] = palette_base_address | pal_hi;

        dc_ptr += sizeof(u8);
      }
    } break;

    case tex_pixel_fmt::Palette8: {
      ProfileZoneNamed("CreateTexture_Pal8");

      const u32 palette_selector     = (texture_format.raw >> 25) & 0b11u;
      const u32 palette_base_address = palette_selector << 8;

      const u32 pal_ram_ctrl       = console->gpu()->get_pal_ram_ctrl();
      const u32 *const palette_ram = console->gpu()->get_palette_ram();

      void (*converter)(const u16 *input, u32 *output) = nullptr;
      switch (pal_ram_ctrl & 3) {
        case 0:
          converter = convert_argb1555;
          break;
        case 1:
          converter = convert_rgb565;
          break;
        case 2:
          converter = convert_argb4444;
          break;
        case 3:
          break;
      }

      if (converter == nullptr) {
        printf("Unsupported texture format ARGB8888 for PAL4\n");
        break;
      }

      for (u32 index = 0; index < (width * height); ++index) {
        index_to_xy(width, height, index, &x, &y);

        u32 pal_index = reader.read_u8(dc_ptr);
        pal_index     = palette_base_address | pal_index;

        if (0) {
          const u16 pal_data_u16 = palette_ram[pal_index] & 0xFFFF;
          u32 rgba;
          converter(&pal_data_u16, &rgba);
          tex->data.get()[y * width + x] = rgba;
        } else {
          tex->data.get()[y * width + x] = pal_index;
        }

        dc_ptr += sizeof(u8);
      }
    } break;

    default:
      printf("Unhandled texture format (%d) encountered\n", texture_format.pixel_fmt);
      break;
  }

  /* TODO Texture hashes could be used to support texture packs. This is
   *      currently disabled. */
  tex->hash = 0xDEADBEEF;
}

u32
calculate_texture_bytes(ta_tex_word texture_format, u32 width, u32 height)
{
  u32 texture_bytes;
  switch (texture_format.pixel_fmt) {
    case tex_pixel_fmt::ARGB1555:
    case tex_pixel_fmt::RGB565:
    case tex_pixel_fmt::ARGB4444:
    case tex_pixel_fmt::YUV422:
      if (texture_format.vq) {
        const u32 code_book_size = 256 * 4 * 16;
        texture_bytes            = code_book_size + (width / 2) * (height / 2) * 1u;
        if (texture_format.mip)
          texture_bytes += texture_logic::vq_mipmap_offset(width);
      } else {
        texture_bytes = width * height * sizeof(u16);
        if (texture_format.mip)
          texture_bytes += texture_logic::nonvq_mipmap_offset(width);
      }
      break;
    case tex_pixel_fmt::Palette4:
      texture_bytes = width * height / 2;
      break;
    case tex_pixel_fmt::Palette8:
      texture_bytes = width * height;
      break;
    case tex_pixel_fmt::Reserved:
      texture_bytes = width * height * 4u;
      break;
    case tex_pixel_fmt::BumpMap:
      texture_bytes = width * height * 2;
      break;
    default:
      assert(0);
      _check(false, "Unhandled texture format encountered");
      break;
  }
  return texture_bytes;
}

}
