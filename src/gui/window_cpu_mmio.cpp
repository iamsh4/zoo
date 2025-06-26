#include <imgui.h>

#include "guest/sh4/sh4_debug.h"
#include "gui/window_cpu_mmio.h"

namespace gui {

CPUMMIOWindow::CPUMMIOWindow(std::shared_ptr<ConsoleDirector> director)
  : Window("CPU MMIO"),
    m_director(director)
{
  return;
}

void
CPUMMIOWindow::render_mmio_registers()
{
  auto console = m_director->console();

  ImGui::Text("Timer Registers (TMU)");
  ImGui::BeginChild("TMUregs");
  ImGui::Columns(8);

  // Headers
  ImGui::Text("Channel");
  ImGui::NextColumn();
  ImGui::Text("Running?");
  ImGui::NextColumn();
  ImGui::Text("TCNT[n]");
  ImGui::NextColumn();
  ImGui::Text("TCOR[n]");
  ImGui::NextColumn();
  ImGui::Text("TCR[n]");
  ImGui::NextColumn();
  ImGui::Text("TCR[n].TPSC");
  ImGui::NextColumn();
  ImGui::Text("Underflow?");
  ImGui::NextColumn();
  ImGui::Text("Raises Interrupts?");
  ImGui::NextColumn();
  ImGui::Separator();

  const cpu::MMIO &mmio_regs(console->cpu()->m_mmio);
  for (int i = 0; i < 3; ++i) {
    ImGui::BeginGroup();

    ImGui::Text("%d", i);
    ImGui::NextColumn();

    bool running = mmio_regs.TSTR.raw & (1 << i);
    ImGui::Text("%s", running ? "yes" : "no");
    ImGui::NextColumn();

    // const u32 tcnt[3] = { mmio_regs.TCNT0.raw, mmio_regs.TCNT1.raw, mmio_regs.TCNT2.raw
    // };
    ImGui::Text("0x%08x", mmio_regs.TCNT[i].raw);
    ImGui::NextColumn();

    ImGui::Text("0x%08x", mmio_regs.TCOR[i].raw);
    ImGui::NextColumn();

    ImGui::Text("0x%04x", mmio_regs.TCR[i].raw);
    ImGui::NextColumn();

    static const unsigned clock_dividers[8] = { 4, 16, 64, 256, 1024, 1024, 1024, 1024 };
    ImGui::Text("%d (/%u)", mmio_regs.TCR[i].TPSC, clock_dividers[mmio_regs.TCR[i].TPSC]);
    ImGui::NextColumn();

    ImGui::Text("%s", mmio_regs.TCR[i].UNF ? "yes" : "no");
    ImGui::NextColumn();

    ImGui::Text("%s", mmio_regs.TCR[i].UNIE ? "yes" : "no");
    ImGui::NextColumn();

    ImGui::EndGroup();
  }

  ImGui::Columns(1);

  // Next section
  ImGui::Separator();

  ImGui::EndChild();
}

void
CPUMMIOWindow::render()
{
  // ImGui::SetNextWindowSizeConstraints(ImVec2(1175, 300), ImVec2(1175, 1600));
  // ImGui::SetNextWindowSize(ImVec2(1175, 600), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("SH4 MMIO", NULL, ImGuiWindowFlags_NoScrollbar)) {
    ImGui::End();
    return;
  }

  render_mmio_registers();

  ImGui::End();
}

}
