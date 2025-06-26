#pragma once

#include <imgui.h>
#include "frontend/console_director.h"
#include "gui/window.h"

struct ImFont;

namespace gui {

class LoggerWindow : public Window {
public:
  LoggerWindow(std::shared_ptr<ConsoleDirector> director);

private:
  static void log_level_button(Log::LogLevel module_level, const char *name);

  void render();

  std::shared_ptr<ConsoleDirector> m_director;
};

}
