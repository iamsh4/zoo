#pragma once

#include "fox/mmio_device.h"
#include "shared/types.h"
#include "shared/log.h"

namespace peripherals {

class Modem : public fox::MMIODevice {
private:
  Log::Logger<Log::LogModule::MODEM> log;

public:
  Modem();

  u8 read_u8(const u32 addr) override;
  void write_u8(const u32 addr, u8 val) override;

  void register_regions(fox::MemoryTable *memory) override;
};

};
