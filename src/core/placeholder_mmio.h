#pragma once

#include "fox/mmio_device.h"
#include "shared/types.h"
#include "shared/log.h"

class PlaceholderMMIO : public fox::MMIODevice {
public:
  PlaceholderMMIO(const char *name, u32 start_address, u32 stop_address)
    : name(name),
      phys_start(start_address),
      phys_end(stop_address)
  {
  }

  void register_regions(fox::MemoryTable *memory) override;

protected:
  u8 read_u8(u32 addr) override;
  u16 read_u16(u32 addr) override;
  u32 read_u32(u32 addr) override;

  void write_u8(u32 addr, u8 val) override;
  void write_u16(u32 addr, u16 val) override;
  void write_u32(u32 addr, u32 val) override;

private:
  const std::string name;
  const u32 phys_start;
  const u32 phys_end;

  Log::Logger<Log::LogModule::HOLLY> log;
};
