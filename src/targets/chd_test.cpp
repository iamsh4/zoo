#include <libchdr/chd.h>
#include <cassert>
#include <cstring>
#include <vector>
#include <stdexcept>

#include "shared/types.h"
#include "media/chd_disc.h"
#include "media/gdi_disc.h"
#include "peripherals/region_free_dreamcast_disc.h"

void
dump_warning_track_audio(zoo::media::CHDDisc &disc)
{

  const auto &audio = disc.tracks()[1];
  printf("audio %u %u\n", audio.fad, audio.fad + audio.num_sectors);

  const u32 start = audio.fad;
  const u32 end = audio.fad + audio.num_sectors;

  std::vector<u8> sector_buffer(2352);
  FILE *fout = fopen("out.raw", "wb");
  for (u32 s = start; s < end; ++s) {
    disc.read_sector(s, { sector_buffer.data(), sector_buffer.size() });
    fwrite(sector_buffer.data(), 2352, 1, fout);
  }
  fclose(fout);
}

void
dump_all_hunks(zoo::media::CHDDisc &disc)
{
  FILE *f = fopen("hunks.bin", "wb");
  std::vector<u8> buffer(10 * 1024 * 1024);

  const u32 count = disc.get_chd_hunk_count();
  int last_percent = -1;
  for (u32 i = 0; i < count; ++i) {
    u32 bytes_read = disc.read_chd_hunk(i, { buffer.data(), buffer.size() });
    fwrite(buffer.data(), bytes_read, 1, f);

    const int percent = i * 100 / count;
    if (percent > last_percent) {
      printf("Progress: %d%%\n", percent);
      last_percent = percent;
    }
  }

  fclose(f);
}

int
main(int argc, char **argv)
{
  if (argc != 2) {
    printf("Usage: %s [chd file path]\n", argv[0]);
    return 1;
  }

  using namespace zoo::media;
  CHDDisc basic(argv[1]);
  RegionFreeDreamcastDisc disc(std::make_shared<CHDDisc>(argv[1]));
  // RegionFreeDreamcastDisc disc(std::make_shared<GDIDisc>(argv[1]));

  // dump_warning_track_audio(disc);
  // return 0;

  // dump_all_hunks(disc);
  // return 0;

  const int start = 150 + 0;
  const int end = 150 + 0 + 5;

  std::vector<u8> sector_buffer(2352);
  for (int s = start; s < end; ++s) {
    printf("Sector %d (0x%04x) ...\n", s, s);
    disc.read_sector(s, { sector_buffer.data(), sector_buffer.size() });

    const int per_row = 16;
    for (int i = 0; i < 2352; i += per_row) {
      printf(" 0x%04x: ", i);
      for (int j = 0; j < per_row; ++j) {
        printf("%02x ", sector_buffer[i + j]);
      }

      printf("| ");

      for (int j = 0; j < per_row; ++j) {
        const char ch = sector_buffer[i + j];
        const bool is_ascii_printable = ch >= 33 && ch <= 126;
        if (is_ascii_printable) {
          printf("%c", ch);
        } else {
          printf(".");
        }
      }
      printf("\n");
    }
  }

  return 0;
}