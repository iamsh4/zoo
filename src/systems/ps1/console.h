#pragma once

#include "guest/r3000/r3000.h"
#include "renderer/vulkan.h"
#include "shared/scheduler.h"
#include "systems/ps1/hw/cdrom.h"
#include "systems/ps1/hw/gpu.h"
#include "systems/ps1/hw/irq_control.h"
#include "systems/ps1/hw/spu.h"
#include "systems/ps1/hw/timers.h"
#include "systems/ps1/renderer.h"
#include "systems/ps1/hw/mmio_registry.h"
#include "systems/ps1/controllers/controller.h"
#include "systems/ps1/hw/mdec.h"

namespace zoo::ps1 {

class Console {
public:
  Console(Vulkan *);

  void step_instruction();
  void reset();
  guest::r3000::R3000 *cpu();
  fox::MemoryTable *memory();
  GPU *gpu();
  Renderer *renderer();
  IRQControl *irq_control();
  EventScheduler *scheduler();
  CDROM *cdrom();
  Timers *timers();
  SPU *spu();
  MDEC* mdec();

  u64 elapsed_cycles() const;
  u64 elapsed_nanos() const;

  void intercept_bios_calls();
  void schedule_event(u64 system_clocks, EventScheduler::Event *event);
  void schedule_event_nanos(u64 delta_nanos, EventScheduler::Event *event);

  void set_internal_pause(bool is_set);
  bool is_internal_pause_requested() const;

  void set_controller(u8 port, std::unique_ptr<Controller> controller);
  Controller *controller(u8 port);

  MMIORegistry *mmio_registry();

private:
  u64 m_cycles_elapsed = 0;
  bool m_internal_pause_requested = false;

  std::unique_ptr<fox::MemoryTable> m_mem_table;
  std::unique_ptr<guest::r3000::R3000> m_cpu;
  std::unique_ptr<GPU> m_gpu;
  std::unique_ptr<IRQControl> m_irq_control;
  std::unique_ptr<Renderer> m_renderer;
  std::unique_ptr<CDROM> m_cdrom;
  std::unique_ptr<SPU> m_spu;
  std::unique_ptr<Timers> m_timers;
  std::unique_ptr<MDEC> m_mdec;
  std::unique_ptr<MMIORegistry> m_mmio_registry;

  std::unique_ptr<Controller> m_controllers[2] = {};

  EventScheduler m_scheduler;

  u32 last_r3000_breakpoint = 0xffff'ffff;
};

}
