#include <fmt/core.h>
#include <time.h>

#include "apu/aica_rtc.h"
#include "core/console.h"

#if 1
#define DEBUG(args...) fmt::print(args)
#else
#define DEBUG(args...)
#endif

namespace apu {

static u64
get_seconds_since_base()
{
  // Returning a constant time right now. This is apparently
  // 11/27/1999 based on what it tried to write.

  time_t current_time = time(nullptr);
  u64 system_seconds  = current_time;

  // Need to add 20 years since DC epoch starts in 1950.
  system_seconds += 20llu * 365 * 24 * 60 * 60;

  // TODO : add the actual number of leap days. This is good enough for the next couple of
  // years :p
  system_seconds += 5llu * 24 * 60 * 60;

  return system_seconds;
}

RTC::RTC(Console *const console)
  : m_console(console),
    m_rtc_tick(EventScheduler::Event("RTC Tick",
                                     std::bind(&RTC::rtc_tick, this),
                                     console->scheduler()))
{
  reset();
}

RTC::~RTC()
{
  m_rtc_tick.cancel();
}

void
RTC::serialize(serialization::Snapshot &snapshot)
{
  m_rtc_tick.serialize(snapshot);

  u64 rtc_data = m_rtc_bits;
  rtc_data |= u64(m_rtc_en) << 32;
  
  static_assert(sizeof(rtc_data) == 8);
  snapshot.add_range("rtc.state", sizeof(rtc_data), &rtc_data);
}

void
RTC::deserialize(const serialization::Snapshot &snapshot)
{
  m_rtc_tick.deserialize(snapshot);

  u64 rtc_data;
  snapshot.apply_all_ranges("rtc.state", &rtc_data);
  m_rtc_bits = u32((rtc_data >> 0) & 0xffff'ffff);
  m_rtc_en   = u32((rtc_data >> 32) & 0xffff'ffff);
}

void
RTC::rtc_tick()
{
  m_rtc_bits++;
  // Tick again in 1 second.
  m_console->schedule_event(1'000'000'000LLU, &m_rtc_tick);
}

void
RTC::reset()
{
  // Disable writes, reset to current host time
  m_rtc_en   = 0;
  m_rtc_bits = get_seconds_since_base();

  // Schedule the first tick.
  m_rtc_tick.cancel();
  m_console->schedule_event(1'000'000'000LLU, &m_rtc_tick);
}

void
RTC::register_regions(fox::MemoryTable *const memory)
{
  memory->map_mmio(0x00710000u, 0x12u, "AICA RTC", this);
}

u8
RTC::read_u8(const u32 addr)
{
  log.error("AICA RTC unhandled read_u8 @ 0x%08X\n", addr);
  return 0;
}

u16
RTC::read_u16(const u32 addr)
{
  log.error("AICA RTC unhandled read_u16 @ 0x%08X\n", addr);
  return 0;
}

u32
RTC::read_u32(const u32 addr)
{
  u32 result;
  switch (addr) {
    case 0x00710000:
      result = (m_rtc_bits >> 16) & 0xFFFF;
      break;

    case 0x00710004:
      result = (m_rtc_bits >> 0) & 0xFFFF;
      break;

    default:
      log.error("AICA RTC unhandled read_u32 @ 0x%08X", addr);
      result = 0;
  }

  log.debug("RTC read u32 from 0x%08x -> 0x%08x", addr, result);

  return result;
}

void
RTC::write_u8(const u32 addr, const u8 val)
{
  log.error("AICA RTC unhandled write_u8 %u @ 0x%08X", val, addr);
}

void
RTC::write_u16(const u32 addr, const u16 val)
{
  log.debug("RTC write u16 to 0x{:08x} <- 0x{:04x}", addr, val);

  switch (addr) {
    case 0x0071'0000:
      if (m_rtc_en) {
        m_rtc_bits &= 0xFFFF;
        m_rtc_bits |= (val << 16);
        // write-enable is reset after writing the second half of the word.
        m_rtc_en = 0;
      }
      break;

    case 0x0071'0004:
      if (m_rtc_en) {
        m_rtc_bits &= 0xFFFF0000;
        m_rtc_bits |= val;
      }
      break;

    case 0x00710008:
      m_rtc_en = val & 1;
      break;

    default:
      log.error("AICA RTC unhandled write_u16 %u @ 0x%08X", val, addr);
  }
}

void
RTC::write_u32(const u32 addr, const u32 val)
{
  log.warn("AICA RTC write_u32 passing to write_u16");
  write_u16(addr, val);
}

}
