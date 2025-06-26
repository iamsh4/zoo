// vim: expandtab:ts=2:sw=2

#include <string>
#include <cstdio>
#include <cstring>
#include <cassert>
#include <fmt/core.h>
#include <sys/mman.h>

#include "fox/fox_types.h"
#include "arm64/arm64_assembler.h"

using namespace fox;
using namespace fox::codegen::arm64;

void
print_u64(const u64 val)
{
  fmt::print(" --> Called from JIT: val = {:010X}\n", val);
}

std::vector<u32>
arm64_fpu()
{
  auto X = Registers::X;
  auto W = Registers::W;
  auto S = Registers::S;

  Assembler assembler;

  // Convention is X29 is previous FP, X30 is previous SP. This forms the stack frame
  // linked list.
  assembler.LDR(S(0), X(1), 8);
  assembler.FMOV(W(0), S(0));
  assembler.FMOV(S(0), W(0));
  assembler.STR(S(0), X(1), 16);
  assembler.RET(X(30));

  return assembler.assemble();
}

std::vector<u32>
arm64_test_bsc_and_cmp()
{
  auto X = Registers::X;
  auto W = Registers::W;
  auto SP = X(31);

  Assembler assembler;

  // Convention is X29 is previous FP, X30 is previous SP. This forms the stack frame
  // linked list.
  assembler.STP_pre(X(29), X(30), SP, -16);
  assembler.MOV(X(1), 0);
  assembler.ADD(W(0), W(1), W(0), Extension::SXTB, 0);
  assembler.LDP_post(X(29), X(30), SP, 16);
  assembler.RET(X(30));

  return assembler.assemble();
}

std::vector<u32>
arm64_call_func()
{
  // Get the address of a function outside of the JIT code, then call it.
  // This test demonstrates frame pointer/SP saving, and temporary value saves over a
  // function call.

  auto X = Registers::X;
  auto SP = X(31);

  Assembler assembler;
  auto func_addr = assembler.create_constant((u64)&print_u64);

  // Convention is X29 is previous FP, X30 is previous SP. This forms the stack frame
  // linked list.
  assembler.STP_pre(X(29), X(30), SP, -16);
  assembler.SUB(SP, SP, 16);
  assembler.ADD(X(29), SP, 0);

  assembler.MOV(X(2), 5);
  auto loop_label = assembler.create_label();
  assembler.push_label(loop_label);
  {
    assembler.LDR(X(1), func_addr);
    assembler.ADD(X(0), X(2), 0); // alias for MOV X2 -> X0

    assembler.STR(X(2), SP, 8); // Save loop counter
    assembler.BLR(X(1));
    assembler.LDR(X(2), SP, 8);

    assembler.SUBS(X(2), X(2), 1);
    assembler.B(Condition::PositiveOrZero, loop_label);
  }

  assembler.ADD(SP, SP, 16);
  assembler.LDP_post(X(29), X(30), SP, 16);
  assembler.RET(X(30));

  return assembler.assemble();
}

std::vector<u32>
arm64_access_jit_external_memory()
{
  // Fn(u64* ptr) { *ptr = 0x11223344; return ptr; }

  auto X = Registers::X;
  auto W = Registers::W;
  auto SP = X(31);

  Assembler assembler;
  auto C1 = assembler.create_constant((u32)0x11223344);

  assembler.SUB(SP, SP, 16);
  assembler.STR(X(30), SP, 8);

  assembler.LDR(W(1), C1);
  assembler.STR(X(1), X(0), X(31));

  assembler.LDR(X(30), SP, 8);
  assembler.ADD(SP, SP, 16);
  assembler.RET(X(30));

  return assembler.assemble();
}

std::vector<u32>
arm64_constant_access()
{
  // Compute C1 * loop_count, return in X0

  auto X = Registers::X;

  Assembler assembler;

  auto loop_label = assembler.create_label();

  auto C1 = assembler.create_constant((u32)0x11223344);
  auto loop_count = assembler.create_constant((u32)3);

  assembler.MOV(X(0), 0);
  assembler.LDR(X(1), loop_count);
  assembler.push_label(loop_label);
  {
    assembler.LDR(X(2), C1);
    assembler.ADD(X(0), X(0), X(2));
    assembler.SUBS(X(1), X(1), 1);
    assembler.B(Condition::UnsignedGreater, loop_label);
  }
  assembler.RET(X(30));

  return assembler.assemble();
}

std::vector<u32>
arm64_fibonacci()
{
  // Basic test of labels/branches, computing fibonacci number F(n+2)
  auto X = Registers::X;

  Assembler assembler;

  auto loop_label = assembler.create_label();
  auto exit_label = assembler.create_label();

  assembler.MOV(X(1), 0);
  assembler.MOV(X(2), 0);
  assembler.AND(X(1), X(1), X(2));
  assembler.ADD(X(2), X(2), 1);
  assembler.push_label(loop_label);
  {
    assembler.ADD(X(3), X(1), X(2));
    assembler.ADD(X(1), X(2), 0); // mov reg -> reg
    assembler.ADD(X(2), X(3), 0); // mov reg -> reg

    assembler.SUBS(X(0), X(0), 1);
    assembler.B(Condition::UnsignedGreater, loop_label);
  }
  assembler.push_label(exit_label);
  assembler.ADD(X(0), X(2), 0); // mov reg -> reg
  assembler.RET(X(30));

  return assembler.assemble();
}

std::vector<u32>
arm64_add(u32 val)
{
  auto X = Registers::X;
  auto W = Registers::W;

  Assembler assembler;

  auto loop_label = assembler.create_label();
  auto exit_label = assembler.create_label();

  assembler.MOV(W(1), Immediate { val });
  assembler.push_label(loop_label);
  {
    assembler.ADD(X(0), X(0), Immediate { 1 });
    assembler.SUBS(X(1), X(1), Immediate { 1 });
    assembler.B(Condition::UnsignedGreater, loop_label);
  }
  assembler.push_label(exit_label);
  assembler.RET(X(30));

  return assembler.assemble();
}

void
test_basic()
{
  printf("======== test_basic ========\n");

  // const auto data = arm64_add(10);
  // const auto data = arm64_constant_access();
  const auto data = arm64_fpu();

  for (const auto &line : data)
    fmt::print("{:#010x}\n", line);

  auto f = fopen("/tmp/arm64.bin", "w");
  fwrite(&data[0], sizeof(u32), data.size(), f);
  fclose(f);
}

void
test_execution()
{
  const u32 MMAP_SIZE = 4096 * 16;
  typedef u64 (*f_u64_u64)(u64);

  // const auto routine = arm64_constant_access();
  // const auto routine = arm64_test_bsc_and_cmp();
  const auto routine = arm64_call_func();

  void *const mapping =
    mmap(0, MMAP_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  memcpy(mapping, &routine[0], sizeof(u32) * routine.size());
  mprotect(mapping, MMAP_SIZE, PROT_READ | PROT_EXEC);

  auto f = fopen("/tmp/arm64.bin", "w");
  fwrite(&routine[0], sizeof(u32), routine.size(), f);
  fclose(f);

  auto fn = (f_u64_u64)mapping;

  u64 x = 255;
  u64 output = fn(x);
  fmt::print("Fn({:#x} -> {:#x})", x, output);
  munmap(mapping, MMAP_SIZE);
}

int
main(int argc, char *argv[])
{
  test_basic();

  // Tests that actually run on ARM64.
#ifdef __aarch64__
  test_execution();
#endif
  return 0;
}
