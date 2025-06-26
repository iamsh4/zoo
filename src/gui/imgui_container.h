#pragma once

#include <memory>

#include "frontend/console_director.h"
#include "gui/window_audio.h"
#include "gui/window_cpu.h"
#include "gui/window_cpu_mmio.h"
#include "gui/window_jit_cache.h"
#include "gui/window_jit_workbench.h"
#include "gui/window_logger.h"
#include "gui/window_graphics.h"
#include "gui/window_memeditor.h"
#include "peripherals/vmu.h"

namespace gui {

// Notes
// - Holds gui::Windows
// - draws the ImGui dockspace and any enabled windows.
class ImGuiContainer final {
public:
  void addWindow(std::shared_ptr<gui::Window> window)
  {
    m_windows.push_back(window);
  }
  void draw(bool draw_windows);

private:
  std::vector<std::shared_ptr<gui::Window>> m_windows;
};

}
