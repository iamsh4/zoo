#include <imgui.h>

#include "guest/rv32/rv32_ir.h"
#include "gui/window_cpu_guest_dragon.h"

namespace gui {

DragonCPUWindowGuest::DragonCPUWindowGuest(zoo::dragon::ConsoleDirector *director)
  : m_director(director),
    m_console(director->console())
{
  instruction_sets.push_back(new guest::rv32::RV32I());
}

std::string
DragonCPUWindowGuest::disassemble(u32 instruction, u32 pc)
{
  using namespace guest::rv32;
  for (auto &isa : instruction_sets) {
    const Decoding decoded = isa->decode(Encoding { .raw = instruction, .pc = pc });
    if (decoded.valid()) {
      return isa->disassemble(decoded);
    }
  }

  return "???";
}

const char *
register_name(unsigned index)
{
  static const char *names[] = {
    "x0",  "x1",  "x2",  "x3",  "x4",  "x5",  "x6",  "x7",  "x8",  "x9",  "x10",
    "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x18", "x19", "x20", "x21",
    "x22", "x23", "x24", "x25", "x26", "x27", "x28", "x29", "x30", "x31", "pc",
  };

  if (index < 33)
    return names[index];
  else
    return "???";
}

void
DragonCPUWindowGuest::render_registers()
{
  using namespace guest::rv32;
  const u32 *const regs = (const u32 *)m_console->cpu()->registers();

  const ImVec4 color_zero(1.0f, 1.0f, 1.0f, 0.3f);
  const ImVec4 color_nonzero(1.0f, 1.0f, 1.0f, 1.0f);

  for (u32 regi = 0; regi < Registers::__NUM_REGISTERS; ++regi) {
    if (regi > 0 && regi % 4 != 0)
      ImGui::SameLine();

    auto color = regs[regi] > 0 ? color_nonzero : color_zero;
    ImGui::Text("%3s ", register_name(regi));
    ImGui::SameLine();
    ImGui::TextColored(color, "%08x", regs[Registers::REG_X_START + regi]);
  }
}

u32
DragonCPUWindowGuest::get_pc() const
{
  return m_console->cpu()->registers()[guest::rv32::Registers::REG_PC];
}

void
DragonCPUWindowGuest::set_pc(u32 new_pc)
{
  // TODO
}

u32
DragonCPUWindowGuest::fetch_instruction(u32 address)
{
  if (address >= 0x8000000) {
    return m_console->memory()->read<u32>(address);
  }
  return 0;
}

void
DragonCPUWindowGuest::pause(bool should_pause)
{
  if (should_pause)
    m_director->set_execution_mode(zoo::dragon::ConsoleDirector::ExecutionMode::Paused);
  else
    m_director->set_execution_mode(zoo::dragon::ConsoleDirector::ExecutionMode::Running);
}

void
DragonCPUWindowGuest::step(u32 instructions)
{
  for (u32 i = 0; i < instructions; ++i) {
    m_director->step_instruction();
  }
}

void
DragonCPUWindowGuest::reset_system()
{
  m_director->reset();
}

fox::jit::Cache *const
DragonCPUWindowGuest::get_jit_cache()
{
  // return &m_console->cpu()->m_jit_cache;
  return nullptr;
}

} // namespace gui
