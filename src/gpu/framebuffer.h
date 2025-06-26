#pragma once

#include <shared/types.h>

class Framebuffer {
private:
  int m_samples;
  u32 m_framebuffer;
  u32 m_depth;
  u32 m_colortex;
  u32 m_width;
  u32 m_height;

public:
  Framebuffer(int width, int height, int samples);
  ~Framebuffer();

  void bind();
  void unbind();
  int width() const
  {
    return m_width;
  }
  int height() const
  {
    return m_height;
  }

  u32 get_color_texture() const
  {
    return m_colortex;
  }
  u32 get_framebuffer_object() const
  {
    return m_framebuffer;
  }
  u32 get_depth_texture() const;
};