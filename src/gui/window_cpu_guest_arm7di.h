#pragma once

#include "frontend/console_director.h"
#include "core/console.h"
#include "gui/window_cpu_guest.h"

namespace gui {
class ARM7DICPUWindowGuest final : public CPUWindowGuest {
private:
  std::shared_ptr<ConsoleDirector> m_director;
  Console *m_console;

public:
  ARM7DICPUWindowGuest(std::shared_ptr<ConsoleDirector> director)
    : m_director(director),
      m_console(director->console().get())
  {
  }

  bool supportsBreakpoint() const override
  {
    return false;
  }
  
  bool supportsWriteWatch() const override
  {
    return false;
  }
  
  bool supportsReadWatch() const override
  {
    return false;
  }

  u8 bytes_per_instruction() const final
  {
    return 4;
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
    // TODO
    return 0;
  }
};
}
