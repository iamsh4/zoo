#version 460 core

#extension GL_EXT_shader_16bit_storage : require

layout (std430, set = 0, binding = 0) buffer PixBuf {
	uint16_t pixels[1024*512];
} ps1_vram;

layout (set=0, binding=1) uniform sampler2D vram_tex;

layout (location = 0) in vec4 color_in;
layout (location = 1) in vec2 tex_uv;
layout (location = 2) flat in uvec2 draw_params;

#include "ps1_shared.glsl"

layout (location = 0) out vec4 color_out;

////////////////////////////////////////////////////////////////////////

// DrawParam unpacking
uint tex_page_x_base()     { return (draw_params.x >> 0) & 0xf; }
uint tex_page_y_base()     { return (draw_params.x >> 4) & 0x1; }
uint tex_page_blend_mode() { return (draw_params.x >> 5) & 0x3; }
uint tex_page_color_mode() { return (draw_params.x >> 7) & 0x3; }

uint opcode()              { return (draw_params.y >> 0) & 0xff; }

bool textured()            { return ((draw_params.y >> 2) & 0x1) != 0; }
bool semi_transparent()    { return ((draw_params.y >> 1) & 0x1) != 0; }
bool gouraud()             { return ((draw_params.y >> 4) & 0x1) == 0; }
bool color_blending()      { return ((draw_params.y >> 0) & 0x1) == 0; }

bool dither_enabled()      { 

  // First check if it's even enabled in the texpage.
  const uint texpage_bit = (draw_params.x >> 9) & 1;
  if(texpage_bit == 0) {
    return false;
  }

  const uint operation = (opcode() >> 5) & 7;

  // LINEs are dithered (no matter if they are mono or do use gouraud shading).
  if(operation == 2) {
    return true;
  }

  // POLYGONs (triangles/quads) are dithered ONLY if they do use gouraud shading or texture blending.
  if(operation == 1 && (gouraud() || color_blending())) {
    return true;
  }

  return false; 
}

vec3 dither(vec3 color) {
  // An offset is added based on position on the screen. (It's a tiled 4x4 table of offsets applied
  // uniformly to each of R, G, and B)
  const float offsets[4][4] = {
    {-4, 0, -3, 1},
    {2, -2, 3, -1},
    {-3, 1, -4, 0},
    {3, -1, 2, -2}
  };

  const uint frag_x = uint(gl_FragCoord.x);
  const uint frag_y = uint(gl_FragCoord.y);
  const vec3 offset = vec3(offsets[frag_x & 3][frag_y & 3]);

  // Perform the dithering with the offset from the lookup table
  color = clamp(color * 255 + offset, vec3(0), vec3(255));
  color = floor(color / 8);
  color = color * 8;

  return color * (1.0 / 255.0);
}

// CLUT Location
// 0-5      X coordinate X/16  (ie. in 16-halfword steps)
// 6-14     Y coordinate 0-511 (ie. in 1-line steps)
uint clut_vram_address() {
  uint clut_x_param = (draw_params.x >> (16+0)) & 0x3f;  // 6 bit
  uint clut_y_param = (draw_params.x >> (16+6)) & 0x1ff; // 9 bit
  return 1024 * clut_y_param + 16 * clut_x_param;
}

vec3 color16_to_vec3(uint col16) {
  uint r = (col16 >>  0) & 0x1f;
  uint g = (col16 >>  5) & 0x1f;
  uint b = (col16 >> 10) & 0x1f;
  return vec3(r,g,b) / float(0x1f);
}

uint clut_query(uint clut_index) {
  uint color_address = clut_vram_address() + clut_index;
  return uint(ps1_vram.pixels[color_address]);
}

bool is_outside_drawing_area() {
  const uint left = gpu_state.drawing_area.top_left & 0xffff;
  const uint top = (gpu_state.drawing_area.top_left >> 16) & 0xffff;
  const uint right = gpu_state.drawing_area.bottom_right & 0xffff;
  const uint bottom = (gpu_state.drawing_area.bottom_right >> 16) & 0xffff;

  const bool is_outside = 
    (gl_FragCoord.x < left || gl_FragCoord.x > right) || 
    (gl_FragCoord.y < top  || gl_FragCoord.y > bottom);
  return is_outside;
}

////////////////////////////////////////////////////////////////////////

void main()
{
  if(is_outside_drawing_area()) {
    discard;
    return;
  }

  // Fill VRAM is extremely simple and has some edge cases ignoring normal behaviors.
  if(opcode() == 0x02) {
    color_out = vec4(color_in.xyz, 1.0);
    return;
  }

  vec4 color = color_in;

  if(textured()) {
    const uint color_mode = tex_page_color_mode();

    // for 16bit:1, for 8bit CLUT: 2, for 4bit CLUT: 4
    const uint texels_per_halfword[4] = {4, 2, 1, 1};
    const uint halfword_within_page = uint(tex_uv.x / texels_per_halfword[color_mode]);

    const uint vram_line_start = 1024 * ((tex_page_y_base() * 256) + uint(tex_uv.y));
    const uint vram_halfword_offset = tex_page_x_base()*64 + halfword_within_page;
    const uint vram_address = vram_line_start + vram_halfword_offset;

    uint vram_sample = uint(ps1_vram.pixels[vram_address]);

    // CLUT = 4bit
    color = vec4(0,0,0,1);
    uint color16;

    if(color_mode == 0) {
      // 4bit breaks 16bits into 4 4-bit colors. 0-3 left-most, 4-7, 8-11, 12-15 right-most.
      uint sub_pixel = uint(tex_uv.x) & 3;
      uint clut_index = (vram_sample >> (sub_pixel*4)) & 0xf;
      color16 = clut_query(clut_index);
    }

    // CLUT = 8bit
    else if(color_mode == 1) {
      // 8bit breaks 16bits into 2 8-bit colors. 0-7 left-most, 8-15 right-most.
      uint sub_pixel = (uint(tex_uv.x) >> 3) & 1;
      uint clut_index = vram_sample;
      clut_index = clut_index >> (sub_pixel * 8);
      color16 = clut_query(clut_index & 0xff);
    }

    else if(color_mode == 2) {
      color16 = vram_sample;
    }

    else {
      // yellow, color_mode 3 is not valid.
      color16 = 0x7c1f;
    }

    const bool tex_mask_bit_set = (color16 & 0x8000) != 0;
    const bool is_transparent = semi_transparent() && tex_mask_bit_set;
    // TODO : semi-transparent blending stuff

    if((color16 == 0) || (is_transparent && color16 == 0x8000)) {
      discard;
      return;
    }

    color.xyz = color16_to_vec3(color16);
    // Raw vs color_blending
    if(color_blending()) {
      color.xyz *= color_in.xyz * 2;
    }
  }
  else
  {
    // Untextured
    color = color_in;
  }

  // TODO : handle blending

  if(dither_enabled()) {
    color.xyz = dither(color.xyz);
  }

  color_out = vec4(color.xyz, 1.0);
}
