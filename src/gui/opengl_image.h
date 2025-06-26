#pragma once

#include "shared/types.h"
#include <vector>

struct OpenGLImage {
  u32 width;
  u32 height;

  std::vector<u32> pixel_data;
  u32 opengl_handle;

  OpenGLImage(u32 width, u32 height);
  ~OpenGLImage();
  
  void write_pixel(u32 x, u32 y, u32 color);
  void fill(u32 color);
  void update_texture();
};