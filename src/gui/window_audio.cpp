#include <imgui.h>

#include "guest/sh4/sh4_debug.h"
#include "gui/window_audio.h"

namespace gui {

AudioWindow::AudioWindow(std::shared_ptr<ConsoleDirector> director)
  : Window("Audio"),
    m_director(director)
{
  return;
}

static const char *INTERRUPT_NAMES[11] = { "Ext",    "Reserved1", "Reserved2", "MidiIn",
                                           "DMA",    "Data",      "TimerA",    "TimerB",
                                           "TimerC", "MidiOut",   "Sample" };

std::string
extract_join(const std::function<std::string(int)> &func, int start, int end)
{
  std::string result;
  for (int i = start; i < end; ++i) {
    std::string part = func(i);
    if (part.empty())
      continue;
    if (result.empty())
      result = part;
    else
      result = result + "," + part;
  }

  return result;
}

void
AudioWindow::render_internal()
{
  auto console            = m_director->console();
  const auto &common_data = console->aica()->get_common_data();
  ImGui::Text("Timers");

  ////////////////////////////////////////////////////////////
  // Timers
  {
    ImGui::Text(" - TIMA x%02x (counts up every %u samples)",
                common_data.TIMA,
                1 << common_data.TACTL);
    ImGui::Text(" - TIMB x%02x (counts up every %u samples)",
                common_data.TIMB,
                1 << common_data.TBCTL);
    ImGui::Text(" - TIMC x%02x (counts up every %u samples)",
                common_data.TIMC,
                1 << common_data.TCCTL);
  }

  ////////////////////////////////////////////////////////////
  // SCPU Interrupts

  {
    auto enabled = extract_join(
      [&](int i) { return common_data.SCIEB & (1 << i) ? INTERRUPT_NAMES[i] : ""; },
      0,
      11);
    auto pending = extract_join(
      [&](int i) { return common_data.SCIPD & (1 << i) ? INTERRUPT_NAMES[i] : ""; },
      0,
      11);
    auto scilv0 = extract_join(
      [&](int i) { return common_data.SCILV0 & (1 << i) ? INTERRUPT_NAMES[i] : ""; },
      0,
      11);
    auto scilv1 = extract_join(
      [&](int i) { return common_data.SCILV1 & (1 << i) ? INTERRUPT_NAMES[i] : ""; },
      0,
      11);
    auto scilv2 = extract_join(
      [&](int i) { return common_data.SCILV2 & (1 << i) ? INTERRUPT_NAMES[i] : ""; },
      0,
      11);
    ImGui::Text("SCPU Interrupts");
    ImGui::Text(" - Enabled: %s", enabled.c_str());
    ImGui::Text(" - Pending: %s", pending.c_str());
    ImGui::Text(" - L: 0x%x", common_data.L);
    ImGui::Text(" - SCILV0: %s", scilv0.c_str());
    ImGui::Text(" - SCILV1: %s", scilv1.c_str());
    ImGui::Text(" - SCILV2: %s", scilv2.c_str());
  }

  ////////////////////////////////////////////////////////////
  // MCPU Interrupts

  {
    auto enabled = extract_join(
      [&](int i) { return common_data.MCIEB & (1 << i) ? INTERRUPT_NAMES[i] : ""; },
      0,
      11);
    auto pending = extract_join(
      [&](int i) { return common_data.MCIPD & (1 << i) ? INTERRUPT_NAMES[i] : ""; },
      0,
      11);
    ImGui::Text("MCPU Interrupts");
    ImGui::Text(" - Enabled: %s", enabled.c_str());
    ImGui::Text(" - Pending: %s", pending.c_str());
  }

  ////////////////////////////////////////////////////////////
  // Misc
  {
    ImGui::Text("Arm7: %s", common_data.AR ? "AR high, not running" : "Running");
    ImGui::Text("Audio");
    ImGui::Text(" - Mono: %u", common_data.MN);
    ImGui::Text(" - Master Volume: %u", common_data.MVOL);
    ImGui::Text(" - AFSEL: %s", common_data.AF ? "AEG Monitor" : "FEG Monitor");
    ImGui::Text(" - Current Channel (MSLC): %u", common_data.MSLC);
    ImGui::Text(" - Access to WaveMem (MRWINH): 0x%x", common_data.MRWINH);
    ImGui::Text(" - DMA Operation: Execute Requested %u (UNIMPLEMENTED)", common_data.EX);

    const u32 rtc =
      console->rtc()->read_u32(0x00710004) | (console->rtc()->read_u32(0x00710000) << 16);
    ImGui::Text("RTC %u", rtc);
  }

  ////////////////////////////////////////////////////////////

  ImGui::Separator();

  ////////////////////////////////////////////////////////////
  // Channel Data
  {
    ImGui::BeginChild("ChannelData");
    ImGui::Columns(8);
    // Headers
    ImGui::Text("Channel");
    ImGui::NextColumn();
    ImGui::Text("KeyOnOff");
    ImGui::NextColumn();
    ImGui::Text("Loop");
    ImGui::NextColumn();
    ImGui::Text("Format");
    ImGui::NextColumn();
    ImGui::Text("StartAddr");
    ImGui::NextColumn();
    ImGui::Text("Position");
    ImGui::NextColumn();
    ImGui::Text("LEA");
    ImGui::NextColumn();
    ImGui::Text("OCT");
    ImGui::NextColumn();
    ImGui::Separator();

    for (int i = 0; i < 64; ++i) {
      const auto &data = console->aica()->get_channel_data(i);
      // data.registers.

      ImGui::BeginGroup();

      ImGui::Text("%d", i);
      ImGui::NextColumn();
      ImGui::Text("%d", data.registers.KB);
      ImGui::NextColumn();
      ImGui::Text("%d", data.registers.LP);
      ImGui::NextColumn();
      ImGui::Text("%d", data.registers.PCMS);
      ImGui::NextColumn();
      ImGui::Text("%d", (data.registers.SA_upper << 16) | data.registers.SA_lower);
      ImGui::NextColumn();
      ImGui::Text("%d", data.status.position);
      ImGui::NextColumn();
      ImGui::Text("%d", data.registers.LEA);
      ImGui::NextColumn();
      ImGui::Text("%d", data.registers.OCT);
      ImGui::NextColumn();

      ImGui::EndGroup();
    }
    ImGui::Columns(1);
    ImGui::EndChild();
  }
}

void
AudioWindow::render()
{
  if (!ImGui::Begin("Audio", NULL, ImGuiWindowFlags_NoScrollbar)) {
    ImGui::End();
    return;
  }

  render_internal();

  ImGui::End();
}

}
