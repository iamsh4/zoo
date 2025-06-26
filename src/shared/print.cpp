#include <cstdarg>

#include "print.h"

std::string
format_string(const char *format, ...)
{
  va_list varargs;
  char buffer[512];

  va_start(varargs, format);
  vsnprintf(buffer, sizeof(buffer), format, varargs);
  va_end(varargs);

  return buffer;
}

u32
read_hex_u32(const char *input)
{
  if (strlen(input) > 2 && input[0] == '0' && input[1] == 'x') {
    input += 2;
  }

  u32 address;
  if (sscanf(input, "%x", &address) == 1) {
    return address;
  }
  return 0xFFFFFFFF;
}
