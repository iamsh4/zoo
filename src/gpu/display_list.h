#pragma once

#include <vector>
#include <memory>

#include <shared/types.h>
#include <shared/math.h>
#include <gpu/texture.h>
#include "gpu/ta_commands.h"

namespace gpu::render {

// Each vertex has a set number of attributes
struct Vertex {
  Vec3f position;
  Vec2f uv;
  Vec4f base_color;
  Vec4f offset_color;
};

struct Triangle {
  Vertex vertices[3];
};

// A sequence of triangles with the same global parameters.
struct DisplayList {
  ta_param_word param_control_word;
  ta_isp_word isp_word;
  ta_tsp_word tsp_word;
  ta_tex_word tex_word;
  TextureKey texture_key;
  std::vector<Triangle> triangles;

  struct {
    bool is_hovered = false;
    bool draw_disabled = false;
  } debug;
};

struct FrameData {
  std::vector<DisplayList> display_lists;
  u32 palette_colors[1024];
  DisplayList background;

  struct {
    u32 fog_color_lookup_table;
    u32 fog_color_per_vertex;
    u32 fog_density;
    u32 fog_clamp_max;
    u32 fog_clamp_min;
  } fog_data;

  std::vector<float> fog_table_data;

  bool dirty = false;
  u32 frame_number;

  FrameData &operator=(FrameData &&other)
  {
    if (this != &other) {
      dirty = other.dirty;
      frame_number = other.frame_number;

      display_lists = std::move(other.display_lists);
      background = std::move(other.background);
      fog_data = other.fog_data;
      fog_table_data = other.fog_table_data;
      memcpy(palette_colors, other.palette_colors, sizeof(palette_colors));

      other.display_lists.clear();
    }

    return *this;
  }
};

}