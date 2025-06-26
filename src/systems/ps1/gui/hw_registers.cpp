#include <imgui.h>
#include "systems/ps1/gui/hw_registers.h"
#include "systems/ps1/console.h"

namespace zoo::ps1::gui {

std::vector<char> message_buffer;

HWRegisters::HWRegisters(Console *console)
  : ::gui::Window("MMIO Registers"),
    m_console(console)
{
  message_buffer.resize(512);
}

void
HWRegisters::render()
{
  ImGui::Begin("MMIO Registers");
  for (auto &reg : m_console->mmio_registry()->m_registers) {

    u32 val = 0;
    memcpy(&val, reg.host_ptr, reg.size);

    if (reg.size == 1) {
      ImGui::Text("%-15s %-20s 0x%02x", reg.category, reg.name, val);
    }
    if (reg.size == 2) {
      ImGui::Text("%-15s %-20s 0x%04x", reg.category, reg.name, val);
    }
    if (reg.size == 4) {
      ImGui::Text("%-15s %-20s 0x%08x", reg.category, reg.name, val);
    }

    if (reg.message && reg.message(&message_buffer)) {
      ImGui::SameLine();
      ImGui::Text("%s", message_buffer.data());
    }
  }
  ImGui::End();
}

}
