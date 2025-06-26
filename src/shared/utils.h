#pragma once

#include <vector>
#include <string>
#include <string_view>

#include "shared/types.h"

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
 * @brief Split an input string into multiple lines.
 */
inline std::vector<std::string>
splitlines(const std::string &input)
{
  std::vector<std::string> result;
  size_t position = input.find_first_not_of("\r\n");
  while (position != std::string::npos) {
    const size_t end = input.find_first_of("\r\n", position);
    if (end == std::string::npos) {
      result.push_back(input.substr(position));
      break;
    }

    result.push_back(input.substr(position, end - position));
    position = input.find_first_not_of("\r\n", end);
  }

  return result;
}
