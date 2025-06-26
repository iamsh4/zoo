#pragma once

#include <chrono>
#include "types.h"

class Stopwatch {
private:
  using clock_source = std::chrono::high_resolution_clock;
  clock_source::time_point start_time;

public:
  Stopwatch()
  {
    reset();
  }

  void reset()
  {
    start_time = clock_source::now();
  }

  float elapsedSeconds()
  {
    const auto now = clock_source::now();
    const auto micros =
      std::chrono::duration_cast<std::chrono::microseconds>(now - start_time).count();
    return micros / float(1000000);
  }
};

inline u64 epoch_nanos() { 
  return std::chrono::high_resolution_clock::now().time_since_epoch().count();
}
