// vim: expandtab:ts=2:sw=2

#include <cstdio>
#include <cassert>
#include <chrono>
#include <iostream>

#include "core/console.h"
#include "gpu/texture_manager.h"
#include "systems/dreamcast/noop_renderer.h"
#include "apu/audio_dummy.h"
#include "shared/log.h"

#define SIMULATED_NANOS u64(5) * 1000 * 1000 * 1000
#define AVERAGE_CYCLES_PER_INSTRUCTION 0.6
#define NANOSECONDS_PER_CYCLE 5lu

int
main(int argc, char *argv[])
{
  Log::level = Log::LogLevel::None;

  std::shared_ptr<zoo::local::Settings> settings = std::make_shared<zoo::local::Settings>();
  const char* bios_path = getenv("ZOO_DC_BIOS_PATH");
  if (!bios_path) {
    throw std::runtime_error("ZOO_DC_BIOS_PATH not set");
  }
  settings->set("dreamcast.bios_path", bios_path);

  zoo::dreamcast::NoopRenderer renderer;
  Console dreamcast(settings, new apu::Audio_Dummy(), &renderer);

  std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

  /* Required so texture loading doesn't segfault */
  new gpu::TextureManager(&dreamcast);

  while (dreamcast.current_time() < SIMULATED_NANOS) {
    dreamcast.run_for(std::chrono::milliseconds(10));
  }

  std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();

  const uint64_t nanoseconds_elapsed =
    std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

  printf("Elapsed host time %1.4lfs, elapsed guest time %1.4lfs (~%luM instructions)\n",
         double(nanoseconds_elapsed) / 1e9,
         double(SIMULATED_NANOS) / 1e9,
         SIMULATED_NANOS / NANOSECONDS_PER_CYCLE);

  const double ratio = double(SIMULATED_NANOS) / double(nanoseconds_elapsed);
  printf("Emulation averaged %0.2lf%% native speed.\n", 100.0 * ratio);

  return 0;
}
