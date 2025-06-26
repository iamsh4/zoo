#pragma once

#include "frontend/console_director.h"
#include "core/console.h"
#include "gui/window_cpu_guest.h"

namespace gui {
class SH4CPUWindowGuest final : public CPUWindowGuest {
private:
  ConsoleDirector *m_director;
  Console *m_console;

public:
  SH4CPUWindowGuest(ConsoleDirector *director)
    : m_director(director),
      m_console(director->console().get())
  {
  }

  bool supportsBreakpoint() const override
  {
    return true;
  }
  bool supportsWriteWatch() const override
  {
    return true;
  }
  bool supportsReadWatch() const override
  {
    return true;
  }
  u8 bytes_per_instruction() const final
  {
    return 2;
  }

  fox::MemoryTable::MemoryRegions memory_regions() final
  {
    return m_console->memory()->regions();
  }

  void breakpoint_add(u32 address) override
  {
    m_console->cpu()->debug_enable(true);
    m_console->cpu()->debug_breakpoint_add(address);
  }

  void breakpoint_remove(u32 address) override
  {
    m_console->cpu()->debug_breakpoint_remove(address);

    // Turn off debug mode as a help if this was the last breakpoint.
    std::vector<u32> breakpoint_list;
    m_console->cpu()->debug_breakpoint_list(&breakpoint_list);
    if (breakpoint_list.empty()) {
      m_console->cpu()->debug_enable(false);
    }
  }

  void breakpoint_list(std::vector<u32> *results) override
  {
    if (results) {
      results->clear();
      m_console->cpu()->debug_breakpoint_list(results);
    }
  }

  void watchpoint_add(u32 address, zoo::WatchpointOperation op) override
  {
    m_console->cpu()->debug_enable(true);
    m_console->cpu()->debug_watchpoint_add(address, op);
  }

  void watchpoint_remove(u32 address, zoo::WatchpointOperation op) override
  {
    m_console->cpu()->debug_watchpoint_remove(address, op);
  }

  void render_registers() override;

  u32 get_pc() const override;
  void set_pc(u32 new_pc) override;
  void pause(bool new_state) override;
  void step(u32 instructions) override;
  void reset_system() override;

  u32 fetch_instruction(u32 address) override;
  fox::jit::Cache *const get_jit_cache() override;

  std::string disassemble(u32 instruction, u32 pc) override;

  u64 elapsed_cycles() override
  {
    return m_console->current_time() / 5;
  }
};
}
