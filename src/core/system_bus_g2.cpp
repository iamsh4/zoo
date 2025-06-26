#include <fmt/core.h>
#include <map>
#include <algorithm>

#include "shared/profiling.h"
#include "core/system_bus_g2.h"
#include "core/console.h"

#if 1
#define DEBUG(args...) fmt::print(args)
#else
#define DEBUG(args...)
#endif

static const std::map<u32, const char *> register_map = {
#define G2_REG(reg_name, reg_addr, dma_channel, description) { reg_addr, description },
#include "system_bus_g2_regs.inc.h"
#undef G2_REG
};

G2Bus::G2Bus(Console *const console)
  : m_console(console),
    m_memory(console->memory()),
    m_engine(new SyncFifoEngine<u32>("G2 Bus",
                                     std::bind(&G2Bus::engine_callback,
                                               this,
                                               std::placeholders::_1,
                                               std::placeholders::_2))),
    m_event_aica_dma("g2.aica-dma-complete",
                     std::bind(&G2Bus::finish_aica_dma, this),
                     console->scheduler())
{
  reset();
}

G2Bus::~G2Bus()
{
  return;
}

void
G2Bus::serialize(serialization::Snapshot &snapshot)
{
  static_assert(sizeof(dma_registers) == 128);
  snapshot.add_range("g2.dma_channel_states", sizeof(dma_registers), &dma_registers[0]);
  
  m_event_aica_dma.serialize(snapshot);
}

void
G2Bus::deserialize(const serialization::Snapshot &snapshot)
{
  snapshot.apply_all_ranges("g2.dma_channel_states", &dma_registers[0]);
  m_event_aica_dma.deserialize(snapshot);
}

void
G2Bus::reset()
{
  memset(dma_registers, 0, sizeof(dma_registers));
}

void
G2Bus::register_regions(fox::MemoryTable *const memory)
{
  memory->map_mmio(0x005f7800u, 0x100u, "G2 Bus", this);
}

u8
G2Bus::read_u8(u32 address)
{
  const auto register_it = register_map.find(address & 0xfffffffcu);
  if (register_it != register_map.end()) {
    log.error("Read u8 from G2 Bus register '%s'", register_it->second);
  } else {
    DEBUG("Read u8 from unknown G2 Bus register @0x{:08x}\n", address);
  }

  switch (address) {
    default:
      DEBUG("Read u8 from unknown G2 Bus address @0x{:08x}, returning 0\n", address);
      return 0x00u;
  }
}

u16
G2Bus::read_u16(u32 address)
{
  const auto register_it = register_map.find(address);
  if (register_it != register_map.end()) {
    log.error("Read u16 from G2 Bus register '%s'", register_it->second);
  } else {
    DEBUG("Read u16 from unknown G2 Bus register @0x{:08x}\n", address);
  }

  return 0u;
}

u32
G2Bus::read_u32(u32 address)
{
  const u32 dma_channel = (address >> 5) & 3;

  switch (address) {

    case RegAddress::SB_ADSTAG:
    case RegAddress::SB_E1STAG:
    case RegAddress::SB_E2STAG:
    case RegAddress::SB_DDSTAG:
      log.verbose("Read u32 from G2 Bus register 0x%08x -> 0x%08x", address, dma_registers[dma_channel].STAG);
      return dma_registers[dma_channel].STAG;
      break;

    case RegAddress::SB_ADST:
    case RegAddress::SB_E1ST:
    case RegAddress::SB_E2ST:
    case RegAddress::SB_DDST:
      log.verbose("Read u32 from G2 Bus register 0x%08x -> 0x%08x", address, dma_registers[dma_channel].ST);
      return dma_registers[dma_channel].ST;
      break;

    case RegAddress::SB_ADEN:
    case RegAddress::SB_E1EN:
    case RegAddress::SB_E2EN:
    case RegAddress::SB_DDEN:
      log.verbose("Read u32 from G2 Bus register 0x%08x -> 0x%08x", address, dma_registers[dma_channel].EN);
      return dma_registers[dma_channel].EN;
      break;

    case 0x005F7880: // SB_G2ID
      log.verbose("Read u32 from SB_G2ID", address, 0x00000000);
      return 0x00000012;

    default:
      log.error("Read u32 from unknown G2 Bus register @0x%08x", address);
      break;
  }

  return 0u;
}

void
G2Bus::write_u8(u32 address, u8 value)
{
  const auto register_it = register_map.find(address);
  if (register_it != register_map.end()) {
    log.error(
      "Write u8 to G2 Bus register '%s' value 0x%02x", register_it->second, value);
  } else {
    log.error("Write u8 to unknown G2 Bus register @x0%08x", address);
  }
}

void
G2Bus::write_u16(u32 address, u16 value)
{
  const auto register_it = register_map.find(address);
  if (register_it != register_map.end()) {
    log.error(
      "Write u16 to G2 Bus register '%s' value 0x%04x", register_it->second, value);
  } else {
    log.error("Write u16 to unknown G2 Bus register @x0%08x", address);
  }
}

inline bool
in_range(u32 val, const u32 min_val, const u32 max_val)
{
  return val >= min_val && val <= max_val;
}

/* Validates if the start DMA address provided is valid. */
inline bool
valid_stag_range(u32 value, u32 channel)
{
  if (channel == 0) {
    return in_range(value, 0x00700000, 0x00707FE0) ||
           in_range(value, 0x00800000, 0x009FFFE0) ||
           in_range(value, 0x02700000, 0x02FFFFE0);
  } else {
    return in_range(value, 0x01000000, 0x01FFFFE0) ||
           in_range(value, 0x03000000, 0x03FFFFE0) ||
           in_range(value, 0x14000000, 0x17FFFFE0);
  }
}

void
G2Bus::write_u32(u32 address, u32 value)
{
  const u32 dma_channel = (address >> 5) & 3;

  switch (address) {
    case RegAddress::SB_ADSTAG:
    case RegAddress::SB_E1STAG:
    case RegAddress::SB_E2STAG:
    case RegAddress::SB_DDSTAG:
      dma_registers[dma_channel].STAG = value & 0x1FFFFFF0;
      break;

    case RegAddress::SB_ADSTAR:
    case RegAddress::SB_E1STAR:
    case RegAddress::SB_E2STAR:
    case RegAddress::SB_DDSTAR:
      dma_registers[dma_channel].STAR = value & 0x1FFFFFF0;
      break;

    case RegAddress::SB_ADLEN:
    case RegAddress::SB_E1LEN:
    case RegAddress::SB_E2LEN:
    case RegAddress::SB_DDLEN:
      dma_registers[dma_channel].LEN = value & 0x1FFFFFF0;
      break;

    case RegAddress::SB_ADDIR:
    case RegAddress::SB_E1DIR:
    case RegAddress::SB_E2DIR:
    case RegAddress::SB_DDDIR:
      dma_registers[dma_channel].DIR = value & 1;
      break;

    case RegAddress::SB_ADTSEL:
    case RegAddress::SB_E1TSEL:
    case RegAddress::SB_E2TSEL:
    case RegAddress::SB_DDTSEL:
      // TODO : Handle changing triggering mechanisms
      dma_registers[dma_channel].TSEL = value & 7;
      break;

    case RegAddress::SB_ADEN:
    case RegAddress::SB_E1EN:
    case RegAddress::SB_E2EN:
    case RegAddress::SB_DDEN:
      // TODO : Forcibly terminate any ongoing DMA
      dma_registers[dma_channel].EN = value & 1;
      break;

    case RegAddress::SB_ADST:
    case RegAddress::SB_E1ST:
    case RegAddress::SB_E2ST:
    case RegAddress::SB_DDST:
      // TODO : Forcibly terminate any ongoing DMA
      dma_registers[dma_channel].ST |= value & 1;

      if (dma_registers[dma_channel].ST && dma_registers[dma_channel].EN) {
        log.info("Write to G2 Start-DMA (Channel %u) triggering G2-AICA DMA",
                 dma_channel);
        m_engine->issue(address, dma_registers[dma_channel].LEN);
      }
      break;

#if 0 /* What even is this? I couldnt' find it in the dreamcast docs? */
    case 0x005F781C:
      // bit0=0 -> Resume/Start DMA
      if (SB_ADEN && (SB_ADSUSP&1) == 0) {
        log.info("Write to SB_SUSP triggering G2-AICA DMA");
        m_fifo.write(0x005F781C, SB_ADLEN);
      }
#endif

      break;

    default:
      log.error("Write u32 to unknown G2 Bus register @x%08x value 0x%08x", address, value);
      break;
  }
}

void
G2Bus::engine_callback(const u32 address, const u32 &value)
{
  const u32 dma_channel = (address >> 5) & 3;

  switch (address) {
      /* G2 AICA DMA request */
    case RegAddress::SB_ADST:
    case RegAddress::SB_E1ST:
    case RegAddress::SB_E2ST:
    case RegAddress::SB_DDST: {

      log.debug("G2Bus AICA-DMA started");
      unsigned length = value & 0x01FFFFE0;

      /* Special case: length 0 == 32 MBytes */
      if (length == 0) {
        length = 32 * 1024 * 1024;
      }

      /* TODO : We only handle the case of System -> AICA for now. */
      if (dma_channel == 0 && dma_registers[dma_channel].DIR == 1) {
        printf("Unhandled DMA from G2 -> Host Memory");
        assert(0);
        dma_registers[dma_channel].EN = 1u - (dma_registers[dma_channel].LEN >> 31);
        dma_registers[dma_channel].ST = 0;
        m_console->interrupt_normal(
          Interrupts::Normal::get_end_of_dma_for_g2_channel(dma_channel));
        break;
      }

      if (dma_channel == 0 && dma_registers[dma_channel].DIR == 0) {
        // DEBUG("Transfer G2 -> AICA, src 0x{:08x} -> 0x{:08x} -- length 0x{:08x}\n",
        //       dma_registers[dma_channel].STAR,
        //       dma_registers[dma_channel].STAG,
        //       length);
      }

      ProfileMessage("G2 AICA DMA Start");
      ProfileZoneNamed("G2 AICA DMA on some channel");

      /* DMA transfer should happen in units of 32 bytes */
      u8 buffer[32];
      while (length > 0) {
        const u32 count = std::min(32u, length);

        m_console->memory()->dma_read(
          buffer, 0x1FFFFFE0 & dma_registers[dma_channel].STAR, count);
        m_console->memory()->dma_write(dma_registers[dma_channel].STAG, buffer, count);

        m_console->memory_usage().ram->set(0x0C00'0000 | (dma_registers[dma_channel].STAG & ~0xF000'0000), dreamcast::G2_AICA_DMA);

        length -= count;
        dma_registers[dma_channel].STAR += count;
        dma_registers[dma_channel].STAG += count;
      }

      const u64 now = m_console->current_time();
      m_console->trace_zone("G2 DMA", TraceTrack::G2, now, now + 2000);
      m_console->schedule_event(2000, &m_event_aica_dma);
      break;
    }

    default:
      DEBUG("Unandled callback address 0x{:08x}, value {}\n", address, value);
      break;
  }
}

void
G2Bus::finish_aica_dma()
{
  const unsigned dma_channel = 0;

  // If DMA Restart is enabled (ADLEN_31), then when we complete DMA, enable again.
  dma_registers[dma_channel].EN = 1u - (dma_registers[dma_channel].LEN >> 31);

  /* We're done (Remember, this is a status bit while DMA is happening) */
  dma_registers[dma_channel].ST = 0;
  dma_registers[dma_channel].LEN = 0;

  m_console->interrupt_normal(
    Interrupts::Normal::get_end_of_dma_for_g2_channel(dma_channel));
}
