#pragma once

#include <array>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>

#include "shared/types.h"

std::vector<u8>
exec(const char *cmd)
{
  std::array<char, 128> buffer;
  std::vector<u8> result;
  std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
  if (!pipe) {
    throw std::runtime_error("popen() failed!");
  }

  int bytes_read;
  while ((bytes_read = fread(buffer.data(), 1, buffer.size(), pipe.get())) > 0) {
    u32 previous_size = result.size();
    result.resize(previous_size + bytes_read);
    memcpy(result.data() + previous_size, buffer.data(), bytes_read);
  }

  return result;
}
