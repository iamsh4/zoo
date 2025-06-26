#pragma once

#include <vector>

#include "gui/window.h"
#include "systems/dragon/hw/gpu.h"
#include "shared/types.h"

namespace zoo::dragon {
class Console;
}

namespace zoo::dragon::gui {

struct CommandTreeNode {
  u32 command;
  u32 value;

  // maybe more stuff here
  std::string name;
  bool is_debug = false;
  u8 debug_word = 0;

  u32 level = 0;
};

class GPU : public ::gui::Window {
private:
  Console *m_console;

  dragon::GPU::CommandList m_command_list;
  std::vector<CommandTreeNode> m_tree_nodes;

  void process_command_list();

  void render_raw_command_list();
  void render_pretty_command_list();

public:
  GPU(Console *console);
  void render() override;
};

}
