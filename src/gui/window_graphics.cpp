#include <unordered_map>
#include <unordered_set>
#include <fmt/printf.h>
#include <imgui.h>

#include "gui/window_graphics.h"
#include "gpu/holly.h"

extern int debug_max_depth_peeling_count;

namespace gui {

const float texture_scale = 1.25f;

const char *
get_pixel_format(u32 pixel_format)
{
  switch (pixel_format) {
    case gpu::tex_pixel_fmt::ARGB1555:
      return "ARGB1555";
    case gpu::tex_pixel_fmt::RGB565:
      return "RGB565";
    case gpu::tex_pixel_fmt::ARGB4444:
      return "ARGB4444";
    case gpu::tex_pixel_fmt::YUV422:
      return "YUV422";
    case gpu::tex_pixel_fmt::BumpMap:
      return "BumpMap";
    case gpu::tex_pixel_fmt::Palette4:
      return "Palette4";
    case gpu::tex_pixel_fmt::Palette8:
      return "Palette8";
    default:
      return "Unknown?";
  }
}

const char *
get_list_type_name(gpu::ta_list_type list_type)
{
  switch (list_type) {
    case gpu::ta_list_type::Opaque:
      return "Opaque";
    case gpu::ta_list_type::OpaqueModifier:
      return "Opaque Modifier Volume";
    case gpu::ta_list_type::Translucent:
      return "Translucent";
    case gpu::ta_list_type::TransModifier:
      return "Translucent Modifier Volume";
    case gpu::ta_list_type::PunchThrough:
      return "Punch-Through";
    default:
      return "???";
  }
}

void
draw_square(const Vec4f &color)
{
  float line_height = ImGui::GetTextLineHeight();
  auto *draw_list   = ImGui::GetWindowDrawList();

  const float width = line_height * 0.5f;

  ImVec2 p0      = ImGui::GetCursorScreenPos();
  ImVec2 p1      = ImVec2(p0.x + width, p0.y + line_height);
  ImU32 im_color = ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, 1.0f));
  draw_list->AddRectFilled(p0, p1, im_color);

  ImVec2 p2 = ImVec2(p0.x + width, p0.y);
  ImVec2 p3 = ImVec2(p2.x + width, p0.y + line_height);
  im_color  = ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, color.w));
  draw_list->AddRectFilled(p2, p3, im_color);

  ImGui::InvisibleButton("", ImVec2(line_height, line_height));
};

void
fill_flags_string(gpu::ta_tex_word &tex_word, char *buffer)
{
  if (tex_word.vq)
    buffer += snprintf(buffer, 16, "VQCompressed ");

  if (!tex_word.scanline)
    buffer += snprintf(buffer, 16, "Twiddled ");
  else
    buffer += snprintf(buffer, 16, "NotTwiddled ");

  if (tex_word.mip)
    buffer += snprintf(buffer, 16, "MIPMapped ");

  if (tex_word.stride)
    buffer += snprintf(buffer, 16, "Stride ");
}

GraphicsWindow::GraphicsWindow(std::shared_ptr<ConsoleDirector> director)
  : Window("Graphics Debugger"),
    m_director(director),
    m_texture_manager(director->console()->texture_manager())
{
  return;
}

void
GraphicsWindow::draw_registers()
{
  ImGui::BeginChild("GPU Registers");
  ImGui::Columns(2);
  ImGui::Text("Register Name");
  ImGui::NextColumn();
  ImGui::Text("Value");
  ImGui::NextColumn();
  ImGui::Separator();

  auto gpu = m_director->console()->gpu();

  struct Entry {
    const char *name;
    u32 ptr;
  };
  const Entry entries[] = {
    Entry { "FB_R_CTRL", gpu->_regs.FB_R_CTRL.raw },
    Entry { "FB_R_SOF1", gpu->_regs.FB_R_SOF1.raw },
    Entry { "FB_R_SOF2", gpu->_regs.FB_R_SOF2.raw },
    Entry { "FB_R_SIZE", gpu->_regs.FB_R_SIZE.raw },
    Entry { "TA_ISP_BASE", gpu->_regs.TA_ISP_BASE },
    Entry { "PARAM_BASE", gpu->_regs.PARAM_BASE },
    Entry { "REGION_BASE", gpu->_regs.REGION_BASE },
    Entry { "TA_ALLOC_CTRL", gpu->_regs.TA_ALLOC_CTRL.raw },
  };
  for (const auto &entry : entries) {
    ImGui::Text(entry.name);
    ImGui::NextColumn();
    ImGui::Text("0x%08X", entry.ptr);
    ImGui::NextColumn();
  }

  ImGui::EndChild();
}

void
GraphicsWindow::draw_region_array_data()
{
  auto gpu     = m_director->console()->gpu();
  auto &_regs  = gpu->_regs;
  auto console = m_director->console();

  ImGui::Text("Region Array begins at 0x%08x", _regs.REGION_BASE);

  ImGui::BeginChild("RegionArrayData");

  //
  ImGui::Columns(8);

  // Headers
  ImGui::Text("Index");
  ImGui::NextColumn();
  ImGui::Text("X/Y");
  ImGui::NextColumn();
  ImGui::Text("Flags");
  ImGui::NextColumn();
  ImGui::Text("Opaque");
  ImGui::NextColumn();
  ImGui::Text("Opaque Modifier");
  ImGui::NextColumn();
  ImGui::Text("Translucent");
  ImGui::NextColumn();
  ImGui::Text("Translucent Modifier");
  ImGui::NextColumn();
  ImGui::Text("PunchThrough");
  ImGui::NextColumn();
  ImGui::Separator();

  ///////////////////////

  const bool region_header_type = _regs.FPU_PARAM_CFG & (1 << 21);
  u32 addr                      = 0x0500'0000 + (_regs.REGION_BASE & 0x007F'FFFF);
  // printf("Region Array at 0x%08x\n", addr);
  for (unsigned index = 0;; index++) {
    const u32 control   = console->memory()->read<u32>(addr);
    const bool last     = control & (1 << 31);
    const bool z_clear  = control & (1 << 30);
    const bool autosort = region_header_type && ((control & (1 << 29)) == 0);
    const bool flush    = control & (1 << 28);
    const u32 tile_x    = (control >> 2) & 0x3f;
    const u32 tile_y    = (control >> 8) & 0x3f;

    u32 pointers[6] = { 0 };
    pointers[0]     = console->memory()->read<u32>(addr + 4);
    pointers[1]     = console->memory()->read<u32>(addr + 8);
    pointers[2]     = console->memory()->read<u32>(addr + 12);
    pointers[3]     = console->memory()->read<u32>(addr + 16);
    if (region_header_type) {
      pointers[4] = console->memory()->read<u32>(addr + 20);
    }

    ImGui::BeginGroup();

    ImGui::Text("%u", index);
    ImGui::NextColumn();

    ImGui::Text("%u,%u", tile_x * 32, tile_y * 32);
    ImGui::NextColumn();

    char flagstring[4];
    flagstring[0] = autosort ? 'S' : '.';
    flagstring[1] = z_clear ? '.' : 'C';
    flagstring[2] = flush ? '.' : 'F';
    flagstring[3] = 0;

    ImGui::Text(flagstring);
    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      ImGui::Text("S Autosort: %s", autosort ? "Yes" : "No ('Pre-Sorted')");
      ImGui::Text("C Z Clear: %s", z_clear ? "No" : "Yes");
      ImGui::Text("F Flush: %s", flush ? "No" : "Yes");
      ImGui::EndTooltip();
    }
    ImGui::NextColumn();

    const u32 mask = (1u << 24) - 1;
    for (unsigned list = 0; list < 5; ++list) {
      if (pointers[list] & 0x8000'0000) {
        ImGui::Text("(none)");
      } else {
        ImGui::Text("0x%08x", pointers[list] & mask);
      }
      ImGui::NextColumn();
    }

    ImGui::EndGroup();

    addr += 4 * (region_header_type ? 6 : 5);

    if (last) {
      // Last region
      break;
    }
  }

  ImGui::Columns(1);
  ImGui::EndChild();
}

void
GraphicsWindow::draw_texture_list()
{
  auto console = m_director->console();

  ImGui::BeginChild("TextureList");
  ImGui::Columns(10);

  // Headers
  ImGui::Text("OpenGL Texture Id");
  ImGui::NextColumn();
  ImGui::Text("Address");
  ImGui::NextColumn();
  ImGui::Text("Pixel Format");
  ImGui::NextColumn();
  ImGui::Text("Flags");
  ImGui::NextColumn();
  ImGui::Text("Resolution");
  ImGui::NextColumn();
  ImGui::Text("Hash");
  ImGui::NextColumn();

  ImGui::Text("UUID");
  ImGui::NextColumn();
  ImGui::Text("Host Allocated");
  ImGui::NextColumn();
  ImGui::Text("Last Updated");
  ImGui::NextColumn();
  ImGui::Text("Last Used");
  ImGui::NextColumn();

  ImGui::Separator();

  for (auto &texture_pair : console->texture_manager()->get_vram_to_textures()) {
    std::shared_ptr<gpu::Texture> texture = texture_pair.second;
    u32 width = texture->width, height = texture->height;

    ImGui::BeginGroup();

    char flags_text_buffer[256] = { 0 };
    fill_flags_string(texture->tex_word, flags_text_buffer);

    ImGui::Text("%d", texture->host_texture_id);
    ImGui::NextColumn();
    ImGui::Text("0x%08X", texture->dc_vram_address);
    ImGui::NextColumn();
    ImGui::Text("%s", get_pixel_format(texture->tex_word.pixel_fmt));
    ImGui::NextColumn();
    ImGui::Text("%s", flags_text_buffer);
    ImGui::NextColumn();
    ImGui::Text("%d x %d", width, height);
    ImGui::NextColumn();
    const auto hash_string = fmt::format("{:08x}", texture->hash);
    ImGui::Text("%s", hash_string.c_str());
    ImGui::NextColumn();

    ImGui::Text("%d", texture->uuid);
    ImGui::NextColumn();
    ImGui::Text("%d", texture->is_host_allocated);
    ImGui::NextColumn();
    ImGui::Text("%d", texture->last_updated_on_frame);
    ImGui::NextColumn();
    ImGui::Text("%d", texture->last_used_on_frame);
    ImGui::NextColumn();

    ImGui::EndGroup();
    bool is_hovered = ImGui::IsItemHovered();
    if (is_hovered) {
      u32 scaled_width = width * texture_scale, scaled_height = height * texture_scale;

      ImVec2 popup_position = ImGui::GetMousePos();
      popup_position.x -= scaled_width / 2;
      popup_position.y -= scaled_height + 32;

      ImGui::SetNextWindowPos(popup_position);

      ImGui::Begin("TexturePreview",
                   &is_hovered,
                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_AlwaysAutoResize);
      ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1, 0, 1, 1));
      ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 0, 1, 1));
      ImGui::Image((void *)(size_t)texture->host_texture_id,
                   ImVec2(scaled_width, scaled_height));
      ImGui::PopStyleColor(2);
      ImGui::End();
    }
  }

  ImGui::Columns(1);
  ImGui::EndChild();
}

static const char *color_names[] = { "Packed", "Floating", "Intensity1", "Intensity2" };

// static const char *list_type_names[] = { "Opaque",
//                                          "Opaque Modifier Volume",
//                                          "Translucent",
//                                          "Translucent Modifier Volume",
//                                          "Punch-Through Polygon" };

static const char *culling_mode_names[] = { "Culling Disabled",
                                            "Cull if Small (unsupported)",
                                            "Cull if Negative",
                                            "Cull if Positive" };

static const char *depth_compare_mode_names[] = {
  "Never", "Less", "Equal", "Less or Equal", "Greater", "Not Equal", "Greater or Equal",
  "Always"
};

static const char *alpha_instruction_names[] = { "Zero",     "One",       "Other",
                                                 "1-Other",  "SrcAlpha",  "1-SrcAlpha",
                                                 "DstAlpha", "1-DstAlpha" };

static const char *fog_mode_names[] = { "Lookup Table",
                                        "Per-Vertex",
                                        "No Fog",
                                        "Lookup Table Mode 2" };

static const char *shading_instruction_names[] = { "Decal",
                                                   "Modulate",
                                                   "Decal Alpha",
                                                   "Modulate Alpha" };

static const char *shading_instruction_equation_rgb[] = {
  "pix.rgb = tex.rgb + offset.rgb",
  "pix.rgb = col.rgb * tex.rgb + offset.rgb",
  "pix.rgb = (tex.rgb * tex.a) + (col.rgb * (1-tex.a)) + offset.rgb",
  "pix.rgb = col.rgb * tex.rgb + offset.rgb"
};

static const char *shading_instruction_equation_a[] = { "pix.a = tex.",
                                                        "pix.a = tex.a",
                                                        "pix.a = col.a",
                                                        "pix.a = col.a * tex.a" };

void
GraphicsWindow::DrawPolygonData(gpu::render::DisplayList &display_list,
                                gpu::render::Triangle &triangle)
{
  auto console = m_director->console();

  // Control Word Data
  {
    auto &pcw(display_list.param_control_word);

    ImGui::Text("Control Word (0x%08X)", pcw.raw);
    ImGui::Text(" - PCW Type    : %s", pcw.type == 4 ? "Polygon" : "Sprite");
    ImGui::Text(" - List Type   : %s",
                get_list_type_name((gpu::ta_list_type)pcw.list_type));
    ImGui::Text(" - Color Type  : %s", color_names[pcw.col_type]);
    ImGui::Text(" - Uses Offset : %s", pcw.offset ? "Yes" : "No");
    ImGui::Text(" - Shading     : %s", pcw.gouraud ? "Smooth" : "Flat");

    if (!pcw.texture)
      ImGui::Text(" - Textured    : No");
    else {
      const auto &texture =
        console->texture_manager()->get_texture_handle(display_list.texture_key);
      const auto &tex_word = texture->tex_word;
      ImGui::Text(" - Textured    : Yes (%s, %s)",
                  get_pixel_format(tex_word.pixel_fmt),
                  pcw.texture ? pcw.uv16 ? "(16-bit UV's)" : "(F32 UV's)" : "");
      ImGui::Text(
        " -  (uuid=%u, host_allocated=%u, host_id=%u, last_updated=%u, last_used=%u)",
        texture->uuid,
        texture->is_host_allocated,
        texture->host_texture_id,
        texture->last_updated_on_frame,
        texture->last_used_on_frame);
    }
  }

  // ISP Word Data
  {
    ImGui::Separator();

    auto &isp(display_list.isp_word);
    ImGui::Text("ISP Word     (0x%08X)", isp.raw);
    ImGui::Text(" - Culling Mode          : %d (%s)",
                isp.opaque_or_translucent.culling_mode,
                culling_mode_names[isp.opaque_or_translucent.culling_mode]);
    ImGui::Text(" - Depth Comparison Mode : %d (%s)",
                isp.opaque_or_translucent.depth_compare_mode,
                depth_compare_mode_names[isp.opaque_or_translucent.depth_compare_mode]);
    ImGui::Text(" - Z-Write Disable       : %d",
                isp.opaque_or_translucent.z_write_disabled);
  }

  // TSP Word Data
  {
    ImGui::Separator();

    auto &tsp(display_list.tsp_word);
    ImGui::Text("TSP Word     (0x%08X)", tsp.raw);
    ImGui::Text(" - SRC Alpha Instruction : %d (%s)",
                tsp.src_alpha,
                alpha_instruction_names[tsp.src_alpha]);
    ImGui::Text(" - DST Alpha Instruction : %d (%s)",
                tsp.dst_alpha,
                alpha_instruction_names[tsp.dst_alpha]);
    ImGui::Text(" - SRC / DST Select      : %d / %d", tsp.src_select, tsp.dst_select);
    ImGui::Text(
      " - Fog Control           : %d (%s)", tsp.fog_mode, fog_mode_names[tsp.fog_mode]);
    ImGui::Text(" - Color Clamp           : %d", tsp.color_clamp);
    ImGui::Text(" - Use Alpha             : %d", tsp.use_alpha);
    ImGui::Text(" - Ignore Texture Alpha  : %d", tsp.no_tex_alpha);
    ImGui::Text(
      " - Flip UV               : (U=%d, V=%d)", (tsp.flip_uv >> 1), tsp.flip_uv & 1);
    ImGui::Text(" - Clamp UV              : %d", tsp.clamp_uv);
    ImGui::Text(" - Shading Instruction   : %d (%s)",
                tsp.instruction,
                shading_instruction_names[tsp.instruction]);
    ImGui::Text("   - RGB   Equation      : %s",
                shading_instruction_equation_rgb[tsp.instruction]);
    ImGui::Text("   - Alpha Equation      : %s",
                shading_instruction_equation_a[tsp.instruction]);
  }

  // Vertex data
  {
    ImGui::Separator();

    auto show_vertex_data = [&](int i, const gpu::render::Vertex &vertex) {
      auto I = [](float v) {
        return (int)(v * 255);
      };

      ImGui::Text("Vertex %d", i);
      ImGui::Text(" - Position   : %3.0f %3.0f %3.4f",
                  vertex.position.x,
                  vertex.position.y,
                  vertex.position.z);

      ImGui::Text(" - Base Color : %02x %02x %02x %02x",
                  I(vertex.base_color.x),
                  I(vertex.base_color.y),
                  I(vertex.base_color.z),
                  I(vertex.base_color.w));
      ImGui::SameLine();
      draw_square(vertex.base_color);

      if (display_list.param_control_word.offset) {
        ImGui::Text(" - Offset   : %02x %02x %02x %02x",
                    I(vertex.offset_color.x),
                    I(vertex.offset_color.y),
                    I(vertex.offset_color.z),
                    I(vertex.offset_color.w));
        ImGui::SameLine();
        draw_square(vertex.offset_color);
      }

      if (display_list.param_control_word.texture) {
        ImGui::Text(" - UV         : %f %f", vertex.uv.x, vertex.uv.y);
      }
    };

    show_vertex_data(0, triangle.vertices[0]);
    ImGui::NewLine();
    show_vertex_data(1, triangle.vertices[1]);
    ImGui::NewLine();
    show_vertex_data(2, triangle.vertices[2]);
  }

  // If this polygon is textured...
  if (display_list.param_control_word.texture) {
    const auto &texture = m_texture_manager->get_texture_handle(display_list.texture_key);

    ImGui::Separator();

    // Show the texture itself
    const float scaled_width  = texture->width * texture_scale;
    const float scaled_height = texture->height * texture_scale;
    ImGui::Image((void *)(size_t)texture->host_texture_id,
                 ImVec2(scaled_width, scaled_height),
                 ImVec2(0, 1),
                 ImVec2(1, 0));

    // Draw a triangle wireframe that matches the triangle UVs
    auto bb_min = ImGui::GetItemRectMin();
    auto bb_max = ImGui::GetItemRectMax();

    auto get_bb_uv = [&](const Vec2f &vertex_uv) {
      // Y is inverted since we inverted the texture drawing
      ImVec2 result(vertex_uv.x, 1 - vertex_uv.y);
      result.x *= (display_list.tsp_word.flip_uv & 2) ? -1 : 1;
      result.y *= (display_list.tsp_word.flip_uv & 1) ? -1 : 1;

      result.x = bb_min.x + (bb_max.x - bb_min.x) * result.x;
      result.y = bb_min.y + (bb_max.y - bb_min.y) * result.y;

      return result;
    };

    auto draw_list  = ImGui::GetWindowDrawList();
    ImVec2 points[] = { get_bb_uv(triangle.vertices[0].uv),
                        get_bb_uv(triangle.vertices[1].uv),
                        get_bb_uv(triangle.vertices[2].uv) };
    ImU32 color     = ImColor(1.f, 0.f, 1.f);
    float thickness = 3.0f;
    draw_list->AddPolyline(points, 3, color, true, thickness);
  }
}

void
GraphicsWindow::draw_display_lists()
{
  auto console = m_director->console();
  auto &frame(console->get_last_frame_data());

  ImGui::BeginChild("TA Display Lists");
  ImGui::Separator();

  u32 last_list_number = 0xFFFFFFFF;
  u32 hovered_list     = 0xFFFFFFFF;

  u32 total_tris = 0;
  for (auto list : frame.display_lists)
    total_tris += list.triangles.size();

  ImGui::Text("TA Frame #%d (%u tris)", frame.frame_number, total_tris);

  /////////////

  if (frame.frame_number != current_frame_number) {
    current_frame_number = frame.frame_number;
    expanded_polygon_lists.clear();
  }

  for (u32 list_number = 0; list_number < frame.display_lists.size(); ++list_number) {
    gpu::render::DisplayList &display_list(frame.display_lists[list_number]);

    for (u32 triangle_num = 0; triangle_num < display_list.triangles.size();
         ++triangle_num) {
      auto &triangle = display_list.triangles[triangle_num];

      if (last_list_number != list_number) {
        // This is the start of a new list
        auto new_list_number = list_number;

        const auto pcw              = display_list.param_control_word;
        const auto pcw_type         = pcw.type == 4 ? "Poly" : "Sprite";
        // const auto list_type_string = list_type_names[pcw.list_type];

        // All sprites must be treated as translucent, despite how the list-type is
        // specified.
        const u32 n_triangles = display_list.triangles.size();

        ImGui::Text("[%c] Polygon List %3d (%6s %u, %4u tris)",
                    (expanded_polygon_lists[new_list_number] == 1) ? '-' : '+',
                    list_number,
                    pcw_type,
                    pcw.list_type, // list_type_string,
                    n_triangles);

        if (ImGui::IsItemClicked()) {
          expanded_polygon_lists[new_list_number] =
            1 - expanded_polygon_lists[new_list_number];
        }

        if (ImGui::IsItemHovered())
          hovered_list = list_number;

        ImGui::SameLine();
        static char label[256];
        snprintf(label, sizeof(label), "label_%d", list_number);
        ImGui::Checkbox(label, &display_list.debug.draw_disabled);

        last_list_number = list_number;
      }

      // If this list is hovered, we need to mark this triangle hovered.
      display_list.debug.is_hovered = list_number == hovered_list;

      /////////////////////////////////////////////
      // Display List details

      // If this row isn't expanded, continue onwards
      if (!expanded_polygon_lists[list_number])
        continue;

      const auto &pcw(display_list.param_control_word);

      // List detail string
      static char detail_string[256];
      snprintf(detail_string, sizeof(detail_string), "Polygon %6d | ", triangle_num);
      strcat(detail_string, color_names[pcw.col_type]);
      strcat(detail_string, pcw.texture ? ", Textured" : ", Non-textured");
      strcat(detail_string, pcw.gouraud ? ", Smooth-Shaded" : ", Flat-Shaded");
      if (pcw.texture)
        strcat(detail_string, pcw.uv16 ? ", Float UVs" : ", 16-bit UVs");
      strcat(detail_string, pcw.offset ? ", Uses Offset" : ", No Offset");

      ImGui::Text("%s", detail_string);
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        DrawPolygonData(display_list, triangle);
        ImGui::EndTooltip();

        display_list.debug.is_hovered = true;
      }
    }
  }

  frame.dirty = true;

  ImGui::EndChild();
}

void
GraphicsWindow::render()
{
  ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w = 0.95f;

  ImGui::SetNextWindowSize(ImVec2(1175, 600), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Graphics Debugger", NULL, ImGuiWindowFlags_NoScrollbar)) {
    ImGui::End();
    return;
  }

  enum CurrentPage
  {
    Textures,
    DisplayLists,
    Registers,
    RegionArray,
  };
  static CurrentPage current_page = Textures;

  ImGui::InputInt("max peeling", &debug_max_depth_peeling_count);

  // clang-format off
                     if (ImGui::Button("Textures"))         current_page = Textures;
  ImGui::SameLine(); if (ImGui::Button("TA Display Lists")) current_page = DisplayLists;
  ImGui::SameLine(); if (ImGui::Button("GPU Registers"))    current_page = Registers;
  ImGui::SameLine(); if (ImGui::Button("Region Array"))     current_page = RegionArray;
  // clang-format on

  switch (current_page) {
    case Textures:
      draw_texture_list();
      break;
    case DisplayLists:
      draw_display_lists();
      break;
    case Registers:
      draw_registers();
      break;
    case RegionArray:
      draw_region_array_data();
      break;
    default:
      ImGui::Text("(Page not implemented yet.)");
      break;
  }

  ImGui::End();
}
}
