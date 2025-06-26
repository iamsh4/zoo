#pragma once

#include "shared/types.h"
#include "shared/log.h"
#include "apu/audio.h"

namespace apu {

class Audio_Dummy final : public Audio {
public:
  Audio_Dummy();
  ~Audio_Dummy();

  /*!
   * @brief Move the playback forward the specified number of samples. If less
   *        than that number of samples are available, the excess is ignored.
   */
  void finish_samples(size_t count);

  // void set_output_volume(float volume);
  size_t queue_samples(i32 *data, size_t bytes);
  size_t queued_samples() const;
  void pause();
  void clear_queued_samples() {}

private:
  mutable size_t m_queued_samples;
};

}
