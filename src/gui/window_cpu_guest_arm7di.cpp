#include <imgui.h>

#include "guest/arm7di/arm7di_disas.h"
#include "gui/window_cpu_guest_arm7di.h"

namespace gui {

using namespace guest::arm7di;

std::string
ARM7DICPUWindowGuest::disassemble(u32 instruction, u32 pc)
{
  const Arm7DIInstructionInfo info {
    .address = pc,
    .word    = instruction,
  };
  return guest::arm7di::disassemble(info);
}

void
ARM7DICPUWindowGuest::render_registers()
{
  const ImVec4 color_active(1.0f, 1.0f, 1.0f, 1.0f);
  const ImVec4 color_inactive(1.0f, 0.80f, 0.80f, 0.4f);

#if 0
  const auto &registers = m_console->aica()->arm7di()->registers();

  const char *names[16] = { "R0", "R1", "R2",  "R3",  "R4",  "R5",  "R6", "R7",
                            "R8", "R9", "R10", "R11", "R12", "R13", "LR", "PC" };

  for (unsigned i = 0; i < 16; ++i) {
    if (i % 4 != 0) {
      ImGui::SameLine();
    }
    ImGui::Text("%-4s", names[i]);
    ImGui::SameLine();

    const u32 reg_value = registers.R[Arm7DIRegisterIndex_R0 + i];
    const auto color = reg_value != 0 ? color_active : color_inactive;
    ImGui::TextColored(color, "0x%08x", reg_value);
  }

  ImGui::Text("CPSR %08x               SPSR %08x", registers.CPSR, registers.SPSR);
#endif
}

u32
ARM7DICPUWindowGuest::get_pc() const
{
  return 0; 
  //return m_console->aica()->arm7di()->registers().R[Arm7DIRegisterIndex_PC];
}

void
ARM7DICPUWindowGuest::set_pc(u32 new_pc)
{
  //m_console->aica()->arm7di()->registers().R[Arm7DIRegisterIndex_PC] = new_pc;
}

void
ARM7DICPUWindowGuest::pause(bool new_state)
{
  // TODO : ARM7DI does not currently control execution flow
}

void
ARM7DICPUWindowGuest::step(u32 instructions)
{
  // TODO : ARM7DI does not currently control execution flow
}

void
ARM7DICPUWindowGuest::reset_system()
{
  m_director->reset_console();
}

u32
ARM7DICPUWindowGuest::fetch_instruction(u32 address)
{
  if (address > 0x8000000) {
    return 0;
  }
  return 0;
  // return m_console->aica()->arm7di()->debug_fetch_instruction(address);
}

fox::jit::Cache *const
ARM7DICPUWindowGuest::get_jit_cache()
{
  return nullptr;
  // return m_console->aica()->arm7di()->get_jit_cache();
}

} // namespace gui
