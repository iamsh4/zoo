// vim: expandtab:ts=2:sw=2

#pragma once

#include "fox/fox_types.h"

namespace fox {

/*!
 * @brief Returns true if the input is a power of 2.
 */
static constexpr bool
is_power2(const u64 value)
{
  return ((value - 1lu) & value) == 0lu;
}

/*!
 * @brief Round value up to a multiple of grain.
 */
template<typename T>
static constexpr T
round_up(const T value, const T grain)
{
  return (value + grain - 1) / grain * grain;
}

/*!
 * @brief Rotate bits in the input value left. The input should be an unsigned
 *        integer value.
 */
template<typename T>
T
rotate_left(const T value, u8 distance)
{
  static constexpr size_t total_bits = sizeof(T) * 8u;
  distance = distance & (total_bits - 1u);
  return (value << distance) | (value >> (total_bits - distance));
}

/*!
 * @brief Rotate bits in the input value right. The input should be an unsigned
 *        integer value.
 */
template<typename T>
T
rotate_right(const T value, u8 distance)
{
  static constexpr size_t total_bits = sizeof(T) * 8u;
  distance = distance & (total_bits - 1u);
  if (distance == 0) {
    return value;
  } else {
    return (value >> distance) | (value << (total_bits - distance));
  }
}

}
