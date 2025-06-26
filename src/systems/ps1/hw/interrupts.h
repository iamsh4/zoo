#pragma once

namespace zoo::ps1::interrupts {
// clang-format off
// https://problemkaputt.de/psx-spx.htm#interrupts
enum Interrupt
{
  VBlank,                  /*!< IRQ0 VBLANK (PAL=50Hz, NTSC=60Hz) */
  GPU,                     /*!< IRQ1 GPU   Can be requested via GP0(1Fh) command (rarely used) */
  CDROM,                   /*!< IRQ2 CDROM */
  DMA,                     /*!< IRQ3 DMA */
  TMR0,                    /*!< IRQ4 TMR0  Timer 0 aka Root Counter 0 (Sysclk or Dotclk) */
  TMR1,                    /*!< IRQ5 TMR1  Timer 1 aka Root Counter 1 (Sysclk or H-blank) */
  TMR2,                    /*!< IRQ6 TMR2  Timer 2 aka Root Counter 2 (Sysclk or Sysclk/8) */
  ControllerAndMemoryCard, /*!< IRQ7 Controller and Memory Card - Byte Received Interrupt */
  SIO,                     /*!< IRQ8 SIO */
  SPU,                     /*!< IRQ9 SPU */
  Lightpen,                /*!< IRQ10 Controller - Lightpen Interrupt (reportedly also PIO...?) */
};
// clang-format on
} // namespace zoo::ps1::interrupts
