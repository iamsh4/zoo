#pragma once

#include <vector>

#include "gui/window.h"
#include "shared/types.h"

namespace zoo::ps1 {
class Console;
}

namespace zoo::ps1::gui {

class Screen : public ::gui::Window {
private:
  Console *m_console;
  u32 m_vram_tex_id;

public:
  Screen(Console *console, u32 vram_tex_id);
  void render() override;
};

}
