#include <array>
#include <cstdio>
#include <filesystem>
#include <fmt/core.h>

#include "serialization/stream.h"

using namespace serialization;

int
main()
{
  u32 x = 1;
  u8 y  = 2;
  std::array<u8, 7> z;
  for (int i = 0; i < 7; ++i)
    z[i] = 20 + i;
  std::vector<u32> regs = { 70, 71, 72 };

  const std::filesystem::path file_path =
    std::filesystem::temp_directory_path() / "zoo_test_serialization.bin";

  {
    Stream stream;
    stream.write(x, y, z, regs);

    std::ofstream f(file_path, std::ios::binary);
    f.write(reinterpret_cast<const char *>(stream.data()), stream.size());
  }

  x = y = 0;
  memset(z.data(), 0, z.size());
  memset(regs.data(), 0, sizeof(regs[0]) * regs.size());
  regs.clear();

  {
    std::ifstream f(file_path, std::ios::binary);

    // Get the file size
    f.seekg(0, std::ios::end);
    const size_t size = f.tellg();
    f.seekg(0, std::ios::beg);

    Stream stream;
    stream.write_raw_from_ifstream(f, size);
    stream.read(x, y, z, regs);
  }

  if (x != 1 || y != 2 || z[0] != 20 || z[6] != 26 || regs[0] != 70 || regs[2] != 72) {
    fmt::print("Failed to serialize/deserialize correctly.\n");
    return 1;
  }

  return 0;
}