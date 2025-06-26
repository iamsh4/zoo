#pragma once

#include <vector>

#include "gui/window.h"
#include "shared/types.h"
#include "shared_data.h"

namespace zoo::ps1 {
class Console;
}

namespace zoo::ps1::gui {

class VRAM : public ::gui::Window {
private:
  Console *m_console;
  SharedData *m_shared_data;
  u32 m_vram_tex_id;

  float t = 0;

public:
  VRAM(Console *console, SharedData *shared_data, u32 vram_tex_id);
  void render() override;
};

}
