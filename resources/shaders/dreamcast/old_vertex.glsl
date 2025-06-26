#version 330 core

layout (location = 0) in vec3 a_position;
layout (location = 1) in vec2 a_tex_coord;
layout (location = 2) in vec4 a_color;
layout (location = 3) in vec4 a_color_offset;

out vec2 tex_coord;
out vec4 color;
out vec4 color_offset;

void main(void)
{
  tex_coord = a_tex_coord;
  color = a_color;
  color_offset = a_color_offset;

  float w = 1.0 / a_position.z;
  if(w < 0.0) {
    gl_Position = vec4(0.0, 0.0, 0.0, w);
    return;
  }

  vec4 pos = vec4(a_position.xyz, w);
  pos.x = (pos.x / 320.0 - 1.0);
  pos.y = (pos.y / 240.0 - 1.0) * -1.0;
  pos.zw = vec2(w, w);
  pos.xy *= pos.w;

  gl_Position = pos;
}
