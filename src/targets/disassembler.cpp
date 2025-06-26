// vim: expandtab:ts=2:sw=2

#include <cstdio>
#include <stdexcept>
#include "guest/sh4/sh4.h"
#include "guest/sh4/sh4_debug.h"

int
main(int argc, char *argv[])
{
  FILE *const fp = fopen("bios-files/SEGA_Dreamcast_BIOS/dc_bios.bin", "rb");

  u32 PC = 0x8c000000u;
  while (!feof(fp)) {
    u16 opcode;
    if (fread(&opcode, 2u, 1u, fp) != 1)
      throw std::runtime_error("Failed to read texture data");

    printf("[%08x] %s\n", PC, cpu::Debugger::disassemble(opcode, PC).c_str());
    PC += 2u;
  }

  fclose(fp);

/* XXX ??? */
#if 0
  if (argc == 93873) {
    MemoryTable *mem = new MemoryTable(0x1BFFFFFFu, 0x1BFFFFFFu);
    cpu::SH4 *bla = new cpu::SH4(mem, nullptr);
    (void)bla;
  }
#endif

  return 0;
}
