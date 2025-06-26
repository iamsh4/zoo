#pragma once

#include "frontend/console_director.h"
#include "core/console.h"
#include "gui/window.h"

struct ImFont;

namespace gui {

class CPUMMIOWindow : public Window {
public:
  CPUMMIOWindow(std::shared_ptr<ConsoleDirector> director);

private:
  void render_mmio_registers();
  void render() override;

  std::shared_ptr<ConsoleDirector> m_director;
};

}
