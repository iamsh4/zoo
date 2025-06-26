#include <imgui.h>
#include "imgui_container.h"

namespace gui {

void
ImGuiContainer::draw(bool draw_windows)
{
  static bool opt_fullscreen_persistant = true;
  bool opt_fullscreen = opt_fullscreen_persistant;
  static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode;

  // We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not
  // dockable into, because it would be confusing to have two docking targets within each
  // others.
  ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
  if (opt_fullscreen) {
    ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetWorkPos());
    ImGui::SetNextWindowSize(viewport->GetWorkSize());
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
  }

  // When using ImGuiDockNodeFlags_PassthruCentralNode, draw_dock() will render our
  // background and handle the pass-thru hole, so we ask Begin() to not render a
  // background.
  if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
    window_flags |= ImGuiWindowFlags_NoBackground;

  // Important: note that we proceed even if Begin() returns false (aka window is
  // collapsed). This is because we want to keep our draw_dock() active. If a draw_dock()
  // is inactive, all active windows docked into it will lose their parent and become
  // undocked. We cannot preserve the docking relationship between an active window and an
  // inactive docking, otherwise any change of dockspace/settings would lead to windows
  // being stuck in limbo and never being visible.
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  static bool p_open = true;
  ImGui::Begin("DockSpace", &p_open, window_flags);
  ImGui::PopStyleVar();

  if (opt_fullscreen)
    ImGui::PopStyleVar(2);

  // DockSpace
  ImGuiIO &io = ImGui::GetIO();
  if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable) {
    ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
  }

  if (ImGui::BeginMenuBar()) {
    if (ImGui::BeginMenu("Windows")) {
      // Disabling fullscreen would allow the window to be moved to the front of other
      // windows, which we can't undo at the moment without finer window depth/z control.

      for (const auto &window : m_windows) {
        if (ImGui::MenuItem(window->name().c_str(), "", window->is_visible())) {
          window->toggle_visible();
        }
      }

      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Options")) {
      // if (ImGui::MenuItem("VSync Limit", "", vsync_limit)) {
      //   vsync_limit = !vsync_limit;
      // }

      // TODO : Add support for options

      ImGui::EndMenu();
    }

    ImGui::Text("(F5 to toggle all debugging)");

    ImGui::EndMenuBar();
  }

  ImGui::End();

  // Draw all windows which are visible
  if (draw_windows) {
    for (const auto &window : m_windows) {
      window->draw();
    }
  }
}

#if 0
void
ImGuiContainer::draw_vmus()
{
  // TODO : This code probably works. Found #if 0'd. I think controllers just needs to be
  // piped in to bring this back to life.
#if 0
  for (unsigned port = 0; port < 4; ++port) {
    if (!controllers[port]) {
      continue;
    }

    const std::shared_ptr<maple::Device> slot0 = controllers[port]->get_device(0u);
    if (!slot0) {
      continue;
    }

    const maple::VMU *const vmu = dynamic_cast<maple::VMU *>(slot0.get());
    if (vmu != nullptr) {
      draw_vmu_window(port, vmu);
    }
  }
#endif
}

void
ImGuiContainer::draw_vmu_window(const unsigned port, const maple::VMU *const vmu)
{
#if 1
  const float screen_offset = 20.0f;
  const float pixel_size = 3.0f;
  const float render_width = maple::VMU::LCD_WIDTH * pixel_size;
  const float render_height = maple::VMU::LCD_HEIGHT * pixel_size;

  ImGuiViewport *const viewport = ImGui::GetMainViewport();
  const ImVec2 viewport_size = viewport->GetWorkSize();
  ImGui::SetNextWindowPos(
    { /* XXX X == 0 seems to actually start a few pixels from the left... */
      screen_offset +
        (viewport_size.x - screen_offset * 3.0f - render_width) / 3.0f * port,
      viewport_size.y - render_height - screen_offset });

  char name[32];
  snprintf(name, sizeof(name), "VMU%u", port);

  const ImGuiWindowFlags window_flags =
    ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
    ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
    ImGuiWindowFlags_NoBackground;
  ImGui::Begin(name, nullptr, window_flags);

  ImVec2 window_topleft = ImGui::GetCursorScreenPos();
  {
    auto *draw_list = ImGui::GetWindowDrawList();
    for (unsigned y = 0; y < maple::VMU::LCD_HEIGHT; ++y) {
      for (unsigned x = 0; x < maple::VMU::LCD_WIDTH; ++x) {
        float lcd_level = vmu->lcd_pixels()[y * maple::VMU::LCD_WIDTH + x];
        lcd_level = 0.25 + 0.5 * lcd_level;

        const ImU32 im_color =
          ImGui::GetColorU32(ImVec4(lcd_level, lcd_level, lcd_level, 0.7f));
        const ImVec2 p2 =
          ImVec2(window_topleft.x + x * pixel_size, window_topleft.y + y * pixel_size);
        const ImVec2 p3 = ImVec2(p2.x + pixel_size, p2.y + pixel_size);
        draw_list->AddRectFilled(p2, p3, im_color);
      }
    }

    ImGui::InvisibleButton("",
                           ImVec2(pixel_size * (maple::VMU::LCD_WIDTH - 1),
                                  pixel_size * (maple::VMU::LCD_HEIGHT - 2)));
  }

  ImGui::End();
#endif
}

#endif

}
