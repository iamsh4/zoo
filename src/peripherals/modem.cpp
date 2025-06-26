#include <iostream>

#include "peripherals/modem.h"

namespace peripherals {

Modem::Modem()
{
  return;
}

u8
Modem::read_u8(const u32 addr)
{
  log.error("Unhandled read to Modem -- returning 0");
  return 0u;
}

void
Modem::write_u8(const u32 addr, u8 val)
{
  log.error("Unhandled write to Modem -- returning 0");
}

void
Modem::register_regions(fox::MemoryTable *const memory)
{
  memory->map_mmio(0x00600000u, 0x800u, "Modem", this);
}

};
