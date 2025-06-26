#pragma once
#include "gui/window.h"
#include "local/settings.h"

namespace gui {

class SettingsWindow : public Window {
public:
  struct SettingsEntry {
    std::string name;
    std::string key;
    std::string default_value;
  };

  SettingsWindow(std::shared_ptr<zoo::local::Settings> settings,
                 std::vector<SettingsEntry> settings_entries);

private:
  std::shared_ptr<zoo::local::Settings> m_settings;
  std::vector<SettingsEntry> m_settings_entries;

  char m_edit_buffer[4096];
  std::string m_current_edit_key;

  void render();
};

}
