#pragma once

#include "fox/mmio_device.h"
#include "shared/types.h"
#include "shared/scheduler.h"

namespace zoo::ps1 {

class Console;

class Timers : public fox::MMIODevice {
private:
  Console *m_console;

  union CounterMode {
    struct {
      // synchronize with bit 1+2
      u32 sync_enable : 1;
      //  Synchronization Modes for Counter 0:
      //    0 = Pause counter during Hblank(s)
      //    1 = Reset counter to 0000h at Hblank(s)
      //    2 = Reset counter to 0000h at Hblank(s) and pause outside of Hblank
      //    3 = Pause until Hblank occurs once, then switch to Free Run
      //  Synchronization Modes for Counter 1:
      //    Same as above, but using Vblank instead of Hblank
      //  Synchronization Modes for Counter 2:
      //    0 or 3 = Stop counter at current value (forever, no h/v-blank start)
      //    1 or 2 = Free Run (same as when Synchronization Disabled)
      u32 sync_mode : 2;
      // 0=counter==ffff 1=counter==target
      u32 reset_to_0 : 1;
      // 1=enable
      u32 irq_at_target : 1;
      // 1=enable
      u32 irq_at_ffff : 1;
      // 0=one-shot 1=repeatedly
      u32 irq_repeatedly : 1;
      // 0=short bit10=0 pulse 1=toggle bit10 on/off
      u32 irq_toggle : 1;
      //  Counter 0:  0 or 2 = System Clock,  1 or 3 = Dotclock
      //  Counter 1:  0 or 2 = System Clock,  1 or 3 = Hblank
      //  Counter 2:  0 or 1 = System Clock,  2 or 3 = System Clock/8
      u32 clock_source : 2;
      // 0=yes 1=no
      u32 interrupt_request : 1;
      // 0=no 1=yes
      u32 reached_target : 1;
      // 0=no 1=yes
      u32 reached_ffff : 1;
      u32 _unused : (16 + 3);
    };
    u32 raw;
  };

  // Registers
  u32 m_counter_values[3] = {};
  CounterMode m_mode[3] = {};
  u32 m_target_values[3] = {};

  // In order to avoid literally calling a callback every system clock tick (i.e. at 33
  // MHz), we batch N system clock ticks together, and then advance timers appropriately
  static constexpr u32 kSystemTickBatchSize = 8;
  EventScheduler::Event m_tick_clock_sources;
  void tick_clock_sources();

  // Each of these 'tick' at a rate of system clock (33MHZ) divided by the number given in
  // this table

  // sysclock is 33ns
  // 0 : sysclock        = 1
  // 1 : dotclock        = 5    (Assuming width=320 ~= 150ns)
  // 2 : sysclock / 8    = 8
  // 3 : hblank          = 1909 (Assuming 60fps, 263 scanlines ~= 63 microseconds)
  static constexpr u64 ticks_per_clock_source_tick[4] = { 1, 5, 8, 1909 };
  u64 m_clock_source_ticks[4] = {};

public:
  Timers(Console *console);

  u8 read_u8(u32 addr) override;
  void write_u8(u32 addr, u8 value) override;

  u16 read_u16(u32 addr) override;
  void write_u16(u32 addr, u16 value) override;

  u32 read_u32(u32 addr) override;
  void write_u32(u32 addr, u32 value) override;

  void tick_hblank();

  void register_regions(fox::MemoryTable *memory) override;
};

} // namespace zoo::ps1
