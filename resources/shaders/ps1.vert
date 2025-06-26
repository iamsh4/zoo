#version 430 core

layout (location = 0) in vec4 xyuv;
layout (location = 1) in vec4 color;
layout (location = 2) in uvec2 draw_params;

layout (location = 0) out vec4 color_out;
layout (location = 1) out vec2 tex_out;
layout (location = 2) flat out uvec2 draw_params_out;

#include "ps1_shared.glsl"

void main()
{
  const uint opcode = draw_params.y & 0xff;
  const bool is_transfer_command = (opcode == 0x02) || (opcode==0x80);

  vec2 pos = xyuv.xy;
  if(!is_transfer_command){
    // Drawing offset
    vec2 offset;
    offset.x = float(gpu_state.drawing_offset & 0xffff);
    offset.y = float((gpu_state.drawing_offset >> 16) & 0xffff);
    pos += offset;
  }

  // Position
  // float x = -1.0 + pos * (2.0 / 1024.0);
  // float y = -1.0 + pos * (2.0 / 512.0);
  pos = vec2(-1, -1) + pos * vec2(2.0 / 1024.0, 2.0 / 512.0);
  gl_Position = vec4(pos, 0.0, 1.0);

  // Texture coordinates are stored in the z and w components of xyuv
  tex_out = xyuv.zw;

  // Various bits of draw parameters, sent without interpolation to FS
  draw_params_out = draw_params;

  // Need to de-swizzle command color data vs framebuffer components
  // TODO : we can remove the need for this in the GPU.
  color_out.rgba = color.bgra;
}
