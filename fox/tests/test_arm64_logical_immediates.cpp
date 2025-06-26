#include <fmt/core.h>

#include "arm64/arm64_logical_immediates.h"

using namespace fox;
using namespace fox::codegen::arm64;

int
main(int argc, char *argv[])
{
  LogicalImmediates generator;

  auto dump64 = [&](u64 val) {
    LogicalImmediates::Encoding encoding = generator.get_imm64(val);
    fmt::print("val {:#018x} -> N {} immr {} imms {}\n",
               val,
               encoding.N,
               encoding.immr,
               encoding.imms);
  };

  for (u64 s = 0; s < 64; ++s) {
    u64 val = 1ULL << s;
    dump64(val);
  }

  dump64(0xF);
  dump64(0xFF);
  dump64(0xFFFF);
  dump64(0xFFFFFFFF);
  dump64(0x1010101010101010);

  return 0;
}
