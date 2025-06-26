#include "core/placeholder_mmio.h"
#include "shared/log.h"

void
PlaceholderMMIO::register_regions(fox::MemoryTable *const memory)
{
  memory->map_mmio(phys_start, phys_end - phys_start, name.c_str(), this);
}

u8
PlaceholderMMIO::read_u8(u32 address)
{
  log.warn("Unhandled read from %s Register @0x%08x (u8)", name.c_str(), address);
  return 0u;
}

u16
PlaceholderMMIO::read_u16(u32 address)
{
  log.warn("Unhandled read from %s Register @0x%08x (u16)", name.c_str(), address);
  return 0u;
}

u32
PlaceholderMMIO::read_u32(u32 address)
{
  log.warn("Unhandled read from %s Register @0x%08x (u32)", name.c_str(), address);
  return 0u;
}

void
PlaceholderMMIO::write_u8(u32 address, u8 value)
{
  log.warn(
    "Unhandled write to %s Register @0x%08x value 0x%02x", name.c_str(), address, value);
}

void
PlaceholderMMIO::write_u16(u32 address, u16 value)
{
  log.warn(
    "Unhandled write to %s Register @0x%08x value 0x%04x", name.c_str(), address, value);
}

void
PlaceholderMMIO::write_u32(u32 address, u32 value)
{
  log.warn(
    "Unhandled write to %s Register @0x%08x value 0x%08x", name.c_str(), address, value);
}
