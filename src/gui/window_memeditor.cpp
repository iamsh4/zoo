#include <algorithm>
#include <imgui.h>

#include "fox/memtable.h"
#include "gui/window_memeditor.h"

namespace gui {

MemoryEditor::MemoryEditor(fox::MemoryTable *memory_table)
  : Window("Memory Editor"),
    m_viewer(new MemoryTableWidget(memory_table, 0x00000000u, 0xa0000000u))
{
  return;
}

void
to_hex(u32 val, u32 n_bits, char *buff, u32 buff_len)
{
  static const char *formats[] = { "0x%02X", "0x%04X", "0x%06X", "0x%08X" };
  snprintf(buff, buff_len, formats[n_bits / 8], val);
}

std::vector<u32>
find_addresses_matching_sequence(const u8 *data,
                                 u32 start,
                                 u32 end,
                                 const std::vector<u8> &search,
                                 u32 max_results = 20)
{
  std::vector<u32> results;
  for (size_t i = start; i < end; ++i) {
    bool match = true;
    for (size_t j = 0; j < search.size() && i + j < end; ++j) {
      if (data[i + j] != search[j]) {
        match = false;
        break;
      }
    }
    if (match) {
      results.push_back(i);
    }
  }
  return results;
}

std::vector<u8>
hex_string_to_bytes(char *string)
{
  std::vector<u8> bytes;
  const unsigned len = strlen(string);
  for (unsigned i = 0; i < len; i += 2) {
    char byte_str[3] = { string[i], string[i + 1], 0 };
    u32 byte;
    sscanf(byte_str, "%02X", &byte);
    bytes.push_back(byte);
  }
  return bytes;
}

void
render_search_tool(Console *console)
{
  static char search_term[256];
  if (ImGui::InputText("search",
                       search_term,
                       sizeof(search_term),
                       ImGuiInputTextFlags_CharsHexadecimal |
                         ImGuiInputTextFlags_EnterReturnsTrue)) {

    if (strlen(search_term) % 2 == 1) {
      printf("Search term must be multiple-of-two sized string");
    } else {
      auto bytes = hex_string_to_bytes(search_term);

      const u8 *const mem_root = console->memory()->root();
      printf("Searching for '%s'\n", search_term);
      auto locations = find_addresses_matching_sequence(
        mem_root, 0x0c000000, 0x0c000000 + 0x1000000, bytes);
      for (auto location : locations) {
        printf("Found match @ 0x%08x\n", location);
      }
    }
  }
}

void
MemoryEditor::render()
{
  if (!ImGui::Begin("Global Memory Editor")) {
    ImGui::End();
    return;
  }

  m_viewer->render();
  ImGui::End();
}

}
