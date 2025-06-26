#pragma once

#include "fox/mmio_device.h"
#include "shared/async_fifo.h"
#include "shared/log.h"
#include "shared/scheduler.h"
#include "serialization/serializer.h"

class Console;

namespace apu {

class RTC : public fox::MMIODevice, public serialization::Serializer {
  Log::Logger<Log::LogModule::AUDIO> log;

public:
  RTC(Console *console);
  ~RTC();

  void reset();
  void register_regions(fox::MemoryTable *memory) override;

  u8 read_u8(u32 addr) override;
  u16 read_u16(u32 addr) override;
  u32 read_u32(u32 addr) override;

  void write_u8(u32 addr, u8 val) override;
  void write_u16(u32 addr, u16 val) override;
  void write_u32(u32 addr, u32 val) override;

  void serialize(serialization::Snapshot &snapshot) override;
  void deserialize(const serialization::Snapshot &snapshot) override;

private:
  Console *const m_console;
  u32 m_rtc_bits;
  u32 m_rtc_en;
  EventScheduler::Event m_rtc_tick;
  
  void rtc_tick();
};

}
