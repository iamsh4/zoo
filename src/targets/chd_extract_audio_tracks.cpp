#include <libchdr/chd.h>
#include <cassert>
#include <cstring>
#include <vector>
#include <stdexcept>

#include "shared/types.h"
#include "media/chd_disc.h"

int
main(int argc, char **argv)
{
  if (argc != 2) {
    printf("Usage: %s [chd file path]\n", argv[0]);
    return 1;
  }

  using namespace zoo::media;
  CHDDisc disc(argv[1]);

  std::vector<u8> sector_buffer(2352);
  char filename[512];

  for (const auto &track : disc.tracks()) {
    if (track.is_audio_track() && track.number == 2) {

      snprintf(filename, sizeof(filename), "audio_track_%02u.raw", track.number);
      printf("[Track %02u/%02u] Writing %u raw audio sectors\n",
             track.number,
             u32(disc.tracks().size()),
             track.num_sectors);


      int counter = 0;
      FILE *fout = fopen(filename, "wb");
      for (u32 s = track.fad; s < track.fad + track.num_sectors; ++s) {
        disc.read_sector(s, { sector_buffer.data(), sector_buffer.size() });
        fwrite(sector_buffer.data(), 2352, 1, fout);
        counter++;
        if (counter >= 75*60) break;
      }
      fclose(fout);

    }
  }

  return 0;
}