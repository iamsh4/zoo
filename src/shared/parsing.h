#pragma once

#include <string>
#include <sstream>

#include "shared/types.h"

inline u32
parse_long(const std::string &str)
{
  u32 result = 0u;

  // Hexadecimal with leading 0x.....
  if (str[1] == 'x') {
    std::stringstream ss;
    ss << std::hex << str.substr(2);
    ss >> result;
  }

  // Otherwise, assume decimal
  else {
    result = std::stoi(str);
  }

  return result;
}
