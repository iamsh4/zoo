#pragma once

#include <vector>

#include "frontend/console_director.h"
#include "core/console.h"
#include "guest/sh4/sh4_jit.h"
#include "gui/window.h"
#include "gui/window_jit_workbench.h"

struct ImFont;

namespace fox::jit {
class Cache;
};

namespace gui {

class JitCacheWindow final : public Window {
public:
  JitCacheWindow(std::shared_ptr<ConsoleDirector> director,
                 JitWorkbenchWindow *workbench);

private:
  enum class SortField
  {
    Address,
    Instructions,
    Executed,
    CpuTime,
    GuardFails
  };

  enum class Backend
  {
    None,
    Interpreter,
    IR,
    Bytecode,
    AMD64
  };

  struct SampleEntry {
    u32 address;
    u32 guard_flags;
    u32 flags;
    size_t instructions;
    cpu::SH4::BasicBlock::StopReason stop_reason;
    cpu::SH4::BasicBlock::Stats stats;
    float cpu_time_s;
  };

  std::shared_ptr<ConsoleDirector> m_director;
  fox::jit::Cache *const m_sh4_jit;
  JitWorkbenchWindow *const m_workbench;

  uint64_t m_frames_since_sampled = 0lu;
  std::vector<SampleEntry> m_sh4_samples;
  SortField m_sh4_sort = SortField::Executed;
  fox::ref<fox::jit::CacheEntry> m_selected;

  std::pair<Backend, std::string> m_disassembly;

  void sample_sh4();
  void render_disassembly_mem_popup(const std::string &line);
  void render() override;
};

}
