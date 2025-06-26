#include <cassert>
#include <cstdio>
#include "shared/async_fifo.h"

template<typename T>
AsyncFIFO<T>::AsyncFIFO(const unsigned size)
  : m_data(new std::pair<u32, T>[size]),
    m_size(size),
    m_head(0u),
    m_tail(0u)
{
  assert(size > 1u);
}

template<typename T>
AsyncFIFO<T>::~AsyncFIFO()
{
  delete[] m_data;
}

template<typename T>
void
AsyncFIFO<T>::write(u32 address, T value)
{
  std::unique_lock<std::mutex> lock(m_lock);

  const unsigned next_tail = (m_tail + 1u) % m_size;
  if (next_tail == m_head) {
    m_condvar.wait(lock);
  }

  m_data[m_tail].first = address;
  m_data[m_tail].second = value;
  m_tail = next_tail;

  m_condvar.notify_one();
}

template<typename T>
void
AsyncFIFO<T>::read(u32 *address, T *value)
{
  std::unique_lock<std::mutex> lock(m_lock);

  if (m_head == m_tail) {
    m_condvar.wait(lock);
  }

  *address = m_data[m_head].first;
  *value = m_data[m_head].second;

  const unsigned next_tail = (m_tail + 1u) % m_size;
  if (next_tail == m_head) {
    m_condvar.notify_one();
  }

  m_head = (m_head + 1u) % m_size;
}

template class AsyncFIFO<u32>;
template class AsyncFIFO<u64>;
