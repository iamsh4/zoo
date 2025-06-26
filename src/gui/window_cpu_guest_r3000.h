#pragma once

#include "systems/ps1/director.h"
#include "gui/window_cpu_guest.h"

namespace gui {
class R3000CPUWindowGuest final : public CPUWindowGuest {
private:
  zoo::ps1::ConsoleDirector *m_director;
  zoo::ps1::Console *m_console;

public:
  R3000CPUWindowGuest(zoo::ps1::ConsoleDirector *director)
    : m_director(director),
      m_console(director->console())
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
    return false;
  }

  u8 bytes_per_instruction() const override
  {
    return 4;
  }

  void breakpoint_add(u32 address) override
  {
    m_console->cpu()->breakpoint_add(address);
  }

  void breakpoint_remove(u32 address) override
  {
    m_console->cpu()->breakpoint_remove(address);
  }

  void breakpoint_list(std::vector<u32> *results) override
  {
    m_console->cpu()->breakpoint_list(results);
  }

  void watchpoint_add(u32 address, zoo::WatchpointOperation op) override
  {
    assert(op == zoo::WatchpointOperation::Write);
    m_console->cpu()->add_mem_write_watch(address);
  }

  void watchpoint_remove(u32 address, zoo::WatchpointOperation op) override
  {
    assert(op == zoo::WatchpointOperation::Write);
    m_console->cpu()->remove_mem_write_watch(address);
  }

  void write_watch_list(std::vector<u32> *out) override
  {
    m_console->cpu()->write_watch_list(out);
  }

  fox::MemoryTable::MemoryRegions memory_regions() final
  {
    return m_console->memory()->regions();
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
    return m_console->elapsed_cycles();
  }
};
}
