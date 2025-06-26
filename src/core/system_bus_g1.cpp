#include <fmt/core.h>
#include <map>
#include <algorithm>

#include "shared/profiling.h"
#include "shared/utils.h"
#include "core/console.h"
#include "system_bus_g1.h"

#if 1
#define DEBUG(args...) fmt::print(args)
#else
#define DEBUG(args...)
#endif

#define G1_DMA_PLOT_NAME "G1 DMA Bytes"

static const std::map<u32, const char *> register_map = {
  { 0x005f7404, "GD-DMA start address" },
  { 0x005f7408, "GD-DMA length" },
  { 0x005f740c, "GD-DMA direction" },

  { 0x005f7414, "GD-DMA enable" },
  { 0x005f7418, "GD-DMA start" },

  { 0x005f7480, "System ROM read access timing" },
  { 0x005f7484, "System ROM write access timing" },
  { 0x005f7488, "Flash ROM read access timing" },
  { 0x005f748c, "Flash ROM write access timing" },
  { 0x005f7490, "GD PIO read acess timing" },
  { 0x005f7494, "GD PIO write acess timing" },

  { 0x005f74a0, "GD-DMA read access timing" },
  { 0x005f74a4, "GD-DMA write access timing" },

  { 0x005f74b0, "System mode" },
  { 0x005f74b4, "G1IORDY signal control" },
  { 0x005f74b8, "GD-DMA address range" },

  { 0x005f74e4, "GD-DMA drive re-enable" },

  { 0x005f74f4, "GD-DMA address count (on Root Bus)" },
  { 0x005f74f8, "GD-DMA transfer counter" },
};

G1Bus::G1Bus(Console *const console)
  : m_console(console),
    m_memory(console->memory()),
    m_engine(new SyncFifoEngine<u32>("G1 Bus",
                                     std::bind(&G1Bus::engine_callback,
                                               this,
                                               std::placeholders::_1,
                                               std::placeholders::_2))),
    m_event_gdrom_dma("g1.gdrom-dma-complete",
                      std::bind(&G1Bus::finish_dma, this),
                      console->scheduler())
{
  return;
}

G1Bus::~G1Bus()
{
  m_event_gdrom_dma.cancel();
}

void
G1Bus::serialize(serialization::Snapshot &snapshot)
{
  static_assert(sizeof(regs) == 28);
  snapshot.add_range("g1.mmio_regs", sizeof(regs), &regs);

  static_assert(sizeof(m_dma) == 16);
  snapshot.add_range("g1.dma_state", sizeof(m_dma), &m_dma);
  
  m_event_gdrom_dma.serialize(snapshot);
}

void
G1Bus::deserialize(const serialization::Snapshot &snapshot)
{
  snapshot.apply_all_ranges("g1.mmio_regs", &regs);
  snapshot.apply_all_ranges("g1.dma_state", &m_dma);
  m_event_gdrom_dma.deserialize(snapshot);
}

void
G1Bus::reset()
{
  return;
}

void
G1Bus::register_regions(fox::MemoryTable *const memory)
{
  memory->map_mmio(0x005f7400u, 0x100u, "G1 Bus", this);
}

u8
G1Bus::read_u8(u32 address)
{
  const auto register_it = register_map.find(address & 0xfffffffcu);
  if (register_it != register_map.end()) {
    log.error("Read u8 from G1 Bus register '%s'", register_it->second);
  } else {
    DEBUG("Read u8 from unknown G1 Bus register @0x{:08x}\n", address);
  }

  switch (address) {
    /* SB_G1SYSM */
    case 0x005f74b0:
      /*
       * MSB four bits indicate the type of hardware:
       *   - Mass Production Unit (0x0)
       *   - SET4-8M              (0x6)
       *   - SET4-32M             (0x8)
       *   - DevBox-16M           (0x9)
       *   - DevBox-32M           (0xA)
       *   - Graphics Box         (0xD)
       *
       *
       * LSB four bits indicate the region:
       *   - Japan, South Korea, Asia NTSC    (0x1)
       *   - North America, Brazil, Argentina (0x4)
       *   - Europe                           (0xC)
       */
      return 0x94u;

    default:
      DEBUG("Read u8 from unknown G1 Bus address @0x{:08x}, returning 0\n", address);
      return 0x00u;
  }
}

u16
G1Bus::read_u16(u32 address)
{
  const auto register_it = register_map.find(address);
  if (register_it != register_map.end()) {
    log.verbose("Read u16 from G1 Bus register '%s'", register_it->second);
  } else {
    DEBUG("Read u16 from unknown G1 Bus register @x{:08x}\n", address);
  }

  return 0u;
}

u32
G1Bus::read_u32(u32 address)
{
  const auto register_it = register_map.find(address);
  if (register_it != register_map.end()) {
    log.verbose("Read u32 from G1 Bus register '%s'", register_it->second);
  } else {
    DEBUG("Read u32 from unknown G1 Bus register @x{:08x}\n", address);
  }

  switch (address) {
    case 0x005f74b0:
      return 0;

    case 0x005f74e4:
      return 0x000000ffu;

    case 0x005f7414:
      return regs.GD_DMA_ENABLE;

    case 0x005f7418:
      return regs.GD_DMA_START;

    case 0x005f74f4:
      return regs.GD_DMA_ADDRESS_COUNT;

    case 0x005f74f8:
      return regs.GD_DMA_TRANSFER_COUNTER;

    default:
      DEBUG("Read u32 from unknown address @x{:08x}, returning zero\n", address);
      break;
  }

  return 0u;
}

void
G1Bus::write_u8(u32 address, u8 value)
{
  const auto register_it = register_map.find(address);
  if (register_it != register_map.end()) {
    log.error(
      "Write u8 to G1 Bus register '%s' value 0x%02x", register_it->second, value);
  } else {
    DEBUG("Write u8 to unknown G1 Bus register @0x{:08x}\n", address);
    assert(false);
    printf("Write u8 to unknown G1 Bus register @0x%08x\n", address);
  }
}

void
G1Bus::write_u16(u32 address, u16 value)
{
  const auto register_it = register_map.find(address);
  if (register_it != register_map.end()) {
    log.error(
      "Write u16 to G1 Bus register '%s' value 0x%04x", register_it->second, value);
  } else {
    DEBUG("Write u16 to unknown G1 Bus register @0x{:08x}\n", address);
    assert(false);
    printf("Write u16 to unknown G1 Bus register @0x%08x\n", address);
  }
}

void
G1Bus::write_u32(u32 address, u32 value)
{
  switch (address) {
    case 0x005f7404:
      regs.GD_DMA_START_ADDRESS = value;
      break;

    case 0x005f7408:
      regs.GD_DMA_LENGTH = value;
      break;

    case 0x005f740c:
      regs.GD_DMA_DIRECTION = value;
      break;

    case 0x005f7414:
      regs.GD_DMA_ENABLE = value;
      break;

    case 0x005f7418: {
      if (!regs.GD_DMA_ENABLE || value != 1 || regs.GD_DMA_START) {
        break;
      }

      regs.GD_DMA_START = 1;
      regs.GD_DMA_TRANSFER_COUNTER = regs.GD_DMA_LENGTH / 32;

      log.debug("Got request to start GDROM DMA Transfer on G1, GDROM -> SH4[0x%08X], "
                "Length[0x%08X]",
                regs.GD_DMA_START_ADDRESS,
                regs.GD_DMA_LENGTH);

      m_dma.destination = regs.GD_DMA_START_ADDRESS;
      m_dma.length      = regs.GD_DMA_LENGTH;
      if (m_dma.length == 0) {
        /* Special case: length 0 == 32 MBytes */
        m_dma.length = 32 * 1024 * 1024;
      }

      m_engine->issue(address, 0);
      break;
    }

    case 0x005f74b8: {
      // This is used to setup which ranges are valid for DMA. If a DMA were initiated
      // in an invalid range, it should raise an exception. We make an assumption that
      // nothing depends on this.
      break;
    }

    case 0x005f74e4: {
      // No-op: Un-documented "re-enable disabled GDROM drive"
      break;
    }

    case 0x005f7484:
    case 0x005f7488:
    case 0x005f748c:
    case 0x005f7490:
    case 0x005f7494:
    case 0x005f74a0:
    case 0x005f74a4: {
      // Timing registers, don't care.
      break;
    }

    case 0x005f74b4: {
      // PIO Enable/Disable. If something is disabling this it might be weird..
      assert(value == 1);
      break;
    }

    case 0x005f74f4:
    case 0x005f74f8: {
      /* Read only */
      break;
    }

    default:
      log.error(
        "Write u32 to unknown G1 Bus register 0x%08x value 0x%08x", address, value);
      printf(
        "Write u32 to unknown G1 Bus register 0x%08x value 0x%08x\n", address, value);
      break;
  }
}

#ifdef TRACY_ENABLE
#define G1_FIBER "G1"
#define G1_DMA_ZONE "G1 DMA (0x005f7418)"
TracyCZoneCtx g1_dma_zone_ctx;
#endif

void
G1Bus::engine_callback(const u32 address, const u32 &value)
{
  switch (address) {
    /* G2 GDROM DMA request */
    case 0x005f7418: {
      log.debug("G1Bus GDROM-DMA started");
      // DEBUG("G1 DMA dest 0x{:08x} length 0x{:08x}\n", m_dma.destination, m_dma.length);

      /* DMA transfer should happen in units of 32 bytes */
      u8 buffer[32];
      unsigned remaining = m_dma.length;
      while (remaining > 0) {
        const u32 count = std::min(32u, remaining);

        m_console->gdrom()->trigger_dma_transfer(count, buffer);
        m_console->memory()->dma_write(0x1FFFFFFF & m_dma.destination, buffer, count);
        m_console->memory_usage().ram->set(0x0C00'0000 | (m_dma.destination & ~0xF000'0000), dreamcast::G1_DiscReadBuffer);
        remaining -= count;
        m_dma.destination += count;
      }

      /* TODO timing check */
      /* TODO If the callback is scheduled farther in the future (to be
       *      realistic) games stop booting. */

      /* Pg 315 : "Under the default settings, reading one word of data requires
       * 50 cycles (= 1000nsec)." Currently, we speed this up 4x to make the
       * emulator load games faster. */

#ifdef TRACY_ENABLE
      ProfilePushFiberZone(G1_FIBER, G1_DMA_ZONE, zone);
      g1_dma_zone_ctx = zone;
#endif

      // "The real transfer speed at the time of GD-ROM access is 10MB/s (2880ns/32B)."
      const u64 delay_nanos =  m_dma.length * 2880 / 32;
      m_dma.start_time      = m_console->current_time();
      m_console->schedule_event(delay_nanos, &m_event_gdrom_dma);

      break;
    }
  }
}

void
G1Bus::finish_dma()
{
  assert(regs.GD_DMA_START == 1);

  regs.GD_DMA_ADDRESS_COUNT += m_dma.length;
  regs.GD_DMA_TRANSFER_COUNTER = 0;

  ProfilePopFiberZone(G1_FIBER, g1_dma_zone_ctx);

  m_console->trace_zone(
    "G1 DMA", TraceTrack::G1, m_dma.start_time, m_console->current_time());

  /* We're done (GD_DMA_START is a status bit while DMA is happening) */
  regs.GD_DMA_START = 0;
  m_console->interrupt_normal(Interrupts::Normal::EndOfDMA_GD);
}
