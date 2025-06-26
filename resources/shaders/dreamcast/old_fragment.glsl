#version 330 core

uniform sampler2D tex0;
uniform int param_control_word;
uniform int tsp_word;
uniform int tex_word;

uniform int pass_number;
uniform sampler2D input_depth_tex;

uniform bool draw_quad;
uniform bool draw_quad_textured;

uniform bool debug_hovered;

uniform sampler1D fog_data;
uniform float FOG_DENSITY;
uniform int FOG_COL_RAM;
uniform int FOG_COL_VERT;
uniform int FOG_CLAMP_MAX;
uniform int FOG_CLAMP_MIN;

uniform sampler1D palette_colors;

in vec2 tex_coord;
in vec4 color;
in vec4 color_offset;

out vec4 outColor;

vec4 unpack_argb(int data)
{
  vec4 result;
  result.a = float( (data >> 24) & 0xFF ) / 255.f;
  result.r = float( (data >> 16) & 0xFF ) / 255.f;
  result.g = float( (data >>  8) & 0xFF ) / 255.f;
  result.b = float( (data      ) & 0xFF ) / 255.f;
  return result;
}

float compute_depth() {
  ///////////////////////////////////////////////
  // Output depth
  float w = 1.0 / gl_FragCoord.w;
  return log2(1.0 + w) / 17.0;
}

vec4 compute_output_color()
{
  vec4 out_color;

  ///////////////////////////////////////////////
  // Now let's figure out the color.

  // Configuration from Global Params/ISP/TSP data

  bool use_offset = (param_control_word & (1<<2)) != 0;
  bool texture_enable = (param_control_word & (1<<3)) != 0;

  bool use_alpha = (tsp_word & (1<<20)) != 0;
  bool ignore_texture_alpha = (tsp_word & (1<<19)) != 0;
  bool flip_u = (tsp_word & (1<<18)) != 0;
  bool flip_v = (tsp_word & (1<<17)) != 0;
  bool clamp_u = (tsp_word & (1<<16)) != 0;
  bool clamp_v = (tsp_word & (1<<15)) != 0;
  int texture_shading_instruction = (tsp_word >> 6) & 3;

  ///////////////////////////////////////////////

  // Compute input color data

  // Base color
  vec4 base_color = color;
  if(!use_alpha)
    base_color.a = 1.0;

  // Texture color
  vec4 texture_color = vec4(0.0);
  if (texture_enable) {
    vec2 uv = tex_coord;

    // If clamping is used, it disables uv-flipping
    bool uses_clamping = clamp_u || clamp_v;
    if(uses_clamping)
    {
      if(clamp_u)
        uv.s = clamp(uv.s, 0.0, 1.0);
      if(clamp_v)
        uv.t = clamp(uv.t, 0.0, 1.0);
    }
    else
    {
      if( mod(uv.s, 2.0) >= 1.0 && flip_u )
        uv.s = 1.0 - mod(uv.s, 1.0);

      if( mod(uv.t, 2.0) >= 1.0 && flip_v )
        uv.t = 1.0 - mod(uv.t, 1.0);
    }

    texture_color = texture(tex0, uv);

    bool is_palette = ((tex_word >> 27) & 7 ) >= 5;
    if (is_palette) {
      float pal_u  = (texture_color.g * 255.0) * 256.0;
            pal_u += texture_color.r * 255.0;
            pal_u  = pal_u / 1024.0;
      texture_color = texture(palette_colors, pal_u);
    }

    if(ignore_texture_alpha)
      texture_color.a = 1.0;
  }

  // Offset Color
  vec4 offset = vec4(0.0);
  if(use_offset)
    offset = max(offset, color_offset);

  ///////////////////////////////////////////////
  // Combine colors

  if(texture_enable)
  {
    // "..., this setting is invalid for non-textured polygons."

    if(texture_shading_instruction == 0) // Decal
    {
      out_color.rgb = texture_color.rgb + offset.rgb;
      out_color.a   = texture_color.a;
    }
    if(texture_shading_instruction == 1) // Modulate
    {
      out_color.rgb = base_color.rgb * texture_color.rgb + offset.rgb;
      out_color.a   = texture_color.a;
    }
    if(texture_shading_instruction == 2) // Decal Alpha
    {
      out_color.rgb  = texture_color.rgb * texture_color.a;
      out_color.rgb += base_color.rgb * (1.0-texture_color.a);
      out_color.rgb += offset.rgb;
      out_color.a    = base_color.a;
    }
    if(texture_shading_instruction == 3) // Modulate Alpha
    {
      // texture_color.rgb = vec3(1, 1 ,1); // EXPERIMENTS
      // texture_color.rgb = clamp(texture_color.rgb, vec3(0,0,0), vec3(1,1,1));
      // base_color.rgb = vec3(1,1,1);
      // offset.rgb = vec3(0,0,0);

      out_color.rgb = base_color.rgb * texture_color.rgb + offset.rgb;
      out_color.a   = base_color.a * texture_color.a;
    }
  }
  else
  {
    out_color = base_color;
  }

  //////////////////////////////////
  // Fog Processing

  // All in (packed u32, ARGB increasing address order)
  vec4 fog_color_max = unpack_argb(FOG_CLAMP_MAX);
  vec4 fog_color_min = unpack_argb(FOG_CLAMP_MIN);

  bool uses_fog_color_clamp = ((tsp_word >> 21) & 1) != 0;
  if(uses_fog_color_clamp)
      out_color = clamp(out_color, fog_color_min, fog_color_max);

  int fog_control = (tsp_word >> 22) & 3;

  // Per-vertex fog intensity
  if((fog_control == 1) && use_offset)
  {
    vec4 fog_color = unpack_argb(FOG_COL_VERT);
    fog_color.a = offset.a;
    out_color.rgb = (1.0 - fog_color.a) * out_color.rgb + fog_color.a * fog_color.rgb;
  }

  // Look-up Table fog
  else if(fog_control == 0)
  {
    float w = gl_FragCoord.w;
    float s = clamp(w * FOG_DENSITY, 1.0, 255.9999);

    float H = floor(log2(s));
    float L = (16.0 * s / pow(2.0, H)) - 16.0;
    float idx = 16*H + L;
    float fog_table_address = idx / 128.0;

    vec4 fog_color = unpack_argb(FOG_COL_RAM);
    float fog_coef = texture(fog_data, fog_table_address).r;

    out_color.rgb = mix(out_color.rgb, fog_color.rgb, fog_coef);
  }

  else
  {
    // TODO : Other fog modes
  }

  return out_color;
}

void main()
{
  ///////////////////////////////////////////////
  // Debug rendering
  if (debug_hovered) {
    outColor = vec4(1.0, 0.0, 1.0, 1.0);
    gl_FragDepth = compute_depth();
    return;
  }

  ///////////////////////////////////////////////
  // Hack : Simply draw a quad without any other controls.
  // Used for drawing full-screen quads.
  // This should be moved to a separate shader.
  if(draw_quad) {
    if(draw_quad_textured)
      outColor = texture(tex0, tex_coord);
    else
      outColor = color;
    gl_FragDepth = compute_depth();
    return;
  }

  float pixel_depth = compute_depth();

  // OIT : Is this fragment's depth greater than whatever's already been written?
  if(pass_number > 0) {
    // If this fragment would be further away than things we've captured so far,
    // then discard.
    float previous_depth = texelFetch(input_depth_tex, ivec2(gl_FragCoord.xy), 0).r;
    if(pixel_depth >= previous_depth) {
      discard;
      return;
    }
  }

  vec4 pixel_color = compute_output_color();
  if (pixel_color.a == 0) {
    discard;
    return;
  }

  outColor = pixel_color;
  gl_FragDepth = pixel_depth;
}
