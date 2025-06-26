#pragma once

#include <functional>
#include <thread>

#include "frontend/console_director.h"
#include "gui/window.h"
#include "local/settings.h"
#include "local/game_library.h"

namespace gui {

class PenguinGameLibWindow : public Window {
public:
  using LaunchCallback = std::function<void(const std::string disc_path)>;

  PenguinGameLibWindow(std::shared_ptr<zoo::local::Settings> settings,
                       std::shared_ptr<zoo::local::GameLibrary> game_library,
                       LaunchCallback launch_callback
                       );

private:
  std::shared_ptr<zoo::local::Settings> m_settings;
  std::shared_ptr<zoo::local::GameLibrary> m_game_library;
  
  LaunchCallback m_launch_callback;

  std::thread m_scanner_thread;

  i32 m_scan_current_count;
  i32 m_scan_total_count;
  std::string m_scan_latest_path;

  void render();
};

}
