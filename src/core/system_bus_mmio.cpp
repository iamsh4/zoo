#include <fmt/core.h>
#include <map>

#include "core/console.h"
#include "core/system_bus_mmio.h"
#include "shared/log.h"

#if 1
#define DEBUG(args...) fmt::print(args)
#else
#define DEBUG(args...)
#endif

static const std::map<u32, const char *> register_map = {
  { 0x005f6800, "ch2-DMA destination address" },
  { 0x005f6804, "ch2-DMA length" },
  { 0x005f6808, "ch2-DMA start" },

  { 0x005f6810, "Sort-DMA start link table address" },
  { 0x005f6814, "Sort-DMA link base address" },
  { 0x005f6818, "Sort-DMA link address bit width" },
  { 0x005f681c, "Sort-DMA link address shift control" },
  { 0x005f6820, "Sort-DMA start" },

  { 0x005f6840, "DBREQ# signal mask control" },
  { 0x005f6844, "BAVL# signal wait count" },
  { 0x005f6848, "DMA (TA/Root Bus) priority count" },
  { 0x005f684c, "ch2-DMA maximum burst length" },

  { 0x005f6880, "TA FIFO remaining amount" },
  { 0x005f6884, "Via TA texture memory bus select 0" },
  { 0x005f6888, "Via TA texture memory bus select 1" },
  { 0x005f688c, "FIFO status" },
  { 0x005f6890, "System reset" },

  { 0x005f689c, "System bus revision number" },
  { 0x005f68a0, "SH4 Root Bus split enable" },

  { 0x005f6900, "Normal interrupt status" },
  { 0x005f6904, "External interrupt status" },
  { 0x005f6908, "Error interrupt status" },

  { 0x005f6910, "Level 2 normal interrupt mask" },
  { 0x005f6914, "Level 2 external interrupt mask" },
  { 0x005f6918, "Level 2 error interrupt mask" },

  { 0x005f6920, "Level 4 normal interrupt mask" },
  { 0x005f6924, "Level 4 external interrupt mask" },
  { 0x005f6928, "Level 4 error interrupt mask" },

  { 0x005f6930, "Level 6 normal interrupt mask" },
  { 0x005f6934, "Level 6 external interrupt mask" },
  { 0x005f6938, "Level 6 error interrupt mask" },

  { 0x005f6940, "Normal interrupt PVR-DMA trigger mask" },
  { 0x005f6944, "External interrupt PVR-DMA trigger mask" },

  { 0x005f6950, "Normal interrupt G2-DMA trigger mask" },
  { 0x005f6954, "External interrupt G2-DMA trigger mask" },
};

SystemBus::SystemBus(Console *console)
  : m_console(console),
    cpu(console->cpu()),
    memory(console->memory()),
    m_engine(new SyncFifoEngine<u32>("System Bus",
                                     std::bind(&SystemBus::engine_callback,
                                               this,
                                               std::placeholders::_1,
                                               std::placeholders::_2)))
{
  reset();
}

SystemBus::~SystemBus()
{
  return;
}

void
SystemBus::serialize(serialization::Snapshot &snapshot)
{
  std::array<u32, (size_t)Regs::N_REGISTERS> data;
  for (unsigned i = 0; i < (size_t)Regs::N_REGISTERS; ++i)
    data[i] = regs[i].load();
  snapshot.add_range(
    "sysbus.mmio_regs", sizeof(u32) * (size_t)Regs::N_REGISTERS, &data[0]);
}

void
SystemBus::deserialize(const serialization::Snapshot &snapshot)
{
  std::array<u32, (size_t)Regs::N_REGISTERS> data;
  snapshot.apply_all_ranges("sysbus.mmio_regs", &data);
  for (unsigned i = 0; i < (size_t)Regs::N_REGISTERS; ++i)
    regs[i] = data[i];
}

void
SystemBus::reset()
{
  for (unsigned i = 0; i < (size_t)Regs::N_REGISTERS; ++i)
    regs[i] = 0;
}

void
SystemBus::register_regions(fox::MemoryTable *const memory)
{
  memory->map_mmio(0x005f6000u, 0xA00u, "ASIC Bus", this);
}

void
SystemBus::raise_int_normal(Interrupts::Normal::Type id)
{
  const u32 bitmask = (1u << id);

  log.debug("SystemBus interrupt from device id=%u, bitmask now %08x",
            id,
            reg(Regs::SB_ISTNRM) | bitmask);

  reg(Regs::SB_ISTNRM) |= bitmask;
  recalculate_irqs();
}

void
SystemBus::raise_int_external(Interrupts::External::Type id)
{
  const u32 bitmask = (1u << id);

  reg(Regs::SB_ISTEXT) |= bitmask;
  recalculate_irqs();
}

void
SystemBus::raise_int_error(Interrupts::Error::Type id)
{
  const u32 bitmask = (1u << id);

  reg(Regs::SB_ISTERR) |= bitmask;
  recalculate_irqs();
}

void
SystemBus::drop_int_external(unsigned id)
{
  const u32 bitmask = (1u << id);
  reg(Regs::SB_ISTEXT) &= ~bitmask;
  recalculate_irqs();
}

u8
SystemBus::read_u8(u32 address)
{
  switch (address) {

    default:
      DEBUG(
        "Unhandled read to System Bus MMIO Register @ physical address 0x{:08x} (u8)\n",
        address);
      return 0u;
  }
}

u16
SystemBus::read_u16(u32 address)
{
  switch (address) {
    default:
      DEBUG(
        "Unhandled read to System Bus MMIO Register @ physical address 0x{:08x} (u16)\n",
        address);
      return 0u;
  }
}

u32
SystemBus::read_u32(u32 address)
{
  u32 value = 0u;

  switch (address) {
    /* Channel-2 DMA */
    case 0x005f6800:
      value = reg(Regs::SB_C2DSTAT);
      break;

    case 0x005f6804:
      value = reg(Regs::SB_C2DLEN);
      break;

    case 0x005f6808:
      value = 0; // SB_C2DST
      break;

    /* General ASIC state */
    case 0x005f6024: /* EXPEVT */
      // Software-initiated Reset. Something bad happened..
      DEBUG("Software-iniated reset (0x005f6024).. OH no!!\n");
      value = 0u; /* Power-on reset */
      break;

    case 0x005f6880:
      value = 8u; // XXX : Check this.
      break;

    case 0x005f6884:
      value = reg(Regs::SB_LMMODE0);
      break;

    case 0x005f6888:
      value = reg(Regs::SB_LMMODE1);
      break;

    case 0x005f688C: /* SB_FFST */
      log.verbose(
        "FFST (FIFO Status)) read to System Bus MMIO Register @ physical address "
        "%08x (u32)",
        address);
      value = 0x0u;
      break;

    case 0x005f689C: /* SB_SBREV System Board Revision */
      value = 0x0000000Bu;
      break;

    case 0x005f6900: { /* reg(Regs::SB_ISTNRM) */
      const u32 cur_err = reg(Regs::SB_ISTERR) ? 0x80000000u : 0x0;
      const u32 cur_ext = reg(Regs::SB_ISTEXT) ? 0x40000000u : 0x0;
      const u32 current = reg(Regs::SB_ISTNRM) | cur_ext | cur_err;
      log.verbose("Interrupt NORMAL read to System Bus MMIO Register @ physical address "
                  "%08x (u32) result 0x%08x",
                  address,
                  current);
      value = current;
      break;
    }

    case 0x005f6904: /* reg(Regs::SB_ISTEXT) */
      log.info("Interrupt EXT read to System Bus MMIO Register @ physical address "
               "%08x (u32) result 0x%08x",
               address,
               reg(Regs::SB_ISTEXT).load());
      value = reg(Regs::SB_ISTEXT);
      break;

    case 0x005f6908: /* reg(Regs::SB_ISTERR) */
      log.info("Interrupt ERR read to System Bus MMIO Register @ physical address "
               "%08x (u32) result 0x%08x",
               address,
               reg(Regs::SB_ISTERR).load());
      value = reg(Regs::SB_ISTERR);
      break;

    /* Level 2 interrupt masks */
    case 0x005f6910:
      value = reg(Regs::SB_IML2NRM);
      break;
    case 0x005f6914:
      value = reg(Regs::SB_IML2EXT);
      break;
    case 0x005f6918:
      value = reg(Regs::SB_IML2ERR);
      break;

    /* Level 4 interrupt masks */
    case 0x005f6920:
      value = reg(Regs::SB_IML4NRM);
      break;
    case 0x005f6924:
      value = reg(Regs::SB_IML4EXT);
      break;
    case 0x005f6928:
      value = reg(Regs::SB_IML4ERR);
      break;

    /* Level 6 interrupt masks */
    case 0x005f6930:
      value = reg(Regs::SB_IML6NRM);
      break;
    case 0x005f6934:
      value = reg(Regs::SB_IML6EXT);
      break;
    case 0x005f6938:
      value = reg(Regs::SB_IML6ERR);
      break;

    /* PVR-DMA trigger masks */
    case 0x005f6940:
      value = reg(Regs::SB_PDTNRM);
      break;
    case 0x005f6944:
      value = reg(Regs::SB_PDTEXT);
      break;

    /* G2-DMA trigger masks */
    case 0x005f6950:
      value = reg(Regs::SB_G2DNRM);
      break;
    case 0x005f6954:
      value = reg(Regs::SB_G2DEXT);
      break;

    default:
      const auto it = register_map.find(address);
      if (it != register_map.end()) {
        DEBUG("Unhandled read from System Bus MMIO Register \"{}\" (u32)\n", it->second);
      } else {
        DEBUG("Unhandled read from System Bus MMIO Register @0x{:08x} (u32)\n", address);
      }
      value = 0u;
      break;
  }

  log.verbose("System Bus read_u32: 0x%08x -> 0x%08x", address, value);
  return value;
}

void
SystemBus::write_u8(u32 address, u8 val)
{
  switch (address) {
    default:
      DEBUG(
        "Unhandled u8 write to System Bus MMIO Register @ physical address 0x{:08x} <- "
        "0x{:02x}\n",
        address,
        val);
      break;
  }
}

void
SystemBus::write_u16(u32 address, u16 val)
{
  switch (address) {
    default:
      DEBUG(
        "Unhandled u16 write to System Bus MMIO Register @ physical address 0x{:08x} <- "
        "0x{:02x}\n",
        address,
        val);
      break;
  }
}

void
SystemBus::write_u32(u32 address, u32 val)
{
  log.verbose("System Bus write_u32: 0x%08x <- 0x%08x", address, val);

  switch (address) {
    /* Channel-2 DMA */
    case 0x005f6800: {
      val                   = val & 0b00000011111111111111111111100000;
      val                   = val | 0b00010000000000000000000000000000;
      reg(Regs::SB_C2DSTAT) = val;
      break;
    }

    case 0x005f6804:
      if (val == 0)
        val = 16 * 1024 * 1024;
      reg(Regs::SB_C2DLEN) = val;
      break;

    case 0x005f6808:
      if (val & 1u) {
        m_console->trace_event("ch2 DMA", TraceTrack::CPU, m_console->current_time());
        m_engine->issue(address, 0x0u);
      }
      break;

      /* Access to texture memory as 32b or 64b access. */
    case 0x005f6880:
      reg(Regs::SB_LMMODE0) = val;
      break;

    case 0x005f6884:
      reg(Regs::SB_LMMODE1) = val;
      break;

    /* Level 2 interrupt masks */
    case 0x005f6910:
      log.debug("System Bus set INT Level 2 mask to 0x%08x", val);
      reg(Regs::SB_IML2NRM) = val;
      recalculate_irqs();
      break;
    case 0x005f6914:
      log.debug("System Bus set EXT Level 2 mask to 0x%08x", val);
      reg(Regs::SB_IML2EXT) = val;
      recalculate_irqs();
      break;
    case 0x005f6918:
      log.debug("System Bus set ERR Level 2 mask to 0x%08x", val);
      reg(Regs::SB_IML2ERR) = val;
      recalculate_irqs();
      break;

    /* Level 4 interrupt masks */
    case 0x005f6920:
      log.debug("System Bus set INT Level 4 mask to 0x%08x", val);
      reg(Regs::SB_IML4NRM) = val;
      recalculate_irqs();
      break;
    case 0x005f6924:
      log.debug("System Bus set EXT Level 4 mask to 0x%08x", val);
      reg(Regs::SB_IML4EXT) = val;
      recalculate_irqs();
      break;
    case 0x005f6928:
      log.debug("System Bus set ERR Level 4 mask to 0x%08x", val);
      reg(Regs::SB_IML4ERR) = val;
      recalculate_irqs();
      break;

    /* Level 6 interrupt masks */
    case 0x005f6930:
      log.debug("System Bus set INT Level 6 mask to 0x%08x", val);
      reg(Regs::SB_IML6NRM) = val;
      recalculate_irqs();
      break;
    case 0x005f6934:
      log.debug("System Bus set EXT Level 6 mask to 0x%08x", val);
      reg(Regs::SB_IML6EXT) = val;
      recalculate_irqs();
      break;
    case 0x005f6938:
      log.debug("System Bus set ERR Level 6 mask to 0x%08x", val);
      reg(Regs::SB_IML6ERR) = val;
      recalculate_irqs();
      break;

    /* PVR-DMA trigger masks */
    case 0x005f6940:
      reg(Regs::SB_PDTNRM) = val;
      break;
    case 0x005f6944:
      reg(Regs::SB_PDTEXT) = val;
      break;

    /* G2-DMA trigger masks */
    case 0x005f6950:
      reg(Regs::SB_G2DNRM) = val;
      break;
    case 0x005f6954:
      reg(Regs::SB_G2DEXT) = val;
      break;

    /* Normal interrupt acknowledge */
    case 0x005f6900:
      log.debug("System Bus acknowledge normal interrupt 0x%08x", val);
      reg(Regs::SB_ISTNRM).fetch_and(~(val & 0x3FFFFFFF));
      recalculate_irqs();
      break;

    /* External interrupt status: Writes ignored */
    case 0x005f6904:
      log.warn("System Bus write to external interrupt status! 0x%08x", val);
      break;

    /* Error interrupt acknowledge */
    case 0x005f6908:
      log.debug("System Bus acknowledge error interrupt 0x%08x", val);
      reg(Regs::SB_ISTERR).fetch_and(~val);
      recalculate_irqs();
      break;

    default:
      const auto it = register_map.find(address);
      if (it != register_map.end()) {
        // DEBUG("Unhandled u32 write to System Bus MMIO Register \"{}\" @ 0x{:08x} <- "
        //       "0x{:08x}\n",
        //       it->second,
        //       address,
        //       val);
      } else {
        // DEBUG("Unhandled u32 write to System Bus MMIO Register @0x{:08x} <-
        // 0x{:08x}\n",
        //       address,
        //       val);
      }
      break;
  }
}

void
SystemBus::engine_callback(const u32 address, const u32 &value)
{
  switch (address) {
    /* PVR DMA request */
    case 0x005f6808: {
      const u32 c2dstat = reg(Regs::SB_C2DSTAT);
      const u32 c2dlen  = reg(Regs::SB_C2DLEN);

      const u32 LMMODE0 = reg(Regs::SB_LMMODE0);
      const u32 LMMODE1 = reg(Regs::SB_LMMODE1);

      log.debug("System Bus TA DMA started from PC=0x%08x C2DSTAT=0x%08x C2DLEN=0x%08x "
                "LMMODE0/1=%u/%u",
                cpu->registers().PC,
                c2dstat,
                c2dlen,
                LMMODE0,
                LMMODE1);

      if (cpu->execute_dmac(2u, c2dstat, c2dlen)) {
        reg(Regs::SB_C2DSTAT) = c2dstat + c2dlen;
      }


      reg(Regs::SB_C2DLEN) =0;
      // Regs::SB_C2DST <- 0 but we don't model this register

      raise_int_normal(Interrupts::Normal::EndOfDMA_CH2);
      break;
    }
  }
}

void
SystemBus::recalculate_irqs()
{
  const u32 istext = reg(Regs::SB_ISTEXT);
  const u32 isterr = reg(Regs::SB_ISTERR);

  // All external and error interrupts appear OR'd together in b30 and b31
  u32 istnrm = reg(Regs::SB_ISTNRM);
  if (istext) {
    istnrm |= 1 << 30;
  }
  if (isterr) {
    istnrm |= 1 << 31;
  }

  const u32 level6_mask = (istnrm & reg(Regs::SB_IML6NRM)) |
                          (istext & reg(Regs::SB_IML6EXT)) |
                          (isterr & reg(Regs::SB_IML6ERR));
  if (level6_mask) {
    cpu->latch_irq(9u);
  } else {
    cpu->cancel_irq(9u);
  }

  const u32 level4_mask = (istnrm & reg(Regs::SB_IML4NRM)) |
                          (istext & reg(Regs::SB_IML4EXT)) |
                          (isterr & reg(Regs::SB_IML4ERR));
  if (level4_mask) {
    cpu->latch_irq(11u);
  } else {
    cpu->cancel_irq(11u);
  }

  const u32 level2_mask = (istnrm & reg(Regs::SB_IML2NRM)) |
                          (istext & reg(Regs::SB_IML2EXT)) |
                          (isterr & reg(Regs::SB_IML2ERR));
  if (level2_mask) {
    cpu->latch_irq(13u);
  } else {
    cpu->cancel_irq(13u);
  }
}
