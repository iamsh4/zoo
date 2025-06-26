#pragma once

#include "shared/types.h"
#include "shared/log.h"

namespace apu {

/*!
 * @class apu::Audio
 * @brief Base class for all audio backends.
 */
class Audio {
public:
  static constexpr u32 QueueSize = 512;
  static constexpr u32 QueueChannels = 2;
  static constexpr u32 QueueFrequency = 44100;

  virtual ~Audio();

  /*!
   * @brief Set output volume (linear) as a fraction [0, 1].
   */
  // virtual void set_output_volume(float volume) = 0;

  /*!
   * @brief Queue audio samples to the output device
   */
  virtual size_t queue_samples(i32 *data, size_t bytes) = 0;

  /*!
   * Returns the number of samples currently in SDL's buffers.
   */
  virtual size_t queued_samples() const = 0;

  /*!
   * @brief Clears any queued audio samples. Note that the underlying
   *        implementation may have already submitted samples to audio
   *        hardware which we have no control of.
   */
  virtual void clear_queued_samples() = 0;

  /*!
   * @brief Pause audio output.
   */
  virtual void pause() = 0;

protected:
  Log::Logger<Log::LogModule::AUDIO> m_log;
};

}
