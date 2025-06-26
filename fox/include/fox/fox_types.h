// vim: expandtab:ts=2:sw=2

#pragma once

#include <initializer_list>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <cassert>

namespace fox {

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef float f32;
typedef double f64;

/*!
 * @union fox::Value
 * @brief Container for any possible scalar constant type supported by IR or
 *        RTL pathways in foxjit.
 */
union Value {
  u8 u8_value;
  u16 u16_value;
  u32 u32_value;
  u64 u64_value;
  i8 i8_value;
  i16 i16_value;
  i32 i32_value;
  i64 i64_value;
  f32 f32_value;
  f64 f64_value;
  bool bool_value;
  u32 label_value;
  void *hostptr_value;
};

class reference_counted {
public:
  reference_counted() : m_references(0u)
  {
    return;
  }

  virtual ~reference_counted()
  {
    deleted = true;
    return;
  }

  void up_refcnt()
  {
    assert(!deleted);
    ++m_references;
  }

  void down_refcnt()
  {
    assert(!deleted);
    if (--m_references == 0u) {
      delete this;
    }
  }

  std::atomic<u32> m_references;
  bool deleted = false;
};

template<typename T>
class ref {
public:
  ref() : m_data(nullptr)
  {
    return;
  }

  ref(T *const data) : m_data(data)
  {
    if (m_data != nullptr) {
      m_data->up_refcnt();
    }
  }

  ref(const ref<T> &other) : m_data(other.m_data)
  {
    if (this != &other && m_data != nullptr) {
      m_data->up_refcnt();
    }
  }

#if 0
  ref(ref<T> &&other)
    : m_data(other.m_data)
  {
    if (this != &other) {
      other.m_data = nullptr;
    }
  }
#endif

  ~ref()
  {
    if (m_data != nullptr) {
      m_data->down_refcnt();
      m_data = nullptr;
    }
  }

  ref<T> &operator=(const ref<T> &other)
  {
    if (this != &other) {
      if (m_data) {
        m_data->down_refcnt();
      }
      m_data = other.m_data;
      if (m_data) {
        m_data->up_refcnt();
      }
    }
    return *this;
  }

#if 0
  ref<T> &operator=(ref<T> &&other)
  {
    if (this != &other) {
      if (m_data != nullptr) {
        m_data->down_refcnt();
      }
      m_data = other.m_data;
      other.m_data = nullptr;
    }
    return *this;
  }
#endif

  T *operator->()
  {
    return m_data;
  }

  const T *operator->() const
  {
    return m_data;
  }

  T *get()
  {
    return m_data;
  }

  const T *get() const
  {
    return m_data;
  }

  operator bool() const
  {
    return m_data != nullptr;
  }

  bool operator!() const
  {
    return m_data == nullptr;
  }

private:
  T *m_data;
};

/*!
 * @struct FlagSet
 * @brief Type safe wrapper for handling a set of bit flags without runtime
 *        overhead in the common case.
 *
 * The flags enum should have each flag value as an integer starting from 0.
 * A maximum of 16 flags are supported.
 */
template<typename T, typename S = u32>
struct FlagSet {
  S flags; /* XXX */

  constexpr FlagSet() : flags(0u)
  {
    return;
  }

  constexpr FlagSet(const std::initializer_list<T> flag_set) : flags(0u)
  {
    for (const T &flag : flag_set) {
      flags |= (1u << static_cast<u16>(flag));
    }
  }

  decltype(flags) operator*() const
  {
    return flags;
  }

  bool check(T flag) const
  {
    return !!((1u << static_cast<decltype(flags)>(flag)) & flags);
  }
};

}
