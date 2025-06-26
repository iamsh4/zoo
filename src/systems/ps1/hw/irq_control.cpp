#include "shared/types.h"
#include "systems/ps1/console.h"
#include "systems/ps1/hw/irq_control.h"

namespace zoo::ps1 {

// VBlank,                  /*!< IRQ0 VBLANK (PAL=50Hz, NTSC=60Hz) */
//   GPU,                     /*!< IRQ1 GPU   Can be requested via GP0(1Fh) command
//   (rarely used) */ CDROM,                   /*!< IRQ2 CDROM */ DMA, /*!< IRQ3 DMA */
//   TMR0,                    /*!< IRQ4 TMR0  Timer 0 aka Root Counter 0 (Sysclk or
//   Dotclk) */ TMR1,                    /*!< IRQ5 TMR1  Timer 1 aka Root Counter 1
//   (Sysclk or H-blank) */ TMR2,                    /*!< IRQ6 TMR2  Timer 2 aka Root
//   Counter 2 (Sysclk or Sysclk/8) */ ControllerAndMemoryCard, /*!< IRQ7 Controller and
//   Memory Card - Byte Received Interrupt */ SIO,                     /*!< IRQ8 SIO */
//   SPU,                     /*!< IRQ9 SPU */
//   Lightpen,                /*!< IRQ10 Controller - Lightpen Interrupt (reportedly also
//   PIO...?) */
bool
make_interrupt_string(u32 bits, std::vector<char> *buffer)
{
  snprintf(buffer->data(),
          buffer->size(),
          "%s%s%s%s%s%s%s%s%s%s%s",
          (bits & (1 << 0)) ? "[Vblank]" : "",
          (bits & (1 << 1)) ? "[GPU]" : "",
          (bits & (1 << 2)) ? "[CDROM]" : "",
          (bits & (1 << 3)) ? "[DMA]" : "",
          (bits & (1 << 4)) ? "[Timer0]" : "",
          (bits & (1 << 5)) ? "[Timer1]" : "",
          (bits & (1 << 6)) ? "[Timer2]" : "",
          (bits & (1 << 7)) ? "[Controller]" : "",
          (bits & (1 << 8)) ? "[SIO]" : "",
          (bits & (1 << 9)) ? "[SPU]" : "",
          (bits & (1 << 10)) ? "[Lightgun]" : "");
  return true;
}

IRQControl::IRQControl(Console *console) : m_console(console)
{
  auto reg = m_console->mmio_registry();
  reg->setup("Interrupts", "I_STAT", &m_i_stat, [&](std::vector<char> *buffer) {
    return make_interrupt_string(m_i_stat, buffer);
  });
  reg->setup("Interrupts", "I_MASK", &m_i_mask, [&](std::vector<char> *buffer) {
    return make_interrupt_string(m_i_mask, buffer);
  });
}

u8
IRQControl::read_u8(u32 addr)
{
  assert(false);
  throw std::runtime_error("Unhandled IRQControl read_u8");
}

u16
IRQControl::read_u16(u32 addr)
{
  return (u16)read_u32(addr);
}

u32
IRQControl::read_u32(u32 addr)
{
  switch (addr) {
    case 0x1f80'1070:
      // printf("irq_control: read(0x%08x) stat -> 0x%08x pc=0x%08x\n",
      //        addr,
      //        m_i_stat,
      //        m_console->cpu()->PC());
      return m_i_stat;
      break;
    case 0x1f80'1074:
      // printf("irq_control: read(0x%08x) mask -> 0x%08x\n", addr, m_i_mask);
      return m_i_mask;
      break;
    default:
      assert(false);
      throw std::runtime_error("Unhandled IRQControl read_u32");
  }
}

void
IRQControl::write_u8(u32 addr, u8 value)
{
  assert(false);
}

void
IRQControl::write_u16(u32 addr, u16 value)
{
  write_u32(addr, value);
}

void
IRQControl::write_u32(u32 addr, u32 value)
{
  printf(
    "irq_control: write 0x%08x < 0x%x pc=0x%08x\n", addr, value, m_console->cpu()->PC());
  switch (addr) {
    case 0x1f80'1070:
      // Write zero to acknowledge interrupt
      m_i_stat &= value;
      update_cpu_external_interrupt();
      break;
    case 0x1f80'1074:
      m_i_mask = value;
      update_cpu_external_interrupt();
      break;
    default:
      assert(false);
  }
}

void
IRQControl::raise(interrupts::Interrupt interrupt)
{
  const u32 bit = 1u << interrupt;
  m_i_stat |= bit;
  update_cpu_external_interrupt();
}

void
IRQControl::update_cpu_external_interrupt()
{
  m_console->cpu()->set_external_irq(m_i_stat & m_i_mask);
}

void
IRQControl::register_regions(fox::MemoryTable *memory)
{
  memory->map_mmio(0x1F80'1070, 8, "IRQ Control", this);
}

} // namespace zoo::ps1
