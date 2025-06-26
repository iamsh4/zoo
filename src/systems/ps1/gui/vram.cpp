#include <cmath>
#include <imgui.h>

#include "systems/ps1/gui/vram.h"
#include "systems/ps1/console.h"

namespace zoo::ps1::gui {

VRAM::VRAM(Console *console, SharedData *shared_data, u32 vram_tex_id)
  : ::gui::Window("VRAM"),
    m_console(console),
    m_shared_data(shared_data),
    m_vram_tex_id(vram_tex_id)
{
}

void
VRAM::render()
{
  ImGui::Begin("VRAM");
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::Image((void *)(size_t)m_vram_tex_id, ImVec2 { 1024, 512 });

  ImVec2 mouse_pos(-1, -1);
  if (ImGui::IsItemHovered()) {
    mouse_pos = ImGui::GetMousePos();
  }

  u32 coord_color = 0x00ff00ff;

  // pulse alpha over time
  t += 0.1;
  t = fmodf(t, 2 * M_PI);
  const float q = (1.f + sinf(t)) * 0.5f;
  coord_color |= u32(q * 0xff) << 24;

  //
  std::vector<VRAMCoord> coords;
  m_shared_data->get_vram_coords(&coords);
  if (!coords.empty()) {
    auto draw_list = ImGui::GetWindowDrawList();
    if (coords.size() >= 3) {
      draw_list->AddTriangleFilled({ float(p.x + coords[0].x), float(p.y + coords[0].y) },
                                   { float(p.x + coords[1].x), float(p.y + coords[1].y) },
                                   { float(p.x + coords[2].x), float(p.y + coords[2].y) },
                                   coord_color);
    }
    if (coords.size() >= 4) {
      draw_list->AddTriangleFilled({ float(p.x + coords[1].x), float(p.y + coords[1].y) },
                                   { float(p.x + coords[2].x), float(p.y + coords[2].y) },
                                   { float(p.x + coords[3].x), float(p.y + coords[3].y) },
                                   coord_color);
    }
  }

  if (mouse_pos.x >= 0) {
    ImGui::Text("Cursor: x=%u y=%u", u32(mouse_pos.x - p.x), u32(mouse_pos.y - p.y));
  }

  ImGui::End();
}

}
