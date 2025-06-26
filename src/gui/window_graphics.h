#pragma once
#include "frontend/console_director.h"
#include "gpu/texture_manager.h"
#include "gui/window.h"

struct ImFont;

namespace gui {

class GraphicsWindow : public Window {
public:
  GraphicsWindow(std::shared_ptr<ConsoleDirector> director);

private:
  std::shared_ptr<ConsoleDirector> m_director;
  gpu::TextureManager *const m_texture_manager;

  std::unordered_map<int, int> expanded_polygon_lists;
  u32 current_frame_number = 0xFFFFFFFF;

  void draw_texture_list();
  void draw_display_lists();
  void draw_region_array_data();
  void draw_registers();
  void render();
  void DrawPolygonData(gpu::render::DisplayList &display_list,
                       gpu::render::Triangle &triangle);
};

}
