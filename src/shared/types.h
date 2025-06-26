// vim: expandtab:ts=2:sw=2

#pragma once

#include <atomic>
#include <cstdint>
#include <cassert>
#include <stdexcept>
#include <cstring>

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

// Reinterpret the bits of incoming type as outgoing type
template<typename T, typename I>
static inline T
reinterpret(I input)
{
  if (sizeof(I) != sizeof(T)) {
    throw std::invalid_argument("Size of source and destination must match!");
  }

  T output;
  memcpy(&output, &input, sizeof(I));
  return output;
}

/*!
 * @brief Generic template used to store a set of boolean flags as a bitmask
 *        with type safety.
 *
 * The enum T should be a class enum with unique values ranging from 0 to one
 * less than the number of bits in the representation S. It is OK to have holes
 * in this range.
 */
template <typename T, typename S = uint32_t>
class bitflags {
public: /* Public API */
  bitflags()
    : m_value(0)
  {
    return;
  }

  bitflags(const T bit)
    : m_value(S(1) << static_cast<S>(bit))
  {
    return;
  }

  explicit bitflags(const S value)
    : m_value(value)
  {
    return;
  }

  operator bool() const
  {
    return m_value != 0;
  }

  explicit operator S() const
  {
    return m_value;
  }

  bitflags operator&(const bitflags other) const
  {
    return bitflags(m_value & other.m_value);
  }

  bitflags operator&(const T flag) const
  {
    return bitflags(m_value & bitflags(flag).m_value);
  }

  bitflags &operator&=(const bitflags other)
  {
    m_value &= other.m_value;
    return *this;
  }

  bitflags &operator&=(const T flag)
  {
    m_value &= bitflags(flag).m_value;
    return *this;
  }

  bitflags operator|(const bitflags other) const
  {
    return bitflags(m_value | other.m_value);
  }

  bitflags operator|(const T flag) const
  {
    return bitflags(m_value | bitflags(flag).m_value);
  }

  bitflags &operator|=(const bitflags other)
  {
    m_value |= other.m_value;
    return *this;
  }

  bitflags &operator|=(const T flag)
  {
    m_value |= bitflags(flag).m_value;
    return *this;
  }

private: /* Internal data */
  S m_value;
};
