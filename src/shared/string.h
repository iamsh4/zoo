#pragma once

#include <string>

// trim from right
inline std::string &
rtrim(std::string &s, const char *t = " \t\n\r\f\v")
{
  s.erase(s.find_last_not_of(t) + 1);
  return s;
}