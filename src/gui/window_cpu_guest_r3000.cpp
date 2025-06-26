#include <imgui.h>

#include "guest/r3000/r3000_disas.h"
#include "gui/window_cpu_guest_r3000.h"

namespace gui {

std::string
R3000CPUWindowGuest::disassemble(u32 instruction, u32 pc)
{
  guest::r3000::Instruction ins(instruction);
  const auto [disassembly, description] =
    guest::r3000::Disassembler().disassemble(pc, ins);
  return disassembly;
}

void
R3000CPUWindowGuest::render_registers()
{
  const auto &cpu = *m_console->cpu();
  u32 const *regs = cpu.registers();

  const ImVec4 color_zero(1.0f, 1.0f, 1.0f, 0.3f);
  const ImVec4 color_nonzero(1.0f, 1.0f, 1.0f, 1.0f);

  for (u32 regi = 0; regi < 32; ++regi) {
    if (regi > 0 && regi % 4 != 0)
      ImGui::SameLine();

    auto color = regs[regi] > 0 ? color_nonzero : color_zero;
    ImGui::Text(
      "%3s/%3s ", cpu.get_register_name(regi, false), cpu.get_register_name(regi, true));
    ImGui::SameLine();
    ImGui::TextColored(color, "%08x", regs[regi]);
  }

  // ImGui::Text("", m)
}

u32
R3000CPUWindowGuest::get_pc() const
{
  return m_console->cpu()->PC();
}

void
R3000CPUWindowGuest::set_pc(u32 new_pc)
{
  // TODO
}

u32
R3000CPUWindowGuest::fetch_instruction(u32 address)
{
  return m_console->cpu()->fetch_instruction(address);
}

void
R3000CPUWindowGuest::pause(bool should_pause)
{
  if (should_pause)
    m_director->set_execution_mode(zoo::ps1::ConsoleDirector::ExecutionMode::Paused);
  else
    m_director->set_execution_mode(zoo::ps1::ConsoleDirector::ExecutionMode::Running);
}

void
R3000CPUWindowGuest::step(u32 instructions)
{
  for (u32 i = 0; i < instructions; ++i) {
    m_director->step_instruction();
  }
}

void
R3000CPUWindowGuest::reset_system()
{
  m_director->reset();
}

fox::jit::Cache *const
R3000CPUWindowGuest::get_jit_cache()
{
  return &m_console->cpu()->m_jit_cache;
}

} // namespace gui
