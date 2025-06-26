
#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#include <SDL2/SDL_opengl.h>
#endif

#include "gpu/framebuffer.h"

Framebuffer::Framebuffer(int width, int height, int samples)
  : m_samples(samples),
    m_width(width),
    m_height(height)
{
  glGenFramebuffers(1, &m_framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);

  // Depth data.
  // NOTE : We don't support multi-sampled depth for textures.
  if (samples > 1) {
    glGenRenderbuffers(1, &m_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, m_depth);

    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_FLOAT, width, height);
    glFramebufferRenderbuffer(
      GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depth);
  } else {
    glGenTextures(1, &m_depth);
    glBindTexture(GL_TEXTURE_2D, m_depth);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_DEPTH_COMPONENT,
                 width,
                 height,
                 0,
                 GL_DEPTH_COMPONENT,
                 GL_FLOAT,
                 NULL);
    glFramebufferTexture2D(
      GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depth, 0);
  }

  // Color data
  glGenTextures(1, &m_colortex);
  if (samples > 1) {
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, m_colortex);
    glTexImage2DMultisample(
      GL_TEXTURE_2D_MULTISAMPLE, samples, GL_RGBA, m_width, m_height, GL_TRUE);
    glFramebufferTexture2D(
      GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE, m_colortex, 0);
  } else {
    glBindTexture(GL_TEXTURE_2D, m_colortex);
    glTexImage2D(
      GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glFramebufferTexture2D(
      GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_colortex, 0);
  }

  assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

u32
Framebuffer::get_depth_texture() const
{
  // Currently, we don't use a texture if MSAA is enabled.
  assert(m_samples == 1 && "Depth textures only available for 1 SPP (i.e. No-MSAA)");
  return m_depth;
}

Framebuffer::~Framebuffer()
{
  unbind();
  // TODO : delete FrameBuffer and attachments
}

void
Framebuffer::bind()
{
  glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
}

void
Framebuffer::unbind()
{
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
