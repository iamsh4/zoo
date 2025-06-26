#pragma once

#include "frontend/console_director.h"
#include "core/console.h"
#include "gui/window.h"

struct ImFont;

namespace gui {

class AudioWindow : public Window {
public:
  AudioWindow(std::shared_ptr<ConsoleDirector> director);

private:
  void render_internal();
  void render() override;

  std::shared_ptr<ConsoleDirector> m_director;
};

}
