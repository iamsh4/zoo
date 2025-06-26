#pragma once

#include "gui/window.h"
#include "shared/types.h"
#include "shared_data.h"

namespace zoo::ps1 {
class Console;
}

namespace zoo::ps1::gui {

class GPU : public ::gui::Window {
private:
  Console *m_console;
  SharedData* m_shared_data;

public:
  GPU(Console *, SharedData*);
  void render() override;
};

}
