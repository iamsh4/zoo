#include <cstdio>
#include <SDL2/SDL.h>

#include "shared/profiling.h"
#include "apu/audio_dummy.h"

namespace apu {

Audio_Dummy::Audio_Dummy()
  : m_queued_samples(0)
{
  return;
}

Audio_Dummy::~Audio_Dummy()
{
  return;
}

size_t
Audio_Dummy::queue_samples(i32 *const data, const size_t bytes)
{
  m_queued_samples += bytes / sizeof(i32);
  return bytes;
}

size_t
Audio_Dummy::queued_samples() const
{
  if (m_queued_samples >= 40) {
    m_queued_samples -= 40;
  } else {
    m_queued_samples = 0;
  }

  return m_queued_samples; /* XXX */
}

void
Audio_Dummy::pause()
{
  return;
}

}
