#include <algorithm>
#include <imgui.h>

#if defined(ZOO_OS_MACOS)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include "local/settings.h"
#include "gui/window_settings.h"

namespace gui {

SettingsWindow::SettingsWindow(std::shared_ptr<zoo::local::Settings> settings,
                               std::vector<SettingsEntry> settings_entries)
  : Window("Settings"),
    m_settings(settings),
    m_settings_entries(settings_entries)
{
}

void
SettingsWindow::render()
{
  if (!m_settings) {
    return;
  }

  if (!ImGui::Begin("Settings")) {
    ImGui::End();
    return;
  }

  ImGui::Text("Settings file @ %s/%s",
              m_settings->settings_root_dir().c_str(),
              m_settings->settings_filename().c_str());

  ImGui::Separator();

  ImGui::BeginTable("SettingsTable", 2);
  ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthFixed);
  ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

  for (const auto &entry : m_settings_entries) {
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    ImGui::Text("%s", entry.name.c_str());

    ImGui::TableSetColumnIndex(1);
    if (entry.key == m_current_edit_key) {
      if (ImGui::InputText("##settings_input",
                           m_edit_buffer,
                           sizeof(m_edit_buffer),
                           ImGuiInputTextFlags_EnterReturnsTrue)) {
        printf("Returned true, buffer is '%s'\n", m_edit_buffer);
        m_settings->set(entry.key, m_edit_buffer);
        m_current_edit_key = "";
      }
    } else {
      const bool is_default = !m_settings->has(entry.key);

      const char *data =
        m_settings->get_or_default(entry.key, entry.default_value).data();

      if (is_default) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4 { .5, .5, .5, 1 });
        ImGui::Text(data);
        ImGui::PopStyleColor();
      } else {
        ImGui::Text(data);
      }

      if (ImGui::IsItemClicked()) {
        strcpy(m_edit_buffer, data);
        m_current_edit_key = entry.key;
      }
    }
  }

  ImGui::EndTable();

  ImGui::End();
}

}
