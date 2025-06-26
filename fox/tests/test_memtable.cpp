// vim: expandtab:ts=2:sw=2

#include <cstdio>

#include "fox/memtable.h"

int
main(int argc, char *argv[])
{
  /* Max phys address on dreamcast is 0x1BFFFFFF (~450MiB)  */
  fox::MemoryTable *const table = new fox::MemoryTable(0x20000000, 0x20000000);

  table->map_sdram(0x0C000000u, 0x00800000u, "Dummy SDRAM");
  table->map_file(
    0x00000000u, 0x00200000u, "bios-files/SEGA_Dreamcast_BIOS/dc_bios.bin", 0x00000000u);

  printf("Reading from BIOS: ");
  for (unsigned i = 0u; i < 29u; ++i) {
    printf("%c", table->read<uint8_t>(2002u + i));
  }
  printf("\n");

  delete table;

  return 0;
}
