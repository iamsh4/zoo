#include <fmt/printf.h>

#include "core/placeholder_mmio.h"
#include "systems/dragon/console.h"
#include "systems/dragon/hw/gpu.h"

namespace zoo::dragon {

// master clock cycles per second (100mhz CPU)
constexpr u64 CPU_HZ = 100 * 1000 * 1000;

// nanos per master clock
constexpr u64 NANOS_PER_CPU_CYCLE = u64(1000) * 1000 * 1000 / CPU_HZ;

// 32-bit address space
#define MAX_VIRTUAL_ADDRESS (u64(1) << 32)

#define MAX_PHYSICAL_ADDRESS 0x8000'0000

// 32MiB of addressable Main/System RAM
#define RAM_SIZE (32 * 1024 * 1024)

using RV32 = guest::rv32::RV32;

Console::Console(const char *bios_path)
  : m_mem_table(
      std::make_unique<fox::MemoryTable>(MAX_VIRTUAL_ADDRESS, MAX_PHYSICAL_ADDRESS)),
    m_cpu(std::make_unique<RV32>(m_mem_table.get()))
{
  // bram
  const u32 blockram_size = 32 * 1024;
  const auto bram_uncached =
    m_mem_table->map_shared(0x00000000u, blockram_size, "bram.uncached");
  m_mem_table->map_shared(0x40000000u, blockram_size, "bram.cached", bram_uncached, 0);

  // sysmem
  const auto sysmem_uncached =
    m_mem_table->map_shared(0x04000000u, RAM_SIZE, "mem.system.uncached");
  m_mem_table->map_shared(0x44000000u, RAM_SIZE, "mem.system.cached", sysmem_uncached, 0);

  // CPU-Internal Address Space
  m_mem_table->map_sdram(0x80000000, 0x1000, "cpu.scratch");
  m_mem_table->map_file(0x80001000, 0x1000, bios_path, 0);

  // GPU
  m_gpu = std::make_unique<GPU>(0x0c000000, this);
  m_mem_table->map_mmio(m_gpu->base_address(), GPU::MMIO_TOTAL_BYTES, "gpu", m_gpu.get());

  m_mem_table->finalize();

  // Return instruction at the beginning of bram
  m_mem_table->write<u32>(0, 0x00008067);

  // Reset to BIOS
  m_cpu->add_instruction_set<guest::rv32::RV32I>();
  m_cpu->add_instruction_set<guest::rv32::RV32M>();
  m_cpu->add_instruction_set<guest::rv32::RV32Zicsr>();
  m_cpu->add_instruction_set<guest::rv32::RV32Zicond>();
  m_cpu->set_reset_address(0x80001000);
}

void
Console::load_bin(const char *path)
{
  FILE *f = fopen(path, "rb");
  fread((void *)m_mem_table->root(), 1, 32 * 1024, f);
  fclose(f);
}

void
Console::schedule_event(u64 system_clocks, EventScheduler::Event *event)
{
  event->schedule(m_cycles_elapsed + system_clocks);
}

void
Console::schedule_event_nanos(u64 delta_nanos, EventScheduler::Event *event)
{
  const u64 delta_cycles = delta_nanos / NANOS_PER_CPU_CYCLE;
  event->schedule(m_cycles_elapsed + delta_cycles);
}

dragon::GPU *
Console::gpu()
{
  return m_gpu.get();
}

u64
Console::elapsed_nanos() const
{
  return m_cycles_elapsed * NANOS_PER_CPU_CYCLE;
}

void
Console::step_instruction()
{
  try {
    const u64 cpu_cycles = m_cpu->step();
    m_cycles_elapsed += cpu_cycles;
    m_scheduler.run_until(m_cycles_elapsed);
  } catch (...) {
    //q
    throw;
  }
}

void
Console::set_internal_pause(bool is_set)
{
  m_internal_pause_requested = is_set;
  // m_cpu->m_halted = is_set;
}

bool
Console::is_internal_pause_requested() const
{
  return m_internal_pause_requested /*|| m_cpu->m_halted */;
}

guest::rv32::RV32 *
Console::cpu()
{
  return m_cpu.get();
}

fox::MemoryTable *
Console::memory()
{
  return m_mem_table.get();
}

EventScheduler *
Console::scheduler()
{
  return &m_scheduler;
}

// MMIORegistry *
// Console::mmio_registry()
// {
//   return m_mmio_registry.get();
// }

void
Console::reset()
{
  m_cycles_elapsed = 0;
  m_cpu->reset();
  m_gpu->reset();
}

// void
// Console::set_controller(u8 port, std::unique_ptr<Controller> controller)
// {
//   assert(port < 2);
//   m_controllers[port] = std::move(controller);
// }

// Controller *
// Console::controller(u8 port)
// {
//   assert(port < 2);
//   return m_controllers[port].get();
// }

u64
Console::elapsed_cycles() const
{
  return m_cycles_elapsed;
}

}
