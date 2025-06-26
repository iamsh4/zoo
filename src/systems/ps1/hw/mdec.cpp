#include <algorithm>

#include "shared/types.h"
#include "systems/ps1/console.h"
#include "systems/ps1/hw/mdec.h"

namespace zoo::ps1 {

MDEC::MDEC(Console *console) : m_console(console)
{
  // auto reg = m_console->mmio_registry();
  // reg->setup("Interrupts", "I_STAT", &m_i_stat, [&](std::vector<char> *buffer) {
  //   return make_interrupt_string(m_i_stat, buffer);
  // });
  // reg->setup("Interrupts", "I_MASK", &m_i_mask, [&](std::vector<char> *buffer) {
  //   return make_interrupt_string(m_i_mask, buffer);
  // });
}

u8
MDEC::read_u8(u32 addr)
{
  printf("mdec: read_u8 0x%08x pc=0x%08x\n", addr, m_console->cpu()->PC());
  assert(false);
  throw std::runtime_error("Unhandled MDEC read_u8");
}

u16
MDEC::read_u16(u32 addr)
{
  printf("mdec: read_u16 0x%08x pc=0x%08x\n", addr, m_console->cpu()->PC());
  assert(false);
  throw std::runtime_error("Unhandled MDEC read_u16");
}

u32
MDEC::read_u32(u32 addr)
{
  printf("mdec: read_u32 0x%08x pc=0x%08x\n", addr, m_console->cpu()->PC());
  switch (addr) {
    case 0x1f80'1820:
      // xxx : Data output
      return 0;

    case 0x1f80'1824:
      return m_status.raw;

    default:
      assert(false);
      throw std::runtime_error("Unhandled MDEC read_u32");
  }
}

void
MDEC::write_u8(u32 addr, u8 value)
{
  printf("mdec: write_u8 0x%08x < 0x%x pc=0x%08x\n", addr, value, m_console->cpu()->PC());
  assert(false);
  throw std::runtime_error("Unhandled MDEC write_u8");
}

void
MDEC::write_u16(u32 addr, u16 value)
{
  printf(
    "mdec: write_u16 0x%08x < 0x%x pc=0x%08x\n", addr, value, m_console->cpu()->PC());
  write_u32(addr, value);
}

void
MDEC::write_u32(u32 addr, u32 value)
{
  printf(
    "mdec: write_u32 0x%08x < 0x%x pc=0x%08x\n", addr, value, m_console->cpu()->PC());

  switch (addr) {
    case 0x1f80'1820:
      handle_command(value);
      break;

    case 0x1f80'1824:
      // Reset MDEC
      if (value & 0x8000'0000) {
        m_status.raw = 0x8004'0000;
        m_command_data.clear();
        m_remaining_params = 0;
        m_current_command = Command::None;
      }
      // xxx enable data-in
      // xxx enable data-out
      break;

    default:
      assert(false);
      throw std::runtime_error("Unhandled MDEC write_u32");
  }
}

void
MDEC::handle_command(u32 value)
{
  const u32 command = (value >> 29) & 0b111;
  printf("mdec: command/param 0x%08x\n", value);

  if (m_current_command == Command::None) {
    if (command == 1) {
      m_remaining_params = command & 0xffff;
      m_current_command = Command::DecodeMacroblocks;
    } else if (command == 2) {
      // Luminance data in 64 bytes
      m_remaining_params = 16;
      // Do we also have color data?
      if (value & 1) {
        m_remaining_params += 16;
      }
      m_current_command = Command::SetQuantTables;
    } else {
      assert(false);
      throw std::runtime_error("MDEC unhandled command");
    }
  }

  else if (m_current_command == Command::DecodeMacroblocks) {
    const u32 param = value;
    printf("mdec: DecodeMacroblocks param 0x%08x\n", param);
    m_remaining_params = std::max(0u, m_remaining_params - 1);

    if (m_remaining_params == 0) {
      printf("mdec: DecodeMacroblocks xxx Decode not yet implemented!\n");
    }
  }

  // xxx
}

void
MDEC::register_regions(fox::MemoryTable *memory)
{
  memory->map_mmio(0x1F80'1820, 8, "MDEC", this);
}

} // namespace zoo::ps1
