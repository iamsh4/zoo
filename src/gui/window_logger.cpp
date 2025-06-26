#include <imgui.h>

#include "gui/window_logger.h"

namespace gui {

struct ImGuiLogModuleButton {
  const char *module_name;
  ImVec4 color;
  Log::LogModule log_module;
};

struct LogLevelData {
  const char *name;
  ImVec4 color;
};

static const ImGuiLogModuleButton LOG_MODULE_BUTTONS[] = {
  { "CPU", ImVec4(0.70f, 0.40f, 0.10f, 1), Log::LogModule::SH4 },
  { "GDROM", ImVec4(0.10f, 0.60f, 0.10f, 1), Log::LogModule::GDROM },
  { "MAPLE", ImVec4(0.15f, 0.70f, 0.50f, 1), Log::LogModule::MAPLE },
  { "GRAPHICS", ImVec4(0.10f, 0.10f, 0.90f, 1), Log::LogModule::GRAPHICS },
  { "G2", ImVec4(0.00f, 0.30f, 0.80f, 1), Log::LogModule::G2 },
  { "GUI", ImVec4(0.30f, 0.40f, 0.10f, 1), Log::LogModule::GUI },
  { "AUDIO", ImVec4(0.60f, 0.10f, 0.10f, 1), Log::LogModule::AUDIO },
  { "MODEM", ImVec4(0.10f, 0.10f, 0.60f, 1), Log::LogModule::MODEM },
  { "HOLLY", ImVec4(0.70f, 0.10f, 0.60f, 1), Log::LogModule::HOLLY },
  { "MEMTABLE", ImVec4(0.45f, 0.00f, 0.25f, 1), Log::LogModule::MEMTABLE },
  { "PENGUIN", ImVec4(0.30f, 0.30f, 0.30f, 1), Log::LogModule::PENGUIN },
};

static const LogLevelData LOG_LEVEL_DATA[] = {
  { "None", ImVec4(0.50, 0.50, 0.50, 1) }, { "EROR", ImVec4(0.80, 0.00, 0.00, 1) },
  { "WARN", ImVec4(0.80, 0.80, 0.00, 1) }, { "INFO", ImVec4(0.25, 0.25, 1.00, 1) },
  { "DEBG", ImVec4(0.25, 1.00, 0.25, 1) }, { "VERB", ImVec4(0.30, 0.30, 0.30, 1) }
};

LoggerWindow::LoggerWindow(std::shared_ptr<ConsoleDirector> director)
  : Window("Logs"),
    m_director(director)
{
  return;
}

void
LoggerWindow::log_level_button(Log::LogLevel module_level, const char *level_name)
{
  const bool is_level_exposed = Log::level >= module_level;
  const ImVec4 color =
    is_level_exposed ? ImVec4(.4f, .4f, .4f, 1) : ImVec4(.1f, .1f, .1f, 1);

  ImGui::PushStyleColor(ImGuiCol_Button, color);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, color);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color);

  if (ImGui::Button(level_name)) {
    Log::level = module_level;
  }

  ImGui::PopStyleColor(3);
}

void
LoggerWindow::render()
{
  ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w = 0.95f;

  ImGui::SetNextWindowSize(ImVec2(1175, 600), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Logger", NULL, ImGuiWindowFlags_NoScrollbar)) {
    ImGui::End();
    return;
  }

  ImGui::BeginGroup();
  const auto n_module_buttons = sizeof(LOG_MODULE_BUTTONS) / sizeof(ImGuiLogModuleButton);
  for (size_t i = 0; i < n_module_buttons; ++i) {
    static const ImVec4 disabled_color(0.05f, 0.05f, 0.05f, 1);
    const bool module_is_enabled =
      Log::is_module_enabled(LOG_MODULE_BUTTONS[i].log_module);
    const ImVec4 current_color =
      module_is_enabled ? LOG_MODULE_BUTTONS[i].color : disabled_color;

    ImGui::PushStyleColor(ImGuiCol_Button, current_color);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, current_color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, current_color);

    if (ImGui::Button(LOG_MODULE_BUTTONS[i].module_name)) {
      if (ImGui::GetIO().KeyShift) {
        Log::module_hide_all();
        Log::module_show(LOG_MODULE_BUTTONS[i].log_module);
      } else if (ImGui::GetIO().KeyCtrl) {
        Log::module_show_all();
      } else if (module_is_enabled) {
        Log::module_hide(LOG_MODULE_BUTTONS[i].log_module);
      } else {
        Log::module_show(LOG_MODULE_BUTTONS[i].log_module);
      }
    }

    ImGui::SameLine();
    ImGui::PopStyleColor(3);
  }

  ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f),
                     "(Solo: [Shift], Enable All: [Ctrl])");
  ImGui::EndGroup();

  ImGui::BeginGroup();
  log_level_button(Log::LogLevel::Verbose, "Verbose");
  ImGui::SameLine();
  log_level_button(Log::LogLevel::Debug, "Debug");
  ImGui::SameLine();
  log_level_button(Log::LogLevel::Info, "Info");
  ImGui::SameLine();
  log_level_button(Log::LogLevel::Warn, "Warn");
  ImGui::SameLine();
  log_level_button(Log::LogLevel::Error, "Error");
  ImGui::SameLine();
  ImGui::EndGroup();

  ImGui::Separator();

  /* Command history and shortcuts */
  {
    static const ImVec4 color_active(0.9f, 0.9f, 0.9f, 1.0f);
    static const ImVec4 color_hit(0.7f, 1.0f, 0.7f, 1.0f);

    ImGui::BeginChild("LoggerArea", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));

    const u32 n_entries = Log::get_current_entry_count();
    for (u32 i = 0; i < n_entries; ++i) {
      Log::LogEntry entry = Log::get_nth_entry(i);
      if (entry.level <= Log::level && Log::is_module_enabled(entry.module)) {
        const char *const module_name = LOG_MODULE_BUTTONS[entry.module].module_name;
        const char *const level_name = LOG_LEVEL_DATA[entry.level].name;
        const ImVec4 &module_color(LOG_MODULE_BUTTONS[entry.module].color);
        const ImVec4 &level_color(LOG_LEVEL_DATA[entry.level].color);

        ImGui::TextColored(module_color, "%8s ", module_name);
        ImGui::SameLine();
        ImGui::TextColored(level_color, "%4s", level_name);
        ImGui::SameLine();
        ImGui::Text("%s", entry.message);
      }
    }
    ImGui::EndChild();
  }

  ImGui::End();
}

}
