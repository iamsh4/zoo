#include <regex>
#include <imgui.h>
#include <fmt/core.h>

#include "fox/jit/cache.h"
#include "guest/sh4/sh4.h"
#include "guest/sh4/sh4_jit.h"
#include "guest/sh4/sh4_debug.h"
#include "gui/window_jit_cache.h"

/* XXX */
#define AVERAGE_CYCLES_PER_INSTRUCTION 0.6f
#define NANOSECONDS_PER_CYCLE 5lu

namespace gui {

JitCacheWindow::JitCacheWindow(std::shared_ptr<ConsoleDirector> director,
                               JitWorkbenchWindow *const workbench)
  : Window("JIT Cache"),
    m_director(director),
    m_sh4_jit(director->console()->cpu()->get_jit_cache()),
    m_workbench(workbench),
    m_disassembly(Backend::None, std::string())
{
  return;
}

void
JitCacheWindow::sample_sh4()
{
  {
    std::lock_guard _(*m_sh4_jit);
    m_sh4_samples.clear();
    m_sh4_samples.reserve(m_sh4_jit->data().size());
    for (auto it : m_sh4_jit->data()) {
      cpu::SH4::BasicBlock *const block =
        static_cast<cpu::SH4::BasicBlock *>(it.second.get());

      m_sh4_samples.emplace_back(
        SampleEntry { it.first,
                      block->guard_flags(),
                      block->flags(),
                      block->instructions().size(),
                      block->stop_reason(),
                      block->stats(),
                      (block->stats().count_executed * block->instructions().size()) *
                        NANOSECONDS_PER_CYCLE / 1000.0f / 1000.0f / 1000.0f *
                        AVERAGE_CYCLES_PER_INSTRUCTION });
    }
  }

  switch (m_sh4_sort) {
    case SortField::Address:
      /* Automatically sorted coming from map<>. */
      break;

    case SortField::Instructions:
      std::sort(m_sh4_samples.begin(),
                m_sh4_samples.end(),
                [](const SampleEntry &a, const SampleEntry &b) {
                  return a.instructions > b.instructions;
                });
      break;

    case SortField::Executed:
      std::sort(m_sh4_samples.begin(),
                m_sh4_samples.end(),
                [](const SampleEntry &a, const SampleEntry &b) {
                  return a.stats.count_executed > b.stats.count_executed;
                });
      break;

    case SortField::CpuTime:
      std::sort(m_sh4_samples.begin(),
                m_sh4_samples.end(),
                [](const SampleEntry &a, const SampleEntry &b) {
                  return a.cpu_time_s > b.cpu_time_s;
                });
      break;

    case SortField::GuardFails:
      std::sort(m_sh4_samples.begin(),
                m_sh4_samples.end(),
                [](const SampleEntry &a, const SampleEntry &b) {
                  return a.stats.guard_failed > b.stats.guard_failed;
                });
      break;
  }
}

void
JitCacheWindow::render_disassembly_mem_popup(const std::string &line)
{
  std::unordered_set<std::string> addresses;
  const std::regex address_regex("0x[0-9a-fA-F]{8}");
  std::smatch matches;
  if (std::regex_search(line, matches, address_regex)) {
    for (const auto &match : matches) {
      addresses.insert(match);
    }
  }

  static const u32 invalid_address = 0xFFFFFFFF;
  const auto deref = [&](u32 addr) {
    try {
      return m_director->console()->memory()->read<u32>(addr);
    }

    catch (std::exception &) {
      return invalid_address;
    }
  };

  if (!addresses.empty()) {
    ImGui::BeginTooltip();
    for (const auto &address : addresses) {
      ImGui::Text("%s", address.c_str());

      u32 addr = strtoul(&address.c_str()[2], nullptr, 16);
      if ((addr = deref(addr)) != invalid_address) {
        ImGui::Text(" -   *%s = 0x%08x", address.c_str(), addr);
      }

      if ((addr = deref(addr)) != invalid_address) {
        ImGui::Text(" -  **%s = 0x%08x", address.c_str(), addr);
      }

      if ((addr = deref(addr)) != invalid_address) {
        ImGui::Text(" - ***%s = 0x%08x", address.c_str(), addr);
      }
    }
    ImGui::EndTooltip();
  }
}

void
JitCacheWindow::render()
{
  ++m_frames_since_sampled;

  ImGui::SetNextWindowSizeConstraints(ImVec2(800, 300), ImVec2(1100, 1600));
  // ImGui::SetNextWindowSize(ImVec2(1175, 600), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Jit Statistics", NULL, ImGuiWindowFlags_NoScrollbar)) {
    ImGui::End();
    return;
  }

  /* Only sample the JIT's state once per "second" or so, to avoid the perf
   * impact of locking contention with the CPU thread. */
  if (m_frames_since_sampled >= 30lu) {
    m_frames_since_sampled = 0lu;
    sample_sh4();
  }

  {
    ImGui::Columns(2);

    ImGui::Text("Total blocks in SH4 JIT cache: %lu", m_sh4_samples.size());

    ImGui::NextColumn();

    if (ImGui::Button("Nuke JIT Cache")) {
      auto cache = m_director->console()->cpu()->get_jit_cache();
      cache->memory_dirtied(0, 0xFFFFFFFF);
    }
  }

  ImGui::Columns(11);

  /* Headers */
  if (ImGui::Button("Address")) {
    m_sh4_sort = SortField::Address;
    m_frames_since_sampled = UINT32_MAX;
  }
  ImGui::NextColumn();

  ImGui::Text("Limit");
  ImGui::NextColumn();
  ImGui::Text("Flags");
  ImGui::NextColumn();

  if (ImGui::Button("Instructions")) {
    m_sh4_sort = SortField::Instructions;
    m_frames_since_sampled = UINT32_MAX;
  }
  ImGui::NextColumn();

  if (ImGui::Button("Executed")) {
    m_sh4_sort = SortField::Executed;
    m_frames_since_sampled = UINT32_MAX;
  }
  ImGui::NextColumn();

  ImGui::Text("Interpreted");
  ImGui::NextColumn();
  ImGui::Text("Native");
  ImGui::NextColumn();

  if (ImGui::Button("CPU Time")) {
    m_sh4_sort = SortField::CpuTime;
    m_frames_since_sampled = UINT32_MAX;
  }
  ImGui::NextColumn();

  ImGui::Text("Guard Flags");
  ImGui::NextColumn();
  if (ImGui::Button("Guard Fails")) {
    m_sh4_sort = SortField::GuardFails;
    m_frames_since_sampled = UINT32_MAX;
  }
  ImGui::NextColumn();
  ImGui::Text("Consistency");
  ImGui::NextColumn();

  ImGui::Separator();

  static const ImVec4 color_green(0.6f, 0.9f, 0.6f, 1.0f);
  static const ImVec4 color_red(0.9f, 0.6f, 0.6f, 1.0f);

  const int line_total_count = m_sh4_samples.size();
  ImGuiListClipper clipper(line_total_count);
  while (clipper.Step()) {
    for (int line_i = clipper.DisplayStart; line_i < clipper.DisplayEnd; ++line_i) {
      const auto &entry = m_sh4_samples[line_i];

      char label[32];
      snprintf(label, sizeof(label), "%08X", entry.address);
      if (ImGui::Selectable(label,
                            m_selected && m_selected->virtual_address() == entry.address,
                            ImGuiSelectableFlags_SpanAllColumns)) {
        m_selected = m_sh4_jit->lookup(entry.address);
        m_workbench->set_target(m_selected);
        m_disassembly.first = Backend::None;
      }
      ImGui::NextColumn();

      switch (entry.stop_reason) {
        case cpu::SH4::BasicBlock::StopReason::SizeLimit:
          ImGui::Text("size");
          ImGui::NextColumn();
          break;
        case cpu::SH4::BasicBlock::StopReason::Branch:
          ImGui::Text("branch");
          ImGui::NextColumn();
          break;
        case cpu::SH4::BasicBlock::StopReason::StartOfBlock:
          ImGui::Text("ebb");
          ImGui::NextColumn();
          break;
        case cpu::SH4::BasicBlock::StopReason::Barrier:
          ImGui::Text("barrier");
          ImGui::NextColumn();
          break;
        case cpu::SH4::BasicBlock::StopReason::InvalidOpcode:
          ImGui::Text("badop");
          ImGui::NextColumn();
          break;
      }
      ImGui::Text("%c%c",
                  (entry.flags & cpu::SH4::BasicBlock::DIRTY) ? 'D' : '_',
                  (entry.flags & cpu::SH4::BasicBlock::DISABLE_FASTMEM) ? '_' : 'F');
      ImGui::NextColumn();

      ImGui::Text("%lu", entry.instructions);
      ImGui::NextColumn();
      ImGui::Text("%s", fmt::format("{}", entry.stats.count_executed).c_str());
      ImGui::NextColumn();
      ImGui::Text("%s", fmt::format("{}", entry.stats.count_interpreted).c_str());
      ImGui::NextColumn();
      ImGui::Text("%s", fmt::format("{}", entry.stats.count_compiled).c_str());
      ImGui::NextColumn();
      ImGui::Text("%0.3fs", entry.cpu_time_s);
      ImGui::NextColumn();

      const unsigned n_guard_bits = 4;
      for (unsigned i = 0u; i < n_guard_bits; ++i) {
        const u32 bit =
          1lu << (n_guard_bits - 1 -
                  i); // Show them in the same order they appear in the CPU
                      // flags since this text is generated from left-to-right
        const bool dont_care = !(entry.guard_flags & bit);
        const bool value = !!(entry.stats.last_flags & bit);
        ImGui::Text("%c", dont_care ? 'X' : (value ? '1' : '0'));
        ImGui::SameLine();
      }
      ImGui::NextColumn();

      ImGui::Text("%s", fmt::format("{}", entry.stats.guard_failed).c_str());
      ImGui::NextColumn();

      /* Color based on the consistency of the flags. */
      if (entry.stats.count_executed > 10) {
        const unsigned flag_pct =
          entry.stats.last_flags_count * 100 / entry.stats.count_executed;
        const bool is_consistent = entry.stats.last_flags_count > 1000 || (flag_pct > 90);
        ImGui::TextColored(is_consistent ? color_green : color_red,
                           "%s",
                           fmt::format("{}", entry.stats.last_flags_count).c_str());
        ImGui::NextColumn();
      } else {
        ImGui::Text("%s", fmt::format("{}", entry.stats.last_flags_count).c_str());
        ImGui::NextColumn();
      }

      ImGui::Separator();
    }
  }
  clipper.End();

  ImGui::End();
}

}
