#pragma once

#include <array>
#include <vector>
#include "shared/types.h"

namespace util {

template<typename T>
struct Span {
  T *const ptr;
  const u64 count;

  Span() : ptr(nullptr), count(0) {}
  Span(T *_ptr) : ptr(_ptr), count(1) {}
  Span(T *_ptr, u64 _count) : ptr(_ptr), count(_count) {}

  Span(std::vector<T> &vec) : ptr(vec.data()), count(vec.size()) {}

  template<size_t N>
  Span(std::array<T, N> &arr) : ptr(arr.data()),
                                count(N)
  {
  }

  T &operator[](size_t i)
  {
    assert(i < count);
    return ptr[i];
  }
  const T &operator[](size_t i) const
  {
    assert(i < count);
    return ptr[i];
  }

  T *begin()
  {
    return ptr;
  }

  T *end()
  {
    return ptr + count;
  }

  const T *begin() const
  {
    return ptr;
  }

  const T *end() const
  {
    return ptr + count;
  }
};

}
