#include <imgui.h>
#include "systems/ps1/gui/screen.h"
#include "systems/ps1/console.h"

namespace zoo::ps1::gui {

Screen::Screen(Console *console, u32 vram_tex_id)
  : ::gui::Window("Screen"),
    m_console(console),
    m_vram_tex_id(vram_tex_id)
{
}

void
Screen::render()
{
  ImGui::Begin("Screen");

  u32 tl_x, tl_y, br_x, br_y;
  m_console->gpu()->get_display_vram_bounds(&tl_x, &tl_y, &br_x, &br_y);

  // const u32 width = br_x - tl_x;
  // const u32 height = br_y - tl_y;

  const float display_width = 800;
  const float display_height = 800 * 3 / 4;

  ImVec2 uv_tl {
    float(tl_x) / 1024.0f,
    float(tl_y) / 512.0f,
  };

  ImVec2 uv_br {
    float(br_x) / 1024.0f,
    float(br_y) / 512.0f,
  };

  ImGui::Image((void *)(size_t)m_vram_tex_id,
               ImVec2 { display_width, display_height },
               uv_tl,
               uv_br);

  ImGui::End();
}

}
