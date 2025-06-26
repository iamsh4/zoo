#pragma once

#include <cstdint>
#include <memory>

class FreeIndexList {
public:
  FreeIndexList(size_t capacity) : m_capacity(capacity)
  {
    m_data = std::make_unique<int[]>(capacity);
    for (size_t i = 0; i < capacity; i++) {
      m_data[i] = i;
    }
    head = 0;
    tail = capacity - 1;
  }

  bool acquire(int *index)
  {
    if (head == tail) {
      return false;
    }
    *index = m_data[head];
    head = (head + 1) % m_capacity;
    return true;
  }

  void release(int index)
  {
    m_data[tail] = index;
    tail = (tail + 1) % m_capacity;
  }

private:
  size_t m_capacity;
  std::unique_ptr<int[]> m_data;
  // Invariant: head == tail iff the list is empty.
  // Invariant: head points to the first free index.
  // Invariant: tail points to the last free index.
  size_t head;
  size_t tail;
};
