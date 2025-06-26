#pragma once

#include "types.h"

#ifdef _MSC_VER
#include <intrin.h>

u32
__builtin_ctz(u32 value)
{
  DWORD trailing_zero = 0u;
  if (_BitScanForward(&trailing_zero, value)) {
    return trailing_zero;
  } else {
    return 32u;
  }
}

u32
__builtin_clz(u32 value)
{
  DWORD leading_zero = 0u;
  if (_BitScanReverse(&leading_zero, value)) {
    return 31u - leading_zero;
  } else {
    return 32u;
  }
}
#endif

template<typename T>
T
rotate_left(const T value, u8 distance)
{
  static constexpr size_t total_bits = sizeof(T) * 8u;
  distance = distance & (total_bits - 1u);
  return (value << distance) | (value >> (total_bits - distance));
}

template<typename T>
T
rotate_right(const T value, u8 distance)
{
  static constexpr size_t total_bits = sizeof(T) * 8u;
  distance = distance & (total_bits - 1u);
  return (value >> distance) | (value << (total_bits - distance));
}

//
constexpr u32
bit_mask(u32 hi_inclusive, u32 low_inclusive) noexcept
{
  assert(hi_inclusive < 32);
  assert(low_inclusive <= hi_inclusive);
  assert(low_inclusive < 32);

  u32 result = 0;
  for (u32 i = low_inclusive; i <= hi_inclusive; ++i)
    result |= (1 << i);
  return result;
}

constexpr u32
extract_bits(u32 input, u32 hi_inclusive, u32 low_inclusive) noexcept
{
  return (input & bit_mask(hi_inclusive, low_inclusive)) >> low_inclusive;
}

// Extend the sign of some operand so it has the same negative/positive value
template<unsigned bits>
static constexpr u32
extend_sign(const u32 i)
{
  const u32 sign_bit_mask = 1u << (bits - 1u);
  const u32 lower_mask = (1u << bits) - 1u;
  return (i & sign_bit_mask) ? ((~lower_mask) | i) : (lower_mask & i);
}
