#pragma once

#include "gui/widget_memory_table.h"
#include "gui/window.h"

namespace gui {

class MemoryEditor : public Window {
public:
  MemoryEditor(fox::MemoryTable *mem_table);

  void add_named_section(const char *name, u32 start, u32 end)
  {
    m_viewer->add_named_section(name, start, end);
  }

private:
  std::unique_ptr<MemoryTableWidget> m_viewer;

  void render() override;
};

}
