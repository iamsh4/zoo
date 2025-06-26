// vim: expandtab:ts=2:sw=2

#pragma once

#include <iostream>
#include <iomanip>
#include <sstream>

#include "shared/types.h"

template<typename T>
struct hex_formatter {
  static constexpr unsigned width = sizeof(T) * 2u;
  const T &value;

  std::string str() const;
};

template<typename T>
hex_formatter<T>
hex_format(const T &value)
{
  return hex_formatter<T> { value };
}

template<typename T>
std::ostream &
operator<<(std::ostream &out, const hex_formatter<T> &value)
{
  const auto flags = out.flags();

  out << std::setw(value.width) << std::setfill('0') << std::hex;
  out << value.value;
  out.flags(flags);

  return out;
}

template<typename T>
std::string
hex_formatter<T>::str() const
{
  std::ostringstream ss;

  ss << std::setw(width) << std::setfill('0') << std::hex;
  ss << value;

  return ss.str();
}

std::string format_string(const char *format, ...);

u32 read_hex_u32(const char *input);
