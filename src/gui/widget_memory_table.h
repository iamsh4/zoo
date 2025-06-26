#pragma once

#include <imgui.h>
#include "fox/memtable.h"
#include "gui/widget.h"

namespace gui {

/*!
 * @class gui::MemoryTableWidget
 * @brief Widget for inspecting and interacting with MemoryTable contents.
 */
class MemoryTableWidget final : public Widget {
public:
  MemoryTableWidget(fox::MemoryTable *memory, u32 address_start, u32 address_end);
  ~MemoryTableWidget();

  void add_named_section(const char *name, u32 start, u32 end);

  void render() override final;

private:
  fox::MemoryTable *const m_memory;
  const u32 m_address_start;
  const u32 m_address_end;
  i64 m_hover_address;
  i64 m_edit_address;
  bool m_edit_refocus;
  char m_data_input[4];
  char m_address_input[32];

  struct Section {
    std::string name;
    u32 start;
    u32 end;
    ImVec4 color;
  };
  std::vector<Section> m_sections;
};


}
