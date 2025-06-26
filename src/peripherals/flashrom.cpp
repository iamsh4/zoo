
#include <fstream>
#include "peripherals/flashrom.h"

// For information on the flash memory layout, see the datasheet:
// MBM29LV002TC

struct Sector {
  u32 offset;
  u32 size;
};
Sector _sectors[] = {
  { 0x00000, 0x10000 },
  { 0x10000, 0x10000 },
  // The data sheet lists more sectors but the DC only has 128KiB of flash
};

Sector
get_sector(u32 addr)
{
  // Page 12 of the datasheet
  const u32 tag = (addr >> 16) & 0b11;
  switch (tag) {
    case 0b00:
      return _sectors[0];
    case 0b01:
      return _sectors[1];
    default:
      // See earlier note. The Dreamcast does not have enough flash to address the other
      // sectors.
      throw std::runtime_error("FlashROM::get_sector invalid sector");
  }
  return Sector { 0 };
}

const u32 kAddr55 = 0x5555;
const u32 kAddrAA = 0x2AAA;

FlashROM::FlashROM(Console *console, std::filesystem::path file_path)
  : m_console(console),
    m_file_path(file_path)
{
  load_from_file();
}

FlashROM::~FlashROM()
{
  save_to_file();
}

void
FlashROM::reset()
{
  m_write_cycle = 0;
  m_mode        = Mode::Normal;
}

void
FlashROM::load_from_file()
{
  // Read file into m_data
  std::ifstream file(m_file_path, std::ios::binary | std::ios::ate);
  if (file.is_open()) {
    const std::streamsize size = file.tellg();
    if (size_t(size) != m_data.size()) {
      throw std::runtime_error("FlashROM::FlashROM invalid file size");
    }
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char *>(m_data.data()), size);
  } else {
    // TODO : Log error
  }
}

void
FlashROM::save_to_file()
{
  std::ofstream file(m_file_path, std::ios::binary);
  if (file.is_open()) {
    file.write(reinterpret_cast<const char *>(m_data.data()), m_data.size());
  } else {
    // TODO : Log error
  }
}

using flashrom_state_t = u64;

void
FlashROM::serialize(serialization::Snapshot &snapshot)
{
  snapshot.add_range("flashrom.data", kDataSize, m_data.data());

  const flashrom_state_t state = m_write_cycle | (u8(m_mode) << 8);
  static_assert(sizeof(state) == 8);
  snapshot.add_range("flashrom.state", sizeof(state), &state);
}

void
FlashROM::deserialize(const serialization::Snapshot &snapshot)
{
  snapshot.apply_all_ranges("flashrom.data", m_data.data());

  flashrom_state_t state;
  snapshot.apply_all_ranges("flashrom.state", &state);
  m_write_cycle = state & 0xff;
  m_mode = (Mode)(state >> 8);
}

void
FlashROM::register_regions(fox::MemoryTable *memory)
{
  memory->map_mmio(0x00200000u, 0x00020000u, "Flash ROM", this);
}

u8
FlashROM::read_u8(u32 addr)
{
  u8 result = m_data[addr & 0x1FFFF];
  // printf("flash read: %02x <- %08x\n", result, addr);
  return result;
}

u16
FlashROM::read_u16(u32 addr)
{
  if (addr % 2 != 0) {
    throw std::runtime_error("FlashROM::read_u16 unaligned access");
  }
  u16 result;
  memcpy(&result, &m_data[addr & 0x1FFFF], sizeof(result));
  printf("(illegal?) flash read16: %04x <- %08x\n", result, addr);
  return result;
}

u32
FlashROM::read_u32(u32 addr)
{
  if (addr % 4 != 0) {
    throw std::runtime_error("FlashROM::read_u32 unaligned access");
  }
  u32 result;
  memcpy(&result, &m_data[addr & 0x1FFFF], sizeof(result));
  printf("(illegal?) flash read32: %08x <- %08x\n", result, addr);
  return result;
}

u64
FlashROM::read_u64(u32 addr)
{
  throw std::runtime_error("FlashROM::read_u64 not implemented");
}

void
FlashROM::write_u8(u32 addr, u8 value)
{
  // Only 0x20000 bytes of flash
  addr &= 0x1FFFF;

  // printf("flash write: %02x -> %08x\n", value, addr);

  switch (m_write_cycle) {
    case 0: {
      if (addr == kAddr55 && value == 0xAA) {
        m_write_cycle = 1;
      } else {
        printf("unhandled flash write cycle 0: %02x -> %08x\n", value, addr);
      }
      break;
    }
    case 1: {
      if (addr == kAddrAA && value == 0x55) {
        m_write_cycle = 2;
      } else {
        printf("unhandled flash write cycle 1: %02x -> %08x\n", value, addr);
      }
      break;
    }
    case 2: {
      if (value == 0x80) {
        // Chip or Sector Erase
        m_write_cycle = 3;
      } else if (value == 0xA0) {
        // Program
        m_mode        = Mode::Program;
        m_write_cycle = 3;
      } else {
        printf("unhandled flash write cycle 2: %02x -> %08x\n", value, addr);
      }
      break;
    }
    case 3: {
      if (m_mode == Mode::Normal) {
        if (addr == kAddr55 && value == 0xAA) {
          m_write_cycle = 4;
        } else {
          printf("unhandled flash write cycle 3: %02x -> %08x\n", value, addr);
        }
      } else if (m_mode == Mode::Program) {
        // Program
        m_data[addr] &= value;
        m_write_cycle = 0;
        m_mode        = Mode::Normal;
      } else {
        printf(
          "unhandled flash write cycle 3 unhanded mode: %02x -> %08x\n", value, addr);
      }
      break;
    }
    case 4: {
      if (addr == kAddrAA && value == 0x55) {
        m_write_cycle = 5;
      } else {
        printf("unhandled flash write cycle 4: %02x -> %08x\n", value, addr);
      }
      break;
    }
    case 5: {
      if (value == 0x30) {
        // Sector erase
        const Sector sector = get_sector(addr);
        memset(&m_data[sector.offset], 0xFF, sector.size);
        m_write_cycle = 0;
      } else {
        printf("unhandled flash write cycle 5: %02x -> %08x\n", value, addr);
      }
      break;
    }
    default: break;
      //throw std::runtime_error("FlashROM::write_u8 invalid write cycle");
  }
}

void
FlashROM::write_u16(u32 addr, u16 value)
{
  throw std::runtime_error("FlashROM::write_u16 not implemented");
}

void
FlashROM::write_u32(u32 addr, u32 value)
{
  throw std::runtime_error("FlashROM::write_u32 not implemented");
}

void
FlashROM::write_u64(u32 addr, u64 value)
{
  throw std::runtime_error("FlashROM::write_u64 not implemented");
}

void
FlashROM::read_dma(u32 addr, u32 length, uint8_t *dst)
{
  throw std::runtime_error("FlashROM::read_dma not implemented");
}

void
FlashROM::write_dma(u32 addr, u32 length, const uint8_t *src)
{
  throw std::runtime_error("FlashROM::write_dma not implemented");
}
