#pragma once

#include <SDL2/SDL.h>

#include "shared/types.h"
#include "shared/log.h"
#include "apu/audio.h"

namespace apu {

class Audio_SDLImpl final : public Audio {
public:
  Audio_SDLImpl();
  ~Audio_SDLImpl();

  // void set_output_volume(float volume);
  size_t queue_samples(i32 *data, size_t bytes);
  size_t queued_samples() const;
  void pause();
  void clear_queued_samples();

private:
  SDL_AudioDeviceID dev;
};

}
