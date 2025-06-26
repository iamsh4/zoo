#include <algorithm>
#include <imgui.h>

#if defined(ZOO_OS_MACOS)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include "gui/window_penguin_gamelib.h"

namespace gui {

std::string
get_gamelib_filepath(const zoo::local::Settings &settings)
{
  std::string path = settings.settings_root_dir() + "/";
  std::string filename =
    settings.get_or_default("dreamcast.gamelib.db_file_name", "dreamcast.gamelib").data();
  return path + filename;
}

PenguinGameLibWindow::PenguinGameLibWindow(
  std::shared_ptr<zoo::local::Settings> settings,
  std::shared_ptr<zoo::local::GameLibrary> gamelib,
  LaunchCallback launch_callback)
  : Window("Game Library"),
    m_settings(settings),
    m_game_library(gamelib),
    m_launch_callback(launch_callback)
{
  m_scan_current_count = -1;
  m_scan_total_count = -1;

  // Load game lib
  const std::string db_path = get_gamelib_filepath(*m_settings);
  m_game_library->load(db_path);
}

void
PenguinGameLibWindow::render()
{
  if (!ImGui::Begin("Game Library")) {
    ImGui::End();
    return;
  }

  if (!m_game_library) {
    return;
  }

  if (!m_scanner_thread.joinable() && ImGui::Button("Re-Scan Game Directory")) {
    m_scan_current_count = 0;
    m_scanner_thread = std::thread([&] {
      zoo::local::GameLibrary::ScanSettings scan_settings {
        .recursive = true,
        .only_modified = false,
        .extensions = { ".chd", ".gdi" },
      };

      m_game_library->scan(
        m_settings->get_or_default("dreamcast.gamelib.scandir", "/tmp"),
        scan_settings,
        [&](const zoo::local::GameLibraryEntry &latest, u32 completed, u32 total) {
          printf("%d/%d\n", completed, total);
          m_scan_current_count = completed;
          m_scan_total_count = total;
          m_scan_latest_path = latest.file_path;
        });
    });
  }

  if (m_scan_current_count == m_scan_total_count && m_scanner_thread.joinable()) {
    // Scan happened and is done!
    m_scanner_thread.join();
    m_scan_current_count = -1;
    m_scan_total_count = -1;

    // Save DB
    const std::string db_path = get_gamelib_filepath(*m_settings);
    m_game_library->save(db_path);
  }

  if (m_scan_current_count > 0 && !m_scanner_thread.joinable()) {
    ImGui::Text("Scan Complete!");
  } else if (m_scan_current_count >= 0) {
    ImGui::Text("Scanning Game Directory! (%d/%d)\n%s",
                m_scan_current_count,
                m_scan_total_count,
                m_scan_latest_path.c_str());
  }

  static char search[1024];
  if (ImGui::InputText("##SearchFilter", search, 1023)) {
    // Search is updated. TODO: only filter the game list now.
  }

  const bool has_search_filter = strlen(search) > 0;
  if (m_scan_current_count == -1) {
    ImGui::BeginTable("GameList", 3);

    ImGui::TableSetupColumn("##PlayButton", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch);

    for (const auto &entry : m_game_library->data()) {
      if (has_search_filter && !strcasestr(entry.file_path.c_str(), search)) {
        continue;
      }

      ImGui::TableNextRow();

      ImGui::TableSetColumnIndex(0);
      ImGui::PushID(entry.media_id);
      if (ImGui::Button("Launch") && m_launch_callback) {
        m_launch_callback(entry.file_path);
      }
      ImGui::PopID();

      ImGui::TableSetColumnIndex(1);
      ImGui::Text("%d MiB", entry.file_size / 1024 / 1024);

      ImGui::TableSetColumnIndex(2);
      ImGui::Text("%s", entry.file_path.c_str());
    }

    ImGui::EndTable();
  }

  ImGui::End();
}

}
