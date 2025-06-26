
#include <regex>
#include <stack>
#include <unordered_set>
#include <string>
#include <algorithm>
#include <functional>

#include <fmt/core.h>
#include <fmt/format.h>
#include <imgui.h>

#include "shared/types.h"
#include "guest/sh4/sh4_debug.h"
#include "gui/window_cpu.h"
#include "core/registers.h"
#include "shared/crc32.h"

namespace gui {

static u32
read_hex_u32(const char *input)
{
  if (strlen(input) > 2 && input[0] == '0' && input[1] == 'x') {
    input += 2;
  }

  u32 address;
  if (sscanf(input, "%x", &address) == 1) {
    return address;
  }
  return 0xFFFFFFFF;
}

CPUWindow::CPUWindow(std::string_view name,
                     std::shared_ptr<CPUWindowGuest> guest,
                     JitWorkbenchWindow *const workbench)
  : Window(name.data()),
    m_cpu_guest(guest),
    m_workbench(workbench),
    m_cpu_stepper(new CpuStepperWidget(m_cpu_guest, workbench, 8192))
{
  m_window_name = name;
  return;
}

void
CPUWindow::render()
{
  /* Update list of active CPU breakpoints */
  m_breakpoints.clear();
  m_cpu_guest->breakpoint_list(&m_breakpoints);

  ImGui::SetNextWindowSizeConstraints(ImVec2(1175, 300), ImVec2(1175, 1600));
  ImGui::SetNextWindowSize(ImVec2(1175, 600), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(m_window_name.c_str(), NULL, ImGuiWindowFlags_NoScrollbar)) {
    ImGui::End();
    return;
  }

  ImGui::Text(
    "%s",
    fmt::format(std::locale("en_US.UTF-8"), "{:L} cycles", m_cpu_guest->elapsed_cycles())
      .c_str());
  ImGui::Separator();

  /* Register States */
  m_cpu_guest->render_registers();

  ImGui::Separator();

  ImGui::Columns(2);

  /* Disassembly view */
  {
    static char address_input[32] = {};

    ImGui::BeginChild("scrolling2", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));
    m_cpu_stepper->render();
    ImGui::EndChild();

    ImGui::PushItemWidth(ImGui::GetContentRegionAvailWidth() / 5);
    ImGui::InputText("##addrinput", address_input, sizeof(address_input));
    ImGui::PopItemWidth();

    ImGui::SameLine();
    if (ImGui::Button("Goto")) {
      u32 address = read_hex_u32(address_input);
      if (address != 0xFFFFFFFF) {
        m_cpu_guest->set_pc(address);
      }
      address_input[0] = 0;
    }

    ImGui::SameLine();
    if (ImGui::Button("Break-X")) {
      u32 address = read_hex_u32(address_input);
      if (address != 0xFFFFFFFF) {
        m_cpu_guest->breakpoint_add(address);
      }
      address_input[0] = 0;
    }
#if 0
    ImGui::SameLine();
    if (ImGui::Button("Break-R")) {
      u32 address = read_hex_u32(address_input);
      if (address != 0xFFFFFFFF) {
        m_cpu_guest->watchpoint_add(address, zoo::WatchpointOperation::Read);
      }
      address_input[0] = 0;
    }
#endif
    ImGui::SameLine();
    if (ImGui::Button("Break-W")) {
      u32 address = read_hex_u32(address_input);
      if (address != 0xFFFFFFFF) {
        m_cpu_guest->watchpoint_add(address, zoo::WatchpointOperation::Write);
      }
      address_input[0] = 0;
    }

    ImGui::SameLine();
    if (ImGui::Button("Halt")) {
      m_cpu_guest->pause(true);
    }

    ImGui::SameLine();
    if (ImGui::Button("Continue")) {
      m_cpu_guest->pause(false);
    }

    ImGui::SameLine();
    if (ImGui::Button("Step")) {
      m_cpu_guest->step(1);
    }

    // ImGui::SameLine();
    // if (ImGui::Button("Step-Back")) {
    //   m_director->step_cpu(-1);
    // }

    ImGui::SameLine();
    if (ImGui::Button("Reboot")) {
      m_cpu_guest->reset_system();
    }
  }

  ImGui::NextColumn();
  ImGui::BeginChild("cpu_window_right_side",
                    ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));

  /* Breakpoints and shortcuts */
  {
    ImGui::Text("Breakpoints");

    // List of breakpoints was updated at entrance to this function
    if (m_breakpoints.empty())
      ImGui::Text("(None)");

    const u32 pc = m_cpu_guest->get_pc();
    for (unsigned i = 0; i < m_breakpoints.size(); ++i) {
      char label[32];

      snprintf(label, sizeof(label), "Remove##%d", i);
      if (ImGui::Button(label)) {
        m_cpu_guest->breakpoint_remove(m_breakpoints[i]);
      }
      ImGui::SameLine();

      static const ImVec4 color_active(0.9f, 0.9f, 0.9f, 1.0f);
      static const ImVec4 color_hit(0.7f, 1.0f, 0.7f, 1.0f);
      ImGui::TextColored(
        (m_breakpoints[i] == pc) ? color_hit : color_active, "0x%08x", m_breakpoints[i]);
    }
  }

// TODO : This is not hard to fix. We just need to add list_watchpoints for guests. I'm
// being lazy since we don't use this often.
#if 0
  {
    ImGui::Text("Read Watchpoints");
    const auto &read_watches = console->cpu()->m_debug_read_watchpoints;

    if (read_watches.empty())
      ImGui::Text("(None)");

    for (auto read_watch : read_watches) {
      char label[32];
      snprintf(label, sizeof(label), "Remove##%d", read_watch);
      if (ImGui::Button(label)) {
        watchpoint_remove(read_watch, cpu::SH4::WatchpointOperation::Read);
      }
      ImGui::SameLine();
      ImGui::Text("0x%08x", read_watch);
    }
  }
#endif

  {
    ImGui::Text("Write Watchpoints");
    std::vector<u32> write_watches;
    m_cpu_guest->write_watch_list(&write_watches);

    if (write_watches.empty())
      ImGui::Text("(None)");

    for (auto write_watch : write_watches) {
      char label[32];
      snprintf(label, sizeof(label), "Remove##%d", write_watch);
      if (ImGui::Button(label)) {
        m_cpu_guest->watchpoint_remove(write_watch, zoo::WatchpointOperation::Write);
      }
      ImGui::SameLine();
      ImGui::Text("0x%08x", write_watch);
    }
  }

  // If we really need to add this back, it needs support in the general guest interface
#if 0
  ImGui::Separator();
  {
    ImGui::Text("Trace Data");
    auto &trace_data = console->cpu()->m_trace;

    if (ImGui::Button("Reset Statistics"))
      trace_data.reset();

    {
      ImGui::Text("Recent edges (Most recent on top)");
      std::vector<tracing::Edge> edges;
      const std::unordered_map<tracing::Edge, tracing::Trace::Stats> edge_stats =
        trace_data.get_edge_stats();

      for (auto &[k, v] : edge_stats) {
        edges.push_back(k);
      }
      std::sort(
        edges.begin(), edges.end(), [&](const tracing::Edge &a, const tracing::Edge &b) {
          const auto time_a = edge_stats.at(a).most_recent_visit;
          const auto time_b = edge_stats.at(b).most_recent_visit;
          return time_a > time_b;
        });
      for (unsigned i = 0; i < 4000 && i < edges.size(); ++i) {
        const auto &edge = edges[i];
        const auto &stats = edge_stats.at(edge);
        unsigned cycles = stats.most_recent_visit;
        ImGui::Text(" - Edge (0x%08x -> 0x%08x) Most recently seen %u",
                    edge.source,
                    edge.destination,
                    cycles);

        // On click, jump the CPU there.
        if (ImGui::IsItemClicked()) {
          console->cpu()->regs.PC = edge.source;
        }
      }
    }

    ImGui::Text("");
    ImGui::Text("Current block count = %lu", trace_data.get_block_stats().size());
    ImGui::Text("Current edge count = %lu", trace_data.get_edge_stats().size());
  }
#endif

// TODO : Add to the Guest interface
#if 0
  ImGui::Separator();
  {
    ImGui::Text("Call/Return Stack");
    std::vector<u32> stack;
    m_director->console()->cpu()->copy_call_stack(stack);
    std::vector<const SDKSymbol *> symbols;

    for (u32 i = 0; i < stack.size(); ++i) {
      const u32 stack_pc = stack[i];

      const u32 matching_symbol_count =
        SDKSymbolManager::instance().get_matching_function_symbols(
          *m_director->console()->memory(), stack_pc, symbols, 10);

      const SDKSymbol *symbol = nullptr;
      if (matching_symbol_count > 0) {
        symbol = symbols[0];
      }

      if (symbol != nullptr) {
        ImGui::Text(" - (%u) 0x%08x (%s.%s.%s [%u]%s)",
                    i,
                    stack[i],
                    symbol->sdk_name,
                    symbol->library_name,
                    symbol->symbol_name,
                    (u32)symbol->total_length,
                    symbol->is_ambiguous ? " (amiguous)" : "");
      } else {
        ImGui::Text(" - (%u) 0x%08x", i, stack[i]);
      }

      // Ambiguous, possibly show other matches
      if (ImGui::IsItemHovered() && matching_symbol_count > 0 && symbol->is_ambiguous) {
        ImGui::BeginTooltip();
        for (u32 i = 0; i < matching_symbol_count; ++i) {
          const auto &sym = symbols[i];
          ImGui::Text("%s.%s.%s", sym->sdk_name, sym->library_name, sym->symbol_name);
        }
        ImGui::EndTooltip();
      }

      // Click address to copy to clipboard.
      if (ImGui::IsItemClicked()) {
        char clipboardText[256];
        snprintf(clipboardText, sizeof(clipboardText), "0x%08x", stack[i]);
        ImGui::SetClipboardText(clipboardText);
      }
    }
  }
#endif

  ImGui::EndChild();
  ImGui::Columns(1);

  ImGui::End();
}

}
