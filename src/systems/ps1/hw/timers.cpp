#include "systems/ps1/hw/timers.h"
#include "systems/ps1/console.h"

namespace zoo::ps1 {

Timers::Timers(Console *console)
  : m_console(console),
    m_tick_clock_sources(
      EventScheduler::Event("timer.tick_clock_sources",
                            std::bind(&Timers::tick_clock_sources, this),
                            m_console->scheduler()))
{
  m_mode[0].interrupt_request = 1;
  m_mode[1].interrupt_request = 1;
  m_mode[2].interrupt_request = 1;

  m_counter_values[0] = 0;
  m_counter_values[1] = 0;
  m_counter_values[2] = 0;

  auto reg = m_console->mmio_registry();
  reg->setup("Timers", "TMR0_CNT", &m_counter_values[0]);
  reg->setup("Timers", "TMR1_CNT", &m_counter_values[1]);
  reg->setup("Timers", "TMR2_CNT", &m_counter_values[2]);

  reg->setup("Timers", "TMR0_MODE", &m_mode[0]);
  reg->setup("Timers", "TMR1_MODE", &m_mode[1]);
  reg->setup("Timers", "TMR2_MODE", &m_mode[2]);

  reg->setup("Timers", "TMR0_TARGET", &m_target_values[0]);
  reg->setup("Timers", "TMR1_TARGET", &m_target_values[1]);
  reg->setup("Timers", "TMR2_TARGET", &m_target_values[2]);

  m_tick_clock_sources.cancel();
  m_console->schedule_event(1000, &m_tick_clock_sources);
}

interrupts::Interrupt TMR_INTERRUPTS[3] = {
  interrupts::TMR0,
  interrupts::TMR1,
  interrupts::TMR2,
};

void
Timers::tick_clock_sources()
{
  // Advnace our internal accounting of the various clock sources forward in time.
  u32 clock_source_ticks[4] = { 0, 0, 0, 0 };
  for (u32 i = 0; i < 4; ++i) {
    // Advance clock source forward in time
    m_clock_source_ticks[i] += kSystemTickBatchSize;

    // How many whole ticks have transpired?
    const u32 whole_ticks = m_clock_source_ticks[i] / ticks_per_clock_source_tick[i];
    clock_source_ticks[i] = whole_ticks;

    // if a whole tick occured, we've recorded that in whole_ticks. save back the remainer
    // for the next event to pick up later.
    if (whole_ticks > 0) {
      m_clock_source_ticks[i] = m_clock_source_ticks[i] % ticks_per_clock_source_tick[i];
    }
  }

  // Advance the actual guest timers
  for (u32 chan = 0; chan < 3; ++chan) {
    const u32 before = m_counter_values[chan];
    u32 delta;

    //  Counter 0:  0 or 2 = System Clock,  1 or 3 = Dotclock
    //  Counter 1:  0 or 2 = System Clock,  1 or 3 = Hblank
    //  Counter 2:  0 or 1 = System Clock,  2 or 3 = System Clock/8
    if (chan == 0 && m_mode[chan].clock_source & 1) {
      delta = clock_source_ticks[1];
    } else if (chan == 1 && m_mode[chan].clock_source & 1) {
      delta = clock_source_ticks[3];
    } else if (chan == 2 && m_mode[chan].clock_source > 1) {
      delta = clock_source_ticks[2];
    } else {
      delta = kSystemTickBatchSize;
    }

    // you can raise an irq in (one-shot mode and IRQ is low) or (repeated mode)
    // const bool can_raise_irq =
    //   (m_mode[chan].irq_repeatedly == 0 && m_mode[chan].interrupt_request) ||
    //   (m_mode[chan].interrupt_request == 1);

    // Did we reach targets?
    const u32 after = before + delta;
    const bool reached_target =
      before < m_target_values[chan] && after >= m_target_values[chan];
    const bool reached_ffff = before < 0xffff && after >= 0xffff;
    // const u32 wrap = m_mode[chan].reset_to_0 ? m_target_values[chan] : 0x10000;

    // Finally, update the counters and other flags
    m_mode[chan].reached_target |= reached_target;
    m_mode[chan].reached_ffff |= reached_ffff;
    m_counter_values[chan] = after & 0xffff;

    if ((m_mode[chan].irq_at_ffff && reached_ffff) ||
        (m_mode[chan].irq_at_target && reached_target)) {
      m_console->irq_control()->raise(TMR_INTERRUPTS[chan]);
      // active low
      m_mode[chan].interrupt_request = 0;
    } else if (!m_mode[chan].irq_toggle) {
      m_mode[chan].interrupt_request = 1;
    }
  }

  m_console->schedule_event(kSystemTickBatchSize, &m_tick_clock_sources);
}

u8
Timers::read_u8(u32 addr)
{
  printf("read_u8 0x%08x\n", addr);
  assert(false);
  throw std::runtime_error("Unhandled timer read_u8");
}

void
Timers::write_u8(u32 addr, u8 value)
{
  printf("write_u8 0x%08x\n", addr);
  assert(false);
  throw std::runtime_error("Unhandled timer write_u8");
}

u16
Timers::read_u16(u32 addr)
{
  // printf("read_u16 0x%08x\n", addr);
  return read_u32(addr);
}
void
Timers::write_u16(u32 addr, u16 value)
{
  // printf("write_u16 0x%08x\n", addr);
  write_u32(addr, value);
}

u32
Timers::read_u32(u32 addr)
{
  const u8 chan = (addr >> 4) & 0xf;
  const u32 base_addr = addr & 0xffff'ff0f;

  switch (base_addr) {
    case 0x1f80'1100:
      return m_counter_values[chan];

    case 0x1f80'1104: {
      // printf("timers: read_u32 0x%08x pc=0x%08x\n", addr, m_console->cpu()->PC());
      u32 val = m_mode[chan].raw;
      m_mode[chan].reached_target = 0;
      m_mode[chan].reached_ffff = 0;
      return val;
    }

    case 0x1f80'1108:
      printf("timers: read_u32 0x%08x pc=0x%08x\n", addr, m_console->cpu()->PC());
      return m_target_values[chan];

    default:
      printf("timers: read_u32 0x%08x pc=0x%08x\n", addr, m_console->cpu()->PC());
      assert(false);
      throw std::runtime_error("Unhandled Timers read_u32");
  }
}

void
Timers::write_u32(u32 addr, u32 value)
{
  const u8 chan = (addr >> 4) & 0xf;
  const u32 base_addr = addr & 0xffff'ff0f;

  switch (base_addr) {
    case 0x1f80'1100:
      // printf("timers: counter[%u] = 0x%08x\n", chan, value);
      m_counter_values[chan] = value;
      break;

    case 0x1f80'1104:
      printf("timers: counter_mode[%u] = 0x%08x\n", chan, value);
      m_mode[chan].raw = value & 0xffff;
      m_mode[chan].interrupt_request = 1; // active low, so acks the interrupt
      m_counter_values[chan] = 0;

      if (m_mode[chan].irq_toggle) {
        assert(false);
      }
      break;

    case 0x1f80'1108:
      printf("timers: target[%u] = 0x%08x\n", chan, value);
      m_target_values[chan] = value & 0xffff;
      break;

    default:
      printf("timers: write_u32 0x%08x < 0x%08x\n", addr, value);
      assert(false);
  }
}

void
Timers::register_regions(fox::MemoryTable *memory)
{
  memory->map_mmio(0x1f80'1100, 0x30, "Timers", this);
}

} // namespace zoo::ps1
