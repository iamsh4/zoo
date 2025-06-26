#include <SDL2/SDL.h>
#include <cstdio>
#include "shared/profiling.h"

#include "apu/audio_sdl.h"

namespace apu {

Audio_SDLImpl::Audio_SDLImpl()
{
  SDL_AudioSpec want;
  SDL_zero(want);
  want.freq     = QueueFrequency;
  want.format   = AUDIO_S32;
  want.channels = QueueChannels;
  want.samples  = QueueSize;
  want.callback = NULL; /* Use queue interface */

  SDL_Init(SDL_INIT_AUDIO);
  dev = SDL_OpenAudioDevice(NULL, false, &want, NULL, 0);
  if (dev == 0) {
    printf("Failed to open audio device: %s\n", SDL_GetError());
    return;
  }

  m_log.info("Initialized audio device '%s'", SDL_GetAudioDeviceName(1, 0));
  SDL_PauseAudioDevice(dev, 0);
}

Audio_SDLImpl::~Audio_SDLImpl()
{
  if (dev != 0) {
    SDL_PauseAudioDevice(dev, 1);
    SDL_CloseAudioDevice(dev);
  }
}

size_t
Audio_SDLImpl::queue_samples(i32 *const data, const size_t bytes)
{
  ProfileZone;

  if (SDL_QueueAudio(dev, data, bytes) != 0) {
    printf("queue failed: %s\n", SDL_GetError());
  }

  return bytes;
}

size_t
Audio_SDLImpl::queued_samples() const
{
  if (dev != 0) {
    return SDL_GetQueuedAudioSize(dev) / (2 * sizeof(i32));
  }
  return 0;
}

void
Audio_SDLImpl::clear_queued_samples()
{
  if (dev) {
    SDL_ClearQueuedAudio(dev);
  }
}

void
Audio_SDLImpl::pause()
{
  if (dev != 0) {
    SDL_PauseAudioDevice(dev, 1);
  }
}

}
