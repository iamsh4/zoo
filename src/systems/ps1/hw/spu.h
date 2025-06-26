#pragma once

#include <fox/mmio_device.h>
#include "shared/types.h"

namespace zoo::ps1 {

class Console;

class SPU : public fox::MMIODevice {
private:
  Console *m_console;

  union {
    u16 raw;
  } SPUCNT;
  
  union {
    u16 raw;
  } SPUSTAT;

  u16 m_irq_address;

  u32 PMON;
  u16 m_data_transfer_addr;
  u16 m_sound_ram_data_transfer_ctrl;

public:
  SPU(Console *);

  u8 read_u8(u32 addr) override;
  u16 read_u16(u32 addr) override;
  u32 read_u32(u32 addr) override;

  void write_u8(u32 addr, u8 value) override;
  void write_u16(u32 addr, u16 value) override;
  void write_u32(u32 addr, u32 value) override;

  void push_dma_word(u32 word);

  void register_regions(fox::MemoryTable *memory) override;
};

} // namespace zoo::ps1
