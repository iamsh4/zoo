#pragma once

#include <vector>

#include "fox/memtable.h"
#include "fox/jit/cache.h"
#include "shared/cpu.h"
#include "shared/types.h"

namespace gui {

class CPUWindowGuest {
public:
  virtual bool supportsBreakpoint() const = 0;
  virtual bool supportsWriteWatch() const = 0;
  virtual bool supportsReadWatch() const = 0;
  virtual u8 bytes_per_instruction() const = 0;

  virtual void breakpoint_add(u32 address) {}
  virtual void breakpoint_remove(u32 address) {}
  virtual void breakpoint_list(std::vector<u32> *results) {}

  virtual void watchpoint_add(u32 address, zoo::WatchpointOperation) {}
  virtual void watchpoint_remove(u32 address, zoo::WatchpointOperation) {}
  virtual void write_watch_list(std::vector<u32>* out) {}

  virtual u32 get_pc() const = 0;
  virtual void set_pc(u32 new_pc) = 0;
  virtual void pause(bool new_state) = 0;
  virtual void step(u32 instructions) = 0;
  virtual void reset_system() = 0;

  virtual fox::MemoryTable::MemoryRegions memory_regions() = 0;
  virtual u32 fetch_instruction(u32 address) = 0;
  virtual fox::jit::Cache *const get_jit_cache() = 0;

  virtual u64 elapsed_cycles() = 0;

  virtual void render_registers() = 0;
  virtual std::string disassemble(u32 instruction, u32 pc) = 0;
};

} // namespace  gui
