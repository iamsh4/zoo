#pragma once

#include <cstdint>
#include <mutex>
#include "shared/types.h"

namespace zoo::dreamcast {

enum class Metric : u8
{
  /* Elapsed time on guest (i.e. elapsed scheduler nanos) */
  GuestNanos = 0,
  /* Elapsed time on host */
  HostNanos,
  /* Host time spent limiting simulation to ~real-time */
  HostNanosLimiting,

  /* Time */
  NanosSH4,
  NanosARM7DI,
  NanosAICASampleGeneration,
  NanosRender,
  NanosTextureGeneration,

  /* Counts */
  CountAudioSamples,
  CountSH4BasicBlocks,
  CountARM7DIBasicBlocks,
  CountGDROMBytesRead,

  CountRenderObjects,
  CountRenderPolygons,
  CountStartRender,
  CountTaFifoBytes,
  CountTaYuvBytes,
  CountTaTextureBytes,
  CountGuestVsync,

  METRICS_COUNT
};

class SystemMetrics {
public:
  struct Data {
    std::atomic<u64> values[static_cast<u8>(Metric::METRICS_COUNT)];

    Data() {}
    Data(const Data &other)
    {
      for (u8 i = 0; i < static_cast<u8>(Metric::METRICS_COUNT); ++i) {
        values[i] = other.values[i].load();
      }
    }

    Data &operator=(const Data &other)
    {
      for (u8 i = 0; i < static_cast<u8>(Metric::METRICS_COUNT); ++i) {
        values[i] = other.values[i].load();
      }
      return *this;
    }

    void reset()
    {
      for (u8 i = 0; i < static_cast<u8>(Metric::METRICS_COUNT); ++i) {
        values[i] = 0;
      }
    }

    u64 get(Metric metric) const
    {
      return values[static_cast<u8>(metric)];
    }
  };

  SystemMetrics()
  {
    m_data.reset();
  }

  void increment(Metric metric, u64 value)
  {
    m_data.values[static_cast<u8>(metric)] += value;
  }

  /** Atomically retrieve the Data object since last call, and clear the current one. */
  Data next()
  {
    Data result = m_data;
    m_data.reset();
    return result;
  }

private:

  Data m_data;
  //   std::mutex m_mutex;
};

}