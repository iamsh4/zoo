#include <filesystem>
#include "local/game_library.h"

int
main(int argc, char **argv)
{
  if (argc != 2) {
    printf("usage: %s [path to directory]\n", argv[0]);
    return 1;
  }

  zoo::local::GameLibrary lib;
  lib.load("/tmp/test.gamedb");

  printf("\n");
  lib.clear();
  lib.scan(
    argv[1],
    zoo::local::GameLibrary::ScanSettings {
      .recursive = true,
      .only_modified = false,
      .extensions = { ".chd", ".gdi" },
    },
    [](const zoo::local::GameLibraryEntry &latest, u32 current, u32 total) {
      u32 percent = current * 100 / total;
      char buff[64];
      strncpy(buff, latest.name.c_str(), 63);
      printf("\rScan progress: %3u%% %-80s", percent, buff);
      fflush(stdout);
    });

  printf("Found %lu entries\n", lib.data().size());

  lib.save("/tmp/gamedb.json");

  printf("\n");
  return 0;
}