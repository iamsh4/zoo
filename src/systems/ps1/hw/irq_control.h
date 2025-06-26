#pragma once

#include "fox/mmio_device.h"
#include "systems/ps1/hw/interrupts.h"

namespace zoo::ps1 {

class Console;

class IRQControl : public fox::MMIODevice {
private:
  u32 m_i_stat = 0;
  u32 m_i_mask = 0;
  Console *m_console;

  void update_cpu_external_interrupt();

public:
  IRQControl(Console *console);

  u8 read_u8(u32 addr) override;
  u16 read_u16(u32 addr) override;
  u32 read_u32(u32 addr) override;

  void write_u8(u32 addr, u8 value) override;
  void write_u16(u32 addr, u16 value) override;
  void write_u32(u32 addr, u32 value) override;

  void register_regions(fox::MemoryTable *memory) override;
  void raise(interrupts::Interrupt);
};

} // namespace zoo::ps1
