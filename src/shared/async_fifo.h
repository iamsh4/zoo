#pragma once

#include <condition_variable>

#include "shared/types.h"

/*!
 * @brief A thread-safe FIFO implementation with a fixed payload type of
 *        std::pair<u32, T>.
 */
template<typename T>
class AsyncFIFO {
public:
  AsyncFIFO(unsigned size);
  ~AsyncFIFO();

  void write(u32 address, T value);
  void read(u32 *address, T *value);

private:
  std::pair<u32, T> *m_data;
  unsigned m_size;
  unsigned m_head;
  unsigned m_tail;
  std::mutex m_lock;
  std::condition_variable m_condvar;
};
