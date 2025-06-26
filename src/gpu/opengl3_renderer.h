#pragma once

#include "gpu/renderer.h"
#include "gpu/holly.h"
#include "opengl_shader_program.h"
#include "gpu/framebuffer.h"

namespace gpu {

namespace render {
struct FrameData;
struct Triangle;
struct DisplayList;
}

class BaseOpenGL3Renderer : public Renderer {
public:
  BaseOpenGL3Renderer(Console *console);
  virtual ~BaseOpenGL3Renderer();

  /** Handle rendering for a frame of data from the console. */
  void render_backend(const render::FrameData &rq) override;

  /** Generate a new frame for the front-end UI. */
  void render_frontend(unsigned width, unsigned height) override;

  void save_screenshot(const std::string &filename) override;

private:
  /* Local State */
  bool m_fb_enable;

  /* OpenGL State */
  std::unique_ptr<ShaderProgram> ta_shader;
  std::unique_ptr<ShaderProgram> overdraw_shader;

  u32 m_render_buffer;
  u32 m_render_depth;
  u32 m_render_texture;
  u32 m_query_object;

  void render_normal(const render::FrameData &);
  void render_oit_peeling(const render::FrameData &);
  void render_overdraw(const render::FrameData &);

  // Polygon drawing functions
  void draw_triangle(float const *vertex_data);
  void draw_quad(float const *vertex_data);
  void draw_full_triangles(const render::Triangle *triangles, u32 num_triangles);
  void render_triangles(const render::DisplayList &display_list);

  void bind_display_list_texture(const gpu::render::DisplayList &display_list,
                                 int texture_unit);

  u32 polygon_vbo;
  u32 polygon_vao;
  u32 polygon_ebo;
  u32 fog_texture;
  u32 palette_ram_texture;

  std::unordered_map<u32, u32> m_tex_uuid_to_host_id;

  std::unique_ptr<Framebuffer> dreamcast_framebuffer[2];
  unsigned last_framebuffer_written_to = 1;

  std::unique_ptr<Framebuffer> presentation_framebuffer;
};

};
