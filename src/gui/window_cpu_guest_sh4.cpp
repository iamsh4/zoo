#include <imgui.h>

#include "guest/sh4/sh4_debug.h"
#include "gui/window_cpu_guest_sh4.h"

namespace gui {

std::string
SH4CPUWindowGuest::disassemble(u32 instruction, u32 pc)
{
  return cpu::Debugger::disassemble(instruction, pc);
}

void
SH4CPUWindowGuest::render_registers()
{
  const ImVec4 color_active(1.0f, 1.0f, 1.0f, 1.0f);
  const ImVec4 color_inactive(1.0f, 0.80f, 0.80f, 0.4f);
  cpu::SH4::Registers regs = m_console->cpu()->registers();
  cpu::SH4::FPUState fpu   = m_console->cpu()->fpu_registers();

  auto colorize = [&color_active, &color_inactive](const bool active) {
    return active ? color_active : color_inactive;
  };

  /* Status Register / Saved Status Register + Misc. Registers */
  {
    ImGui::Text("SR:    ");
    ImGui::SameLine();
    ImGui::TextColored(colorize(regs.SR.T), "[T]");
    ImGui::SameLine();
    ImGui::TextColored(colorize(regs.SR.S), "[S]");
    ImGui::SameLine();
    ImGui::TextColored(colorize(regs.SR.Q), "[Q]");
    ImGui::SameLine();
    ImGui::TextColored(colorize(regs.SR.M), "[M]");
    ImGui::SameLine();
    ImGui::TextColored(colorize(regs.SR.FD), "[FD]");
    ImGui::SameLine();
    ImGui::TextColored(colorize(regs.SR.BL), "[BL]");
    ImGui::SameLine();
    ImGui::TextColored(colorize(regs.SR.RB), "[RB]");
    ImGui::SameLine();
    ImGui::TextColored(colorize(regs.SR.MD), "[MD]");

    ImGui::SameLine();
    ImGui::Text("             GBR: %08x             PR: %08x            SPC: %08x",
                regs.GBR,
                regs.PR,
                regs.SPC);

    ImGui::Text("SSR:   ");
    ImGui::SameLine();
    ImGui::TextColored(colorize(regs.SSR.T), "[T]");
    ImGui::SameLine();
    ImGui::TextColored(colorize(regs.SSR.S), "[S]");
    ImGui::SameLine();
    ImGui::TextColored(colorize(regs.SSR.Q), "[Q]");
    ImGui::SameLine();
    ImGui::TextColored(colorize(regs.SSR.M), "[M]");
    ImGui::SameLine();
    ImGui::TextColored(colorize(regs.SSR.FD), "[FD]");
    ImGui::SameLine();
    ImGui::TextColored(colorize(regs.SSR.BL), "[BL]");
    ImGui::SameLine();
    ImGui::TextColored(colorize(regs.SSR.RB), "[RB]");
    ImGui::SameLine();
    ImGui::TextColored(colorize(regs.SSR.MD), "[MD]");

    ImGui::SameLine();
    ImGui::Text("             VBR: %08x            SPR: %08x            SGR: %08x",
                regs.VBR,
                regs.SPR,
                regs.SGR);
  }

  /* Register Column Legends */
  ImGui::Text("                ");
  for (unsigned i = 0; i < 8; ++i) {
    ImGui::SameLine();
    ImGui::Text("           %d", i);
  }

  /* General Purpose Registers (Bank0+1) */
  {
    ImGui::Text("GPR");

    ImGui::Text("    B0 R00:R07  ");
    const ImVec4 b0_color    = regs.SR.RB ? color_inactive : color_active;
    const unsigned b0_offset = regs.SR.RB ? 16 : 0;
    for (unsigned i = 0; i < 8; ++i) {
      ImGui::SameLine();
      ImGui::TextColored(b0_color, "    %08x", regs.general_registers[i + b0_offset]);
    }

    ImGui::Text("    B1 R00:R07  ");
    const ImVec4 b1_color    = regs.SR.RB ? color_active : color_inactive;
    const unsigned b1_offset = regs.SR.RB ? 0 : 16;
    for (unsigned i = 0; i < 8; ++i) {
      ImGui::SameLine();
      ImGui::TextColored(b1_color, "    %08x", regs.general_registers[i + b1_offset]);
    }

    ImGui::Text("       R08:R15  ");
    for (unsigned i = 0; i < 8; ++i) {
      ImGui::SameLine();
      ImGui::Text("    %08x", regs.general_registers[i + 8]);
    }
  }

  /* Floating point config register */
  ImGui::TextUnformatted("");
  ImGui::Text("FPSCR:  ");
  ImGui::SameLine();
  ImGui::TextColored(colorize(fpu.FPSCR.RM0), "[RM0]");
  ImGui::SameLine();
  ImGui::TextColored(colorize(fpu.FPSCR.RM1), "[RM1]");
  ImGui::SameLine();
  ImGui::TextColored(colorize(fpu.FPSCR.DN), "[DN]");
  ImGui::SameLine();
  ImGui::TextColored(colorize(fpu.FPSCR.PR), "[PR]");
  ImGui::SameLine();
  ImGui::TextColored(colorize(fpu.FPSCR.SZ), "[SZ]");
  ImGui::SameLine();
  ImGui::TextColored(colorize(fpu.FPSCR.FR), "[FR]");

  /* Special FPU registers */
  ImGui::SameLine();
  ImGui::Text(
    "                 FPUL: %08x / %0.3f", fpu.FPUL, reinterpret<float>(fpu.FPUL));

  /* Floating Point Registers */
  {
    ImGui::Text("FPU");

    const ImVec4 b0_color = fpu.FPSCR.FR ? color_inactive : color_active;
    ImGui::Text("    B0 SP00:SP07");
    for (unsigned i = 0; i < 8; ++i) {
      ImGui::SameLine();
      ImGui::TextColored(b0_color, "  %10.3f", fpu.banks[fpu.FPSCR.FR].sp[i]);
    }

    ImGui::Text("    B0 SP08:SP15");
    for (unsigned i = 0; i < 8; ++i) {
      ImGui::SameLine();
      ImGui::TextColored(b0_color, "  %10.3f", fpu.banks[fpu.FPSCR.FR].sp[i + 8]);
    }

    const ImVec4 b1_color = fpu.FPSCR.FR ? color_active : color_inactive;
    ImGui::Text("    B1 SP00:SP07");
    for (unsigned i = 0; i < 8; ++i) {
      ImGui::SameLine();
      ImGui::TextColored(b1_color, "  %10.3f", fpu.banks[1 - fpu.FPSCR.FR].sp[i + 8]);
    }

    ImGui::Text("    B1 SP08:SP15");
    for (unsigned i = 0; i < 8; ++i) {
      ImGui::SameLine();
      ImGui::TextColored(b1_color, "  %10.3f", fpu.banks[1 - fpu.FPSCR.FR].sp[i + 8]);
    }
  }
}

u32
SH4CPUWindowGuest::get_pc() const
{
  return m_console->cpu()->registers().PC;
}
void
SH4CPUWindowGuest::set_pc(u32 new_pc)
{
  *m_console->cpu()->pc_register_pointer() = new_pc;
}
void
SH4CPUWindowGuest::pause(bool new_state)
{
  m_director->pause(new_state);
}
void
SH4CPUWindowGuest::step(u32 instructions)
{
  m_director->set_cpu_execution_mode(cpu::SH4::ExecutionMode::Interpreter);
  m_director->step_cpu(1);
}
void
SH4CPUWindowGuest::reset_system()
{
  m_director->reset_console();
}
u32
SH4CPUWindowGuest::fetch_instruction(u32 address)
{
  return m_console->cpu()->idata_read(address);
}

fox::jit::Cache *const
SH4CPUWindowGuest::get_jit_cache()
{
  return m_console->cpu()->get_jit_cache();
}

} // namespace gui
