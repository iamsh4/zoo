// vim: expandtab:ts=2:sw=2

#include <memory>
#include <cstdio>
#include <cstring>
#include <fmt/core.h>

#include "amd64/amd64_routine.h"

using namespace fox;

void
test_routine()
{
  printf("======== test_routine ========\n");

  /*
   * mov eax, 0x01234567
   * ret
   */
  const u8 x86_opcodes[] = { 0xB8, 0x67, 0x45, 0x23, 0x01, 0xC3 };
  std::unique_ptr<fox::codegen::Routine> const routine {
    new fox::codegen::amd64::Routine(x86_opcodes, sizeof(x86_opcodes)) };
  const uint64_t return_value = routine->execute(nullptr);
  fmt::print("return_value = {:#018x}\n", return_value);

  printf("\n");
}

int
main(int argc, char *argv[])
{
  test_routine();
  return 0;
}
