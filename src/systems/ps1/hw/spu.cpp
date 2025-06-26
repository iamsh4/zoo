#include "systems/ps1/hw/spu.h"
#include "systems/ps1/console.h"

namespace zoo::ps1 {

SPU::SPU(Console *console) : m_console(console)
{
  SPUSTAT.raw = 0;
  SPUCNT.raw = 0;
}

void
SPU::register_regions(fox::MemoryTable *memory)
{
  memory->map_mmio(0x1F80'1c00, 640, "SPU", this);
}

u16
SPU::read_u16(u32 addr)
{
  if (addr >= 0x1F80'1C00 && addr < 0x1F80'1Da0) {
    // 1F801C00h+N*10h 4   Voice 0..23 Volume Left/Right
    // 1F801C04h+N*10h 2   Voice 0..23 ADPCM Sample Rate
    // 1F801C06h+N*10h 2   Voice 0..23 ADPCM Start Address
    // 1F801C08h+N*10h 4   Voice 0..23 ADSR Attack/Decay/Sustain/Release
    // 1F801C0Ch+N*10h 2   Voice 0..23 ADSR Current Volume
    // 1F801C0Eh+N*10h 2   Voice 0..23 ADPCM Repeat Address

    // More is in here than what is listed above
    // xxx :
    return 0;
  }

  switch (addr) {
    case 0x1f80'1db8:
    case 0x1f80'1dba:
      // XXX : main volume
      return 0;

    case 0x1f80'1daa:
      return SPUCNT.raw;

    case 0x1f80'1dae:
      return SPUSTAT.raw;

    case 0x1f80'1d88:
    case 0x1f80'1d8a:
      // xxx : KON
      return 0;

    case 0x1f80'1d8c:
    case 0x1f80'1d8e:
      // xxx : KOFF
      return 0;

    case 0x1f80'1d9c:
    case 0x1f80'1d9e:
      // xxx : channel on/off stat
      return 0;

    case 0x1F80'1DA4:
      return m_irq_address;

    case 0x1F801DA6:
      return m_data_transfer_addr;

    case 0x1f80'1dac:
      // xxx
      return m_sound_ram_data_transfer_ctrl;

    case 0x1f80'1db0:
    case 0x1f80'1db2:
      // CD audio volume
      return 0;

    case 0x1f80'1db4:
    case 0x1f80'1db6:
      // External audio volume
      return 0;

    default:
      printf("spu: Unhandled read 0x%08x \n", addr);
      assert(false);
  }
  assert(false);
  throw std::runtime_error("Unhandled SPU read_u16");
}

void
SPU::write_u16(u32 addr, u16 value)
{
  // printf("spu: write 0x%08x < 0x%x\n", addr, value);

  if (addr >= 0x1F80'1C00 && addr < 0x1F80'1D80) {
    // 1F801C00h+N*10h 4   Voice 0..23 Volume Left/Right
    // 1F801C04h+N*10h 2   Voice 0..23 ADPCM Sample Rate
    // 1F801C06h+N*10h 2   Voice 0..23 ADPCM Start Address
    // 1F801C08h+N*10h 4   Voice 0..23 ADSR Attack/Decay/Sustain/Release
    // 1F801C0Ch+N*10h 2   Voice 0..23 ADSR Current Volume
    // 1F801C0Eh+N*10h 2   Voice 0..23 ADPCM Repeat Address
    // xxx :
    return;
  }

  if (addr >= 0x1F801Dc0 && addr < 0x1f80'1e00) {
    // xxx : reverb
    return;
  }

  switch (addr) {
    case 0x1F80'1D80: // main vol
    case 0x1F80'1D82:

    case 0x1F80'1D84: // reverb
    case 0x1F80'1D86:
      printf("spu: xxx volume-related\n");
      break;

    case 0x1F80'1Daa:
      printf("spu: SPUCNT < %x\n", value);
      SPUCNT.raw = value;
      SPUSTAT.raw &= ~0x3f;
      SPUSTAT.raw |= 0x3f & value;
      break;

    case 0x1f80'1d88:
    case 0x1f80'1d8a:
      // xxx : KON
      break;

    case 0x1f80'1d8c:
      // xxx : KOFF
      break;

    case 0x1f80'1d8e:
      // garbage
      break;

    case 0x1f80'1d90:
    case 0x1f80'1d92:
      // xxx
      PMON = value;
      break;

    case 0x1F801D94:
    case 0x1F801D96:
      // xxx : noise
      break;

    case 0x1F801D98:
    case 0x1F801D9a:
      // xxx : reverb
      break;

    case 0x1f80'1d9c:
    case 0x1f80'1d9e:
      // xxx : channel on/off stat
      break;

    case 0x1F80'1DA2:
      // xxx : reverb ram area
      break;

    case 0x1F80'1DA4:
      m_irq_address = value;
      break;

    case 0x1F801DB0:
    case 0x1F801DB2:
      // xxx : cd audio vol
      break;

    case 0x1F801DB4:
    case 0x1F801DB6:
      // xxx : external audio vol
      break;

    case 0x1F801DA6:
      // xxx
      printf("spu: transfer_addr < 0x%x\n", value);
      m_data_transfer_addr = value;
      break;

    case 0x1F801DA8:
      // xxx : manual data transfer fifo to ARAM
      m_data_transfer_addr = (m_data_transfer_addr + 1) & 0xffff;
      break;

    case 0x1F801DAC:
      // xxx : transfer mode
      printf("spu: transfer_ctrl < 0x%x\n", value);
      m_sound_ram_data_transfer_ctrl = value;
      break;

    default:
      printf("spu: Unhandled write 0x%08x < %x\n", addr, value);
      assert(false);
  }
}

void
SPU::push_dma_word(u32 word)
{
  // XXX : sound ram
  // XXX : (This should actually be *8 or something)
  m_data_transfer_addr = (m_data_transfer_addr + 1) & 0xffff;
}

// SPU shouldn't allow any access but 16bit

u8
SPU::read_u8(u32 addr)
{
  assert(false);
  throw std::runtime_error("Unhandled SPU read_u8");
}

u32
SPU::read_u32(u32 addr)
{
  assert(false);
  throw std::runtime_error("Unhandled SPU read_u32");
}

void
SPU::write_u8(u32 addr, u8 value)
{
  assert(false);
  throw std::runtime_error("Unhandled SPU write_u8");
}

void
SPU::write_u32(u32 addr, u32 value)
{
  assert(false);
  throw std::runtime_error("Unhandled SPU write_u32");
}

} // namespace zoo::ps1
