#include <fstream>
#include <fmt/core.h>

#include <fcntl.h>
// #include <libelf/libelf.h>

#include "core/console.h"
#include "shared/log.h"
#include "shared/profiling.h"
#include "shared/stopwatch.h"
#include "shared/file.h"

#include "core/placeholder_mmio.h"
#include "core/system_bus_mmio.h"
#include "core/system_bus_g1.h"
#include "core/system_bus_g2.h"
#include "gpu/holly.h"
#include "gpu/texture_fifo.h"
#include "gpu/texture_manager.h"
#include "apu/aica.h"

#include "peripherals/maple.h"
#include "peripherals/modem.h"
#include "peripherals/gdrom.h"
#include "serialization/session.h"
#include "serialization/storage.h"

#include "media/chd_disc.h"
#include "media/gdi_disc.h"
#include "peripherals/region_free_dreamcast_disc.h"

constexpr u64 MAX_VIRTUAL_ADDRESS  = 0x100000000u;
constexpr u64 MAX_PHYSICAL_ADDRESS = 0x20000000u;

Console::Console(std::shared_ptr<zoo::local::Settings> settings,
                 apu::Audio *const audio,
                 zoo::dreamcast::Renderer *renderer)
  : m_settings(settings),
    m_renderer(renderer),
    m_elapsed_nanos(0lu),
    m_mem(new fox::MemoryTable(MAX_VIRTUAL_ADDRESS, MAX_PHYSICAL_ADDRESS)),
    m_sh4(new cpu::SH4(this)),
    m_holly(new gpu::Holly(this)),
    m_sys_bus(new SystemBus(this)),
    m_g1_bus(new G1Bus(this)),
    m_g2_bus(new G2Bus(this)),
    m_maple(new Maple(this)),
    m_gdrom(new GDRom(this)),
    m_aica_rtc(new apu::RTC(this)),
    m_aica(new apu::AICA(this, audio)),
    m_texture_manager(new gpu::TextureManager(this))
{
  _memory_usage.ram = std::make_unique<MemoryPageData<dreamcast::MemoryUsage>>(
    0x0C00'0000, 16 * 1024 * 1024, 128);
  _memory_usage.vram = std::make_unique<MemoryPageData<dreamcast::MemoryUsage>>(
    0x0500'0000, 8 * 1024 * 1024, 128);
  _memory_usage.waveram = std::make_unique<MemoryPageData<dreamcast::MemoryUsage>>(
    0x0080'0000, 2 * 1024 * 1024, 128);

  /* Console memory is 16MiB and mapped four times in the physical address
   * space. */
  {
    const auto sysmem = m_mem->map_shared(0x0C000000u, 0x01000000u, "mem.system");
    m_mem->map_shared(
      0x0D000000u, 0x01000000u, "System Memory Mirror 1", sysmem, 0x00000000u);
    m_mem->map_shared(
      0x0E000000u, 0x01000000u, "System Memory Mirror 2", sysmem, 0x00000000u);
    m_mem->map_shared(
      0x0F000000u, 0x01000000u, "System Memory Mirror 3", sysmem, 0x00000000u);

    /* XXX hack to make it visible in the upper 4GiB section. Don't
     *     include the ones in the 0xFF000000 range as those are CPU
     *     registers. */
    for (size_t i = 1; i < 8; ++i) {
      u32 offset = i * 0x20000000u;
      m_mem->map_shared(
        0x0C000000u + offset, 0x01000000u, "System Memory Mirror 0", sysmem, 0x00000000u);
      m_mem->map_shared(
        0x0D000000u + offset, 0x01000000u, "System Memory Mirror 1", sysmem, 0x00000000u);
      m_mem->map_shared(
        0x0E000000u + offset, 0x01000000u, "System Memory Mirror 2", sysmem, 0x00000000u);

      if (i == 7) {
        break;
      }

      m_mem->map_shared(
        0x0F000000u + offset, 0x01000000u, "System Memory Mirror 3", sysmem, 0x00000000u);
    }
  }

  const auto vram_32 = m_mem->map_shared(
    0x0500'0000, 0x0080'0000, "PVR-IF Texture 32b Memory Access 0x0500_0000");
  m_mem->map_shared(
    0x0700'0000, 0x0080'0000, "PVR-IF Texture 32b Memory Access 0x0700_0000", vram_32, 0);

  // AICA/ARM7 Wave memory (2MiB)
  const auto mem_aica_shared = m_mem->map_shared(0x00800000u, 0x00200000u, "mem.aica");
  m_mem->map_shared(
    0x00a00000u, 0x00200000u, "AICA Memory Mirror 1", mem_aica_shared, 0x00000000u);
  m_mem->map_shared(
    0x00c00000u, 0x00200000u, "AICA Memory Mirror 2", mem_aica_shared, 0x00000000u);
  m_mem->map_shared(
    0x00e00000u, 0x00200000u, "AICA Memory Mirror 3", mem_aica_shared, 0x00000000u);
  m_mem->map_sdram(0x01000000u, 0x01000000u, "G2 External Area");

  // Map the BIOS
  const std::filesystem::path bios_path =
    m_settings->get_or_default("dreamcast.bios_path", "");
  if (!std::filesystem::exists(bios_path)) {
    fprintf(stderr,
            "BIOS file '%s' not found. Run 'make' from the firmware repo directory"
            " or setup the file manually.\n",
            bios_path.c_str());
    exit(1);
  }
  m_mem->map_file(0x00000000u, 0x00200000u, bios_path.c_str(), 0);

  // Map the FlashROM
  const std::filesystem::path flashrom_path =
    m_settings->get_or_default("dreamcast.flashrom_path", "");
  m_flashrom = new FlashROM(this, flashrom_path);

  /* Connect the hardware devices */
  mmio_devices.emplace(new peripherals::Modem());
  mmio_devices.emplace(m_sys_bus);
  mmio_devices.emplace(m_g1_bus);
  mmio_devices.emplace(m_g2_bus);
  mmio_devices.emplace(m_gdrom);
  mmio_devices.emplace(m_maple);
  mmio_devices.emplace(m_holly);
  mmio_devices.emplace(m_aica_rtc);
  mmio_devices.emplace(m_aica);
  mmio_devices.emplace(m_flashrom);

  /* Dummy Devices */
  mmio_devices.emplace(new PlaceholderMMIO("PVR Control", 0x5f7C00u, 0x5F7D00u));

  for (const auto &device : mmio_devices) {
    device->register_regions(m_mem.get());
  }

  /* Apply memory configuration */
  m_mem->finalize();

  /* Reset all components to power-on emulation state. */
  power_reset();

  // m_sh4->set_sampling_profiler_running(true);
}

Console::~Console()
{
  if (m_aica) {
    m_aica->shutdown();
  }
}

void
Console::power_reset()
{
  m_sh4->reset();
  m_aica->reset();
  m_g1_bus->reset();
  m_g2_bus->reset();
  m_holly->reset();
  m_gdrom->reset();
  m_maple->reset();
  m_sys_bus->reset();

  m_elapsed_nanos = 0;

  trace_event("PowerReset", TraceTrack::Console, current_time());
}

static const u64 NANOS_PER_CYCLE = 5;

void
Console::debug_run_single_block()
{
  m_elapsed_nanos += m_sh4->step_block() * NANOS_PER_CYCLE;
  m_scheduler.run_until(m_elapsed_nanos);
}

void
Console::trace_zone(std::string_view name,
                    TraceTrack track,
                    u64 start_nanos,
                    u64 end_nanos)
{
  if (m_trace) {
    m_trace->zone(u32(track), start_nanos, end_nanos, name);
  }
}

void
Console::trace_event(const std::string_view name, TraceTrack track, u64 nanos)
{
  if (m_trace) {
    m_trace->instant(u32(track), nanos, name);
  }
}

void
Console::debug_step_single_block(const u64 stop_nanos)
{
  assert(stop_nanos > m_elapsed_nanos);

  /* The effective elapsed cycles cannot be updated until the end of the block.
   * In a JIT backend it would not be updated during block execution. Similarly
   * interrupts can only be serviced on the first instruction. */
  u64 elapsed_nanos = m_elapsed_nanos;

  m_sh4->debug_enable(true);
  m_sh4->debug_mask_interrupts(false);
  elapsed_nanos += m_sh4->step() * NANOS_PER_CYCLE;

  /* Run remaining cycles for this emulated block. */
  m_sh4->debug_mask_interrupts(true);
  while (elapsed_nanos < stop_nanos) {
    elapsed_nanos += m_sh4->step() * NANOS_PER_CYCLE;
  }

  m_elapsed_nanos = elapsed_nanos;
  m_scheduler.run_until(m_elapsed_nanos);
}

void
Console::debug_step()
{
  m_elapsed_nanos += m_sh4->step() * NANOS_PER_CYCLE;
  m_scheduler.run_until(m_elapsed_nanos);
}

void
Console::debug_step_back(serialization::Session &session)
{
#if 0
  const u64 cycle_at_entrance  = m_elapsed_cycles;
  const u32 sh4_pc_at_entrance = *m_sh4->pc_register_pointer();

  // Get the latest snapshot before now
  const auto latest_snapshot = session.get_latest_snapshot(cycle_at_entrance - 1);
  if (!latest_snapshot) {
    // TODO : log
    return;
  }

  load_state(*latest_snapshot);
  const u64 cycles_after_load = m_elapsed_cycles;
  const u64 skip_back         = 10;

  if (cycle_at_entrance <= cycles_after_load + skip_back) {
    // There is less than skip_back cycles to run.
    fmt::print(
      "Less than {} cycles remaining between snapshot and now. Not going forward.\n");
    return;
  } else {
    const u64 cycles_to_run = cycle_at_entrance - cycles_after_load - skip_back;
    run_for(std::chrono::nanoseconds(cycles_to_run * MASTER_CLOCK_INTERVAL));
  }
  fmt::print("Finished stepping back. Current cycles = {}\n", m_elapsed_cycles);
#endif
  throw std::runtime_error("Not implemented");
}

void
Console::run_for(std::chrono::duration<u64, std::nano> nanoseconds_to_run)
{
  if (nanoseconds_to_run.count() == 0)
    return;

  /* 200 Mhz, but this is not really accurate. */

  const bool running_interpreter_mode =
    cpu()->get_execution_mode() == cpu::SH4::ExecutionMode::Interpreter;

  const u64 target_time = m_elapsed_nanos + nanoseconds_to_run.count();

  while (m_elapsed_nanos < target_time) {
    /* Run devices (CPU, video, etc.) until the next_checkpoint, or until
     * something else should run which was scheduled sooner. */
    u64 next_checkpoint = std::min(target_time, m_scheduler.next_timestamp());

    while (m_elapsed_nanos < next_checkpoint) {
      // const u64 sh4_nanos_start = epoch_nanos();
      if (m_sh4->is_debug_enabled() || running_interpreter_mode) {
        m_elapsed_nanos += m_sh4->step() * NANOS_PER_CYCLE;
      } else {
        m_elapsed_nanos += m_sh4->step_block() * NANOS_PER_CYCLE;
      }

      // const u64 sh4_nanos_elapsed = epoch_nanos() - sh4_nanos_start;
      // m_metrics.increment(zoo::dreamcast::Metric::NanosSH4, sh4_nanos_elapsed);

      // Update in case the last instruction(s) executed updated scheduler
      next_checkpoint = std::min(next_checkpoint, m_scheduler.next_timestamp());
    }

    m_scheduler.run_until(m_elapsed_nanos);
  }
}

void
Console::schedule_event(const u64 delta_nanos, EventScheduler::Event *const event)
{
  event->schedule(m_elapsed_nanos + delta_nanos);
}

void
Console::open_disc_drive()
{
  m_gdrom->open_drive();
}

void
Console::close_disc_drive()
{
  m_gdrom->close_drive();
}

void
Console::load_elf(const std::string &elf_path)
{
  // We need to mimic system boot. First, load 'boot.ram.bin' into memory.
  FILE *boot_ram = fopen("boot.ram.bin", "rb");
  if (!boot_ram) {
    printf("Failed to open boot.ram.bin\n");
    return;
  }
  fseek(boot_ram, 0, SEEK_END);
  size_t boot_ram_size = ftell(boot_ram);
  fseek(boot_ram, 0, SEEK_SET);
  m_mem->dma_write(0x0c00'0000, boot_ram, boot_ram_size);

  // Load each section from the ELF file into memory.
#if 0
  Elf *elf = NULL;
  Elf_Scn *scn = NULL;
  Elf_Data *data = NULL;
  Elf32_Shdr *shdr = NULL;
  char *name = NULL;
  int fd_in = 0, fd_out = 0;
  size_t shstrndx = 0, n = 0;

  elf_version(EV_CURRENT);
  fd_in = open(elf_path.c_str(), O_RDONLY);
  elf = elf_begin(fd_in, ELF_C_READ, NULL);

  // Retrieve the index of the section name string table
  elf_getshdrstrndx(elf, &shstrndx);

  while ((scn = elf_nextscn(elf, scn)) != NULL) {
    data = nullptr;

    // Retrieve the section header
    shdr = elf32_getshdr(scn);
    if (!shdr) {
      continue;
    }

    // Get the name of the section
    if (shdr->sh_flags & (SHF_ALLOC | SHF_EXECINSTR) && shdr->sh_size > 0) {
      name = elf_strptr(elf, shstrndx, shdr->sh_name);
      printf("Loading section '%s' [0x%08x, 0x%08x] from elf file offset 0x%08x\n",
             name,
             shdr->sh_addr,
             shdr->sh_addr + shdr->sh_size,
             shdr->sh_offset);

      // Read the section and write it to the file
      u8 *dest = (u8 *)m_mem->root() + shdr->sh_addr;
      while ((data = elf_getdata(scn, data)) != nullptr && data->d_buf) {
        memcpy(dest, data->d_buf, data->d_size);
        dest += data->d_size;
      }
    }
  }

  elf_end(elf);
  close(fd_in);
#endif

  // Setup register state
  cpu::SH4::Registers registers;
  registers.clear();
  registers.PC  = 0x8c01'0000;
  registers.GBR = 0x8c00'0000;
  registers.VBR = 0x8c00'f400;
  registers.PR  = 0xac00'e0b2;
  registers.SPC = 0x8c00'077a;
  registers.SGR = 0x7e00'0fc4;
  m_sh4->set_registers(registers);

  // BIOS function pointer table setup
  uint32_t const tbl[][2] = {
    { 0x8c0000c0, 0x8c0010f0 }, { 0x8c0000bc, 0x8c001000 }, { 0x8c0000b8, 0x8c003d00 },
    { 0x8c0000b4, 0x8c003b80 }, { 0x8c0000b0, 0x8c003c00 }, { 0x8c0000ac, 0xa05f7000 },
    { 0x8c0000a8, 0xa0200000 }, { 0x8c0000a4, 0xa0100000 }, { 0x8c0000a0, 0x00000000 },
    { 0x8c00002e, 0x00000000 }, { 0x8c0000e0, 0x8c000800 }, { 0x8cfffff8, 0x8c000128 },
  };
  for (auto const &entry : tbl) {
    if (entry[0] % 4 == 0)
      m_mem->write<u32>(entry[0], entry[1]);
    else {
      m_mem->write<u16>(entry[0], entry[1]);
      // m_mem->write<u16>(entry[0] + 2, 0);
    }
  }
}

static bool
should_serdes(fox::MemoryRegion *region)
{
  return region && region->name.rfind("mem.", 0) == 0;
}

void
Console::save_state(serialization::Snapshot &snapshot)
{
  for (const auto region : m_mem->regions()) {
    if (should_serdes(region)) {
      const u32 region_length = region->phys_end - region->phys_offset + 1;
      const u8 *data          = m_mem->root() + region->phys_offset;
      snapshot.add_range(region->name, region->phys_offset, region_length, data);
    }
  }

  m_aica->serialize(snapshot);      
  m_sh4->serialize(snapshot);       
  m_g1_bus->serialize(snapshot);    
  m_g2_bus->serialize(snapshot);    
  m_holly->serialize(snapshot);     
  m_sys_bus->serialize(snapshot);   
  m_texture_manager->serialize(snapshot);
  m_aica_rtc->serialize(snapshot);  
  m_flashrom->serialize(snapshot);  
  m_gdrom->serialize(snapshot);     

  snapshot.add_range("console.elapsed_nanos", sizeof(m_elapsed_nanos), &m_elapsed_nanos);
}

void
Console::load_state(const serialization::Snapshot &snapshot)
{
  using Range                  = serialization::Storage::Range;
  const auto dma_from_snapshot = [&](const Range *range) {
    // printf("dma_from_snapshot : start_addr 0x%08x length %u\n", range->start_address, range->length);
    m_mem->dma_write(range->start_address, range->data, range->length);
  };

  snapshot.apply_all_ranges("mem.system", dma_from_snapshot);
  snapshot.apply_all_ranges("mem.aica", dma_from_snapshot);
  snapshot.apply_all_ranges("console.elapsed_nanos", &m_elapsed_nanos);

  m_aica->deserialize(snapshot);
  m_sh4->deserialize(snapshot);
  m_g1_bus->deserialize(snapshot);
  m_g2_bus->deserialize(snapshot);
  m_holly->deserialize(snapshot);
  m_sys_bus->deserialize(snapshot);
  m_texture_manager->deserialize(snapshot);
  m_aica_rtc->deserialize(snapshot);
  m_flashrom->deserialize(snapshot);
  m_gdrom->deserialize(snapshot);

  // Need to dump audio queue to keep host audio in sync with new state
  m_aica->output()->clear_queued_samples();
}

void
Console::dump_ram(const std::string_view &file_path, u32 address, u32 length)
{
  std::ofstream ofs;
  ofs.open(file_path.data(),
           std::ofstream::out | std::ofstream::binary | std::ofstream::trunc);

  const auto write_region = [&](u32 address, u32 length) {
    const u8 *const root      = m_mem->root();
    const u8 *const mem_start = root + address;
    ofs.write((char *)mem_start, length);
  };

  write_region(address, length);
}
