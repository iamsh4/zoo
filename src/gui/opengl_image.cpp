#include "gui/opengl_image.h"
#if defined(ZOO_OS_MACOS)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

OpenGLImage::OpenGLImage(u32 width, u32 height) : width(width), height(height)
{
  pixel_data.resize(width * height, 0xFF00ff00);
  glGenTextures(1, &opengl_handle);
  glBindTexture(GL_TEXTURE_2D, opengl_handle);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  update_texture();
}

OpenGLImage::~OpenGLImage()
{
  glDeleteTextures(1, &opengl_handle);
}

void
OpenGLImage::fill(u32 color)
{
  for (u32 i = 0; i < pixel_data.size(); ++i) {
    pixel_data[i] = color;
  }
}

void
OpenGLImage::write_pixel(u32 x, u32 y, u32 color)
{
  assert(x < width);
  assert(y < height);
  pixel_data[width * y + x] = color;
}

void
OpenGLImage::update_texture()
{
  glBindTexture(GL_TEXTURE_2D, opengl_handle);
  glTexImage2D(GL_TEXTURE_2D,
               0,
               GL_RGBA,
               width,
               height,
               0,
               GL_RGBA,
               GL_UNSIGNED_BYTE,
               pixel_data.data());
}