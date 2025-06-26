#pragma once

#include <vector>

#include "gui/window.h"
#include "shared/types.h"

namespace zoo::ps1 {
class Console;
}

namespace zoo::ps1::gui {

class HWRegisters : public ::gui::Window {
private:
  Console *m_console;

public:
  HWRegisters(Console *);
  void render() override;
};

}
