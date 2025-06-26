#include <fmt/core.h>
#include <stdexcept>
#include <string>
#include <vector>
#include <mutex>
#include <cstring>
#include <cmath>

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#include <SDL2/SDL_opengl.h>
#include <GL/glu.h>
#endif

#include "shared/file.h"
#include "core/console.h"
#include "shared/math.h"
#include "gpu/display_list.h"
#include "gpu/texture_manager.h"
#include "gpu/opengl3_renderer.h"
#include "gpu/opengl_shader_program.h"
#include "gpu/framebuffer.h"
#include "shared/profiling.h"
#include "shared/stopwatch.h"

// Rendering enhancements

static const float framebuffer_scale_multiplier = 1;
static const int framebuffer_msaa_samples       = 1;
static const bool mipmap_everything             = false;

static const u32 framebuffer_width  = (u32)(640 * framebuffer_scale_multiplier);
static const u32 framebuffer_height = (u32)(480 * framebuffer_scale_multiplier);

static std::atomic<i32> global_opengl_texture_count;

int debug_max_depth_peeling_count = 8;

namespace gpu {

BaseOpenGL3Renderer::BaseOpenGL3Renderer(Console *console)
  : Renderer(console),
    m_fb_enable(false)
{
  const std::filesystem::path path_vs = "resources/shaders/dreamcast/old_vertex.glsl";
  const std::filesystem::path path_fs = "resources/shaders/dreamcast/old_fragment.glsl";

  ta_shader = std::make_unique<ShaderProgram>(path_vs, path_fs);

  // Main Rendering Framebuffer
  dreamcast_framebuffer[0] = std::make_unique<Framebuffer>(
    framebuffer_width, framebuffer_height, framebuffer_msaa_samples);
  dreamcast_framebuffer[1] = std::make_unique<Framebuffer>(
    framebuffer_width, framebuffer_height, framebuffer_msaa_samples);
  last_framebuffer_written_to = 1;

  // We write the multi-sampled main rendering framebuffer into this one so it can be
  // rendered to the user.
  presentation_framebuffer =
    std::make_unique<Framebuffer>(framebuffer_width, framebuffer_height, 1);

  ////////

  glGenBuffers(1, &polygon_vbo);
  glGenVertexArrays(1, &polygon_vao);
  glGenBuffers(1, &polygon_ebo);
  glGenQueries(1, &m_query_object);

  glGenTextures(1, &fog_texture);
  global_opengl_texture_count++;
  glBindTexture(GL_TEXTURE_1D, fog_texture);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glGenTextures(1, &palette_ram_texture);
  global_opengl_texture_count++;
  glBindTexture(GL_TEXTURE_1D, palette_ram_texture);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

BaseOpenGL3Renderer::~BaseOpenGL3Renderer()
{
  // TODO
}

void
BaseOpenGL3Renderer::draw_triangle(float const *vertex_data)
{
  static const u32 indices[] = { 0, 1, 2 };

  const int vertex_size = (3 + 2 + 4 + 4) * sizeof(float);

  glBindVertexArray(polygon_vao);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, polygon_ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

  glBindBuffer(GL_ARRAY_BUFFER, polygon_vbo);
  glBufferData(GL_ARRAY_BUFFER, vertex_size * 3, vertex_data, GL_STATIC_DRAW);

  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, vertex_size, (void *)0); // Position
  glEnableVertexAttribArray(0);

  glVertexAttribPointer(1,
                        2,
                        GL_FLOAT,
                        GL_FALSE,
                        vertex_size,
                        (void *)(3 * sizeof(float))); // Texture Coordinates
  glEnableVertexAttribArray(1);

  glVertexAttribPointer(
    2, 4, GL_FLOAT, GL_FALSE, vertex_size, (void *)(5 * sizeof(float))); // Color
  glEnableVertexAttribArray(2);

  glVertexAttribPointer(
    3, 4, GL_FLOAT, GL_FALSE, vertex_size, (void *)(9 * sizeof(float))); // Offset Color
  glEnableVertexAttribArray(3);

  glBindVertexArray(polygon_vao);
  glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, 0);
  glBindVertexArray(0);
}

void
BaseOpenGL3Renderer::draw_full_triangles(const render::Triangle *triangles,
                                         u32 num_triangles)
{
  static_assert((3 * 13 * sizeof(float)) == sizeof(render::Triangle));
  const int vertex_size = (3 + 2 + 4 + 4) * sizeof(float);

  const float *vertex_data = (float *)triangles;

  glBindVertexArray(polygon_vao);

  glBindBuffer(GL_ARRAY_BUFFER, polygon_vbo);
  glBufferData(
    GL_ARRAY_BUFFER, vertex_size * 3 * num_triangles, vertex_data, GL_STATIC_DRAW);

  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, vertex_size, (void *)0); // Position
  glEnableVertexAttribArray(0);

  glVertexAttribPointer(1,
                        2,
                        GL_FLOAT,
                        GL_FALSE,
                        vertex_size,
                        (void *)(3 * sizeof(float))); // Texture Coordinates
  glEnableVertexAttribArray(1);

  glVertexAttribPointer(
    2, 4, GL_FLOAT, GL_FALSE, vertex_size, (void *)(5 * sizeof(float))); // Color
  glEnableVertexAttribArray(2);

  glVertexAttribPointer(
    3, 4, GL_FLOAT, GL_FALSE, vertex_size, (void *)(9 * sizeof(float))); // Offset Color
  glEnableVertexAttribArray(3);

  glBindVertexArray(polygon_vao);
  glDrawArrays(GL_TRIANGLES, 0, 3 * num_triangles);
  glBindVertexArray(0);
}

void
BaseOpenGL3Renderer::render_triangles(const gpu::render::DisplayList &display_list)
{
  static u32 current_ta_cull_mode = 0xFFFFFFFF;

  const auto &tsp_word(display_list.tsp_word);
  const auto &isp_word(display_list.isp_word);
  const auto &param_control_word(display_list.param_control_word);

  // Change alpha blending
  static const int ta_to_opengl_blending_src[] = { GL_ZERO,      GL_ONE,
                                                   GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR,
                                                   GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                                                   GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA };

  static const int ta_to_opengl_blending_dst[] = { GL_ZERO,      GL_ONE,
                                                   GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR,
                                                   GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                                                   GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA };

  u32 new_src_func = ta_to_opengl_blending_src[tsp_word.src_alpha];
  u32 new_dst_func = ta_to_opengl_blending_dst[tsp_word.dst_alpha];
  glBlendFunc(new_src_func, new_dst_func);

  if (display_list.debug.is_hovered) {
    glBlendFunc(GL_ONE, GL_ZERO);
  }

  if (param_control_word.texture) {
    const bool clamp_u = tsp_word.clamp_uv & 2;
    const bool clamp_v = tsp_word.clamp_uv & 1;
    glTexParameteri(
      GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, clamp_u ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    glTexParameteri(
      GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, clamp_v ? GL_CLAMP_TO_EDGE : GL_REPEAT);
  }

  //////////

  if (0) {
    // Cull mode logic
    const auto new_cull_mode = isp_word.opaque_or_translucent.culling_mode;
    if (current_ta_cull_mode != new_cull_mode) {
      if (new_cull_mode == 0)
        glDisable(GL_CULL_FACE);
      else {
        glEnable(GL_CULL_FACE);
        // TODO : Handle other culling modes
      }

      current_ta_cull_mode = new_cull_mode;
    }
  }

  //////////

  ta_shader->setUniform1i("param_control_word", reinterpret<i32>(param_control_word.raw));
  ta_shader->setUniform1i("tsp_word", reinterpret<i32>(tsp_word.raw));
  ta_shader->setUniform1i("tex_word", reinterpret<i32>(display_list.tex_word.raw));

  if (display_list.debug.is_hovered) {
    ta_shader->setUniform1i("debug_hovered", 1);
    draw_full_triangles(&display_list.triangles[0], display_list.triangles.size());
    ta_shader->setUniform1i("debug_hovered", 0);
  } else {
    u64 triangle_count = display_list.triangles.size();
    draw_full_triangles(&display_list.triangles[0], triangle_count);
  }
}

void
BaseOpenGL3Renderer::render_backend(const render::FrameData &frame_data)
{
  if (ta_shader->wasSourceModified()) {
    ta_shader->compileAndLink();
  }

  const u64 start = epoch_nanos();
  render_oit_peeling(frame_data);
  const u64 end = epoch_nanos();

  m_console->metrics().increment(zoo::dreamcast::Metric::NanosRender, end - start);
}

void
BaseOpenGL3Renderer::bind_display_list_texture(
  const gpu::render::DisplayList &display_list,
  int texture_unit)
{
  assert(display_list.param_control_word.texture);

  auto texture =
    m_console->texture_manager()->get_texture_handle(display_list.texture_key);
  glActiveTexture(GL_TEXTURE0 + texture_unit);
  glEnable(GL_TEXTURE_2D);
  ta_shader->setUniform1i("tex0", texture_unit);

  bool created_texture = false;
  if (auto it = m_tex_uuid_to_host_id.find(texture->uuid);
      it != m_tex_uuid_to_host_id.end()) {
    assert(texture->is_host_allocated);
    glBindTexture(GL_TEXTURE_2D, it->second);
  } else {
    /* Need to turn converted data into OGL textures */
    glGenTextures(1, &texture->host_texture_id);
    texture->is_host_allocated           = true;
    m_tex_uuid_to_host_id[texture->uuid] = texture->host_texture_id;
    global_opengl_texture_count++;
    glBindTexture(GL_TEXTURE_2D, texture->host_texture_id);
    created_texture = true;
  }

  if (texture->is_dirty || created_texture) {

    if (texture->tex_word.mip || mipmap_everything) {
      glTexImage2D(GL_TEXTURE_2D,
                   0,
                   GL_RGBA,
                   texture->width,
                   texture->height,
                   0,
                   GL_RGBA,
                   GL_UNSIGNED_BYTE,
                   texture->data.get());
      glGenerateMipmap(GL_TEXTURE_2D);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    } else {
      glTexImage2D(GL_TEXTURE_2D,
                   0,
                   GL_RGBA,
                   texture->width,
                   texture->height,
                   0,
                   GL_RGBA,
                   GL_UNSIGNED_BYTE,
                   texture->data.get());

      // If we're not doing mipmapping, consider if this is point or bilinear sampling
      if (display_list.tsp_word.filter_mode == 0) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      }
    }

    texture->is_dirty = false;
  }
}

void
BaseOpenGL3Renderer::render_oit_peeling(const render::FrameData &frame)
{
  glEnable(GL_BLEND);
  ta_shader->activate();

  // Clear + setup OpenGL state.
  for (int i = 0; i < 2; ++i) {
    dreamcast_framebuffer[i]->bind();
    glViewport(
      0, 0, dreamcast_framebuffer[i]->width(), dreamcast_framebuffer[i]->height());
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  }

  dreamcast_framebuffer[0]->bind();

  glDisable(GL_CULL_FACE);

  ///////////////////////////////////////////////////
  // Setup Fog data
  // TODO : Only update this data if it's changed.

  glActiveTexture(GL_TEXTURE1);
  glEnable(GL_TEXTURE_1D);
  glBindTexture(GL_TEXTURE_1D, fog_texture);
  if (const float *fog_data = &frame.fog_table_data[0]; fog_data) {
    glTexImage1D(GL_TEXTURE_1D, 0, GL_R32F, 128, 0, GL_RED, GL_FLOAT, fog_data);
  }
  ta_shader->setUniform1i("fog_data", 1);

  int fog_density_mantissa = ((frame.fog_data.fog_density >> 8) & 0xFF);
  i8 fog_density_exponent  = (frame.fog_data.fog_density & 0xFF);
  float fog_density_f = (fog_density_mantissa / 128.0f) * powf(2.f, fog_density_exponent);

  ta_shader->setUniform1f("FOG_DENSITY", fog_density_f);
  ta_shader->setUniform1i("FOG_COL_RAM", frame.fog_data.fog_color_lookup_table);
  ta_shader->setUniform1i("FOG_COL_VERT", frame.fog_data.fog_color_per_vertex);
  ta_shader->setUniform1i("FOG_CLAMP_MAX", frame.fog_data.fog_clamp_max);
  ta_shader->setUniform1i("FOG_CLAMP_MIN", frame.fog_data.fog_clamp_min);

  ///////////////////////////////////////////////////
  // Setup Palette RAM
  glActiveTexture(GL_TEXTURE3);
  glEnable(GL_TEXTURE_1D);
  glBindTexture(GL_TEXTURE_1D, palette_ram_texture);
  glTexImage1D(GL_TEXTURE_1D,
               0,
               GL_RGBA,
               1024,
               0,
               GL_RGBA,
               GL_UNSIGNED_BYTE,
               &frame.palette_colors[0]);
  ta_shader->setUniform1i("palette_colors", 3);

  ///////////////////////////////////////////////////
  //  Draw background triangles

  glDisable(GL_DEPTH_TEST);
  glEnable(GL_TEXTURE_2D);
  ta_shader->setUniform1i("pass_number", 0);

  {
    const auto &background = frame.background;
    if (frame.background.triangles.size() == 2) {
      render_triangles(background);
    }
  }

  ///////////////////////////////////////////////////
  // Cleanup host texture resources
  {
    // Any texture which is not in the texture_manager but is allocated on the
    // host should be removed. Said another, way, the host textures should match those in
    // the texture manager.

    // Create {host-allocated} - {everything in texture_manager}
    std::unordered_set<u32> deletion_set;
    for (auto &kv : m_tex_uuid_to_host_id)
      deletion_set.insert(kv.first);
    for (auto &[addr, tex] : m_console->texture_manager()->get_vram_to_textures())
      deletion_set.erase(tex->uuid);

    // Remove all of those textures since the texture_manager isn't tracking them
    for (u32 tex_uuid : deletion_set) {
      const u32 host_tex_id = m_tex_uuid_to_host_id[tex_uuid];
      glDeleteTextures(1, &host_tex_id);
      global_opengl_texture_count--;
      m_tex_uuid_to_host_id.erase(tex_uuid);
    }
  }

  ///////////////////////////////////////////////////
  // Step 0 : Render all opaque geometry, record to normal depth buffer texture.

  // Normal rendering settings
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glBindFramebuffer(GL_READ_FRAMEBUFFER,
                    dreamcast_framebuffer[0]->get_framebuffer_object());
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER,
                    dreamcast_framebuffer[0]->get_framebuffer_object());

  const auto draw_list = [&](const gpu::render::DisplayList &display_list) {
    if (display_list.debug.draw_disabled)
      return;

    if (display_list.param_control_word.texture)
      bind_display_list_texture(display_list, 0);
    else
      glDisable(GL_TEXTURE_2D);

    // TODO : We don't handle bump mapping yet.
    if (display_list.param_control_word.texture) {
      auto texture =
        m_console->texture_manager()->get_texture_handle(display_list.texture_key);
      if (texture->tex_word.pixel_fmt == tex_pixel_fmt::BumpMap) {
        return;
      }
    }

    render_triangles(display_list);
  };

  for (auto &display_list : frame.display_lists) {
    // const auto pcw       = display_list.param_control_word;
    const auto list_type = display_list.param_control_word.list_type;

    // Note: pg 113 "Sprites (textured polygons that use transparent texels) must be
    // drawn with translucent polygons, even if no α blending is performed"
    if (list_type != ta_list_type::Opaque)
      continue;

    draw_list(display_list);
  }

  last_framebuffer_written_to = 0;

  ///////////////////////////////////////////////////
  // Step 1 : Render translucent objects

  // Less-normal settings... Accept the farthest out fragment possible. The shader
  // will reject anything further than what's been drawn so far.
  glDepthFunc(GL_GEQUAL);
  glClearDepth(0);

  // Ping-pong between two framebuffers to do a kind of back-to-front depth peeling.
  // Count total samples generated per-pass and bail out if we ever draw nothing.
  for (int pass_number = 1;
       pass_number < 16 && pass_number < debug_max_depth_peeling_count;
       ++pass_number) {
    unsigned prev_fb   = last_framebuffer_written_to;
    unsigned active_fb = 1 - last_framebuffer_written_to;

    glBindFramebuffer(GL_READ_FRAMEBUFFER,
                      dreamcast_framebuffer[prev_fb]->get_framebuffer_object());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,
                      dreamcast_framebuffer[active_fb]->get_framebuffer_object());
    glClear(GL_DEPTH_BUFFER_BIT);
    {
      const int width  = dreamcast_framebuffer[prev_fb]->width();
      const int height = dreamcast_framebuffer[prev_fb]->height();
      glBlitFramebuffer(
        0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, dreamcast_framebuffer[prev_fb]->get_depth_texture());

    ta_shader->setUniform1i("input_depth_tex", 2);
    ta_shader->setUniform1i("pass_number", pass_number);

    glBeginQuery(GL_SAMPLES_PASSED, m_query_object);

    for (u32 list_i = 0; list_i < frame.display_lists.size(); ++list_i) {
      auto &display_list(frame.display_lists[list_i]);
      const auto list_type = display_list.param_control_word.list_type;
      const auto pcw       = display_list.param_control_word;

      if (!(list_type == ta_list_type::Translucent ||
            list_type == ta_list_type::PunchThrough || pcw.type == ta_para_type::Sprite))
        continue;

      // TODO: Revisit this in the future. We don't support varying depth functions yet.
      static const GLenum gl_depth_funcs[] = { GL_NEVER,  GL_LESS,    GL_EQUAL,
                                               GL_LEQUAL, GL_GREATER, GL_NOTEQUAL,
                                               GL_GEQUAL, GL_ALWAYS };
      const GLenum depth_func =
        gl_depth_funcs[display_list.isp_word.opaque_or_translucent.depth_compare_mode];
      
      glEnable(GL_DEPTH_TEST);
      glDepthFunc(gl_depth_funcs[depth_func]);

      draw_list(display_list);
    }

    glEndQuery(GL_SAMPLES_PASSED);

    dreamcast_framebuffer[active_fb]->unbind();
    last_framebuffer_written_to = active_fb;

    i32 samples_passed = -1;
    glGetQueryObjectiv(m_query_object, GL_QUERY_RESULT, &samples_passed);

    // If the last pass through the display lists didn't yield any samples, we're done.
    if (samples_passed == 0)
      break;
    else
      ; // printf("Pass %u : %u samples\n", pass_number, samples_passed);
  }

  ///////////////////////////////////////////////////
  // Cleanup
  glClearDepth(1);
  glDisable(GL_TEXTURE_2D);
  glDisable(GL_DEPTH_TEST);
}

void
BaseOpenGL3Renderer::render_normal(const render::FrameData &frame)
{
  // TODO
}

void
BaseOpenGL3Renderer::render_frontend(unsigned width, unsigned height)
{
  ProfileZone;

  bool should_draw = false;

  // PowerVR atomically places an entire new frane's data into sdl_frame_data
  // when a frame completes. So, if the queue is non-empty, it means we have
  // an entire frame of drawing commands ready. Move that data here (clearing
  // the TA's own queue at the same time) and render.
  m_console->render_lock().lock();
  if (m_console->get_frame_data().dirty) {

    // New polygons to render! Move it to "last_frame_data" and mark that dirty.
    m_console->get_last_frame_data()       = std::move(m_console->get_frame_data());
    m_console->get_last_frame_data().dirty = true;

    // Mark "frame_data" not dirty. The next time there is new content to render, this
    // will be marked true and we'll re-enter this block.
    m_console->get_frame_data().dirty = false;
    should_draw                       = true;
    m_fb_enable                       = false;
  }

  // Even if there isn't new data from the console, the debugger or other tools may have
  // modified this data and so we should draw again if that's the case.
  should_draw |= m_console->get_last_frame_data().dirty;

  // If something was just moved here, then let's re-render the frame
  if (should_draw) {
    // This is still not thread safe. TODO : Put last_frame_data under a separate lock.
    render_backend(m_console->get_last_frame_data());
  }

  // TODO: This should ideally unlock before render_backend, but we need to thread-safe
  // get last_frame_data first.
  m_console->render_lock().unlock();

  ///////////////////////////////

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDisable(GL_BLEND);
  glViewport(0, 0, width, height);
  glClearColor(0, 0, 1, 1);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  ta_shader->activate();
  ta_shader->setUniform1i("draw_quad", 1);
  ta_shader->setUniform1i("draw_quad_textured", 1);

  glEnable(GL_TEXTURE_2D);

  ////////////////////////////////
  // Rendered images are written to a framebuffer, however, software can also explicitly
  // blit graphics there by hand. This happens for instance in the opening trademark
  // screen after the boot animation. If there was a write to this region, then we'll draw
  // that instead of whatever display list might exist. This is obviously not a complete
  // solution, but every game tested so far only draws with primitives OR draws directly
  // to the framebuffer, so this works.

  ta_tex_word fb_tex;
  memset(&fb_tex, 0, sizeof(ta_tex_word));
  const u32 vram_framebuffer_address = 0x05200000;
  fb_tex.address                     = (vram_framebuffer_address - 0x04000000u) >> 8;
  fb_tex.pixel_fmt                   = tex_pixel_fmt::Reserved;

  ta_tsp_word fb_tsp;
  fb_tsp.size_u = 2;
  fb_tsp.size_v = 2;

  std::shared_ptr<Texture> framebuffer =
    m_console->texture_manager()->get_texture_handle({ fb_tex, fb_tsp });
  framebuffer->last_used_on_frame = m_console->gpu()->get_render_count();

  // TODO : Fix this.
  if (!framebuffer->is_host_allocated) {
    /* Need to turn converted data into OGL textures */
    glGenTextures(1, &framebuffer->host_texture_id);
    global_opengl_texture_count++;
    glBindTexture(GL_TEXTURE_2D, framebuffer->host_texture_id);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA,
                 framebuffer->width,
                 framebuffer->height,
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 framebuffer->data.get());

    framebuffer->is_host_allocated = true;
    m_fb_enable                    = true;
  }

  glEnable(GL_TEXTURE_2D);

  if (m_fb_enable) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, framebuffer->host_texture_id);
    ta_shader->setUniform1i("tex0", 0);

    static const float quad_vertices[] = {
      0,   0,   0.01, 0, 0, 1, 1,   1,   1,    0, 0, 0, 0, 0,   480, 0.01, 0, 1, 1, 1,
      1,   1,   0,    0, 0, 0, 640, 480, 0.01, 1, 1, 1, 1, 1,   1,   0,    0, 0, 0,

      640, 480, 0.01, 1, 1, 1, 1,   1,   1,    0, 0, 0, 0, 640, 0,   0.01, 1, 0, 1, 1,
      1,   1,   0,    0, 0, 0, 0,   0,   0.01, 0, 0, 1, 1, 1,   1,   0,    0, 0, 0,
    };

    draw_triangle(&quad_vertices[0]);
    draw_triangle(&quad_vertices[3 * 13]);

  } else {

    // Blit the dreamcast framebuffer to the one we present to the user.
    const auto &fb(dreamcast_framebuffer[last_framebuffer_written_to]);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fb->get_framebuffer_object());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,
                      presentation_framebuffer->get_framebuffer_object());
    glBlitFramebuffer(0,
                      0,
                      fb->width(),
                      fb->height(),
                      0,
                      0,
                      fb->width(),
                      fb->height(),
                      GL_COLOR_BUFFER_BIT,
                      GL_NEAREST);

    // Go back to the main output for rendering
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Bind the presentation color attachment as a texture to draw to the screen
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, presentation_framebuffer->get_color_texture());
    ta_shader->setUniform1i("tex0", 0);

    static const float quad_vertices[] = {
      0,   0,   0.01, 0, 1, 1, 1,   1,   1,    0, 0, 0, 0, 0,   480, 0.01, 0, 0, 1, 1,
      1,   1,   0,    0, 0, 0, 640, 480, 0.01, 1, 0, 1, 1, 1,   1,   0,    0, 0, 0,

      640, 480, 0.01, 1, 0, 1, 1,   1,   1,    0, 0, 0, 0, 640, 0,   0.01, 1, 1, 1, 1,
      1,   1,   0,    0, 0, 0, 0,   0,   0.01, 0, 1, 1, 1, 1,   1,   0,    0, 0, 0,
    };

    draw_triangle(&quad_vertices[0]);
    draw_triangle(&quad_vertices[3 * 13]);
  }

  glBindTexture(GL_TEXTURE_2D, 0);
  ta_shader->setUniform1i("draw_quad_textured", 0);
  ta_shader->setUniform1i("draw_quad", 0);
  glDisable(GL_TEXTURE_2D);

  return;
}

void
BaseOpenGL3Renderer::save_screenshot(const std::string &filename)
{
  const auto &fb(dreamcast_framebuffer[last_framebuffer_written_to]);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, fb->get_framebuffer_object());

  std::vector<u8> fb_buffer;
  fb_buffer.resize(4 * fb->width() * fb->height());
  glReadPixels(
    0, 0, fb->width(), fb->height(), GL_RGBA, GL_UNSIGNED_BYTE, fb_buffer.data());

  // Write to a ppm
  FILE *f = fopen(filename.c_str(), "wb");
  if (!f) {
    throw std::runtime_error(
      fmt::format("Failed to open file for writing: {}", filename));
  }
  fprintf(f, "P6\n%d %d\n255\n", fb->width(), fb->height());
  for (int y = fb->height() - 1; y >= 0; --y) {
    for (int x = 0; x < fb->width(); ++x) {
      const int i = (y * fb->width() + x) * 4;
      fputc(fb_buffer[i + 0], f);
      fputc(fb_buffer[i + 1], f);
      fputc(fb_buffer[i + 2], f);
    }
  }
}

}
