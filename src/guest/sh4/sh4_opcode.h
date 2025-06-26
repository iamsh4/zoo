// vim: expandtab:ts=2:sw=2

#pragma once

#include <functional>
#include <string>

#include "shared/types.h"
#include "sh4_ir.h"

namespace cpu {

class SH4;
class JitCompiler_x64;

enum OpcodeFlags
{
  NO_FLAGS = 0u,

  /* Cannot be in delay slot following branch. */
  ILLEGAL_IN_DELAY_SLOT = 1u << 0u,

  /* Can only be used in supervisor mode. */
  PRIVILEGED = 1u << 1u,

  /* Instruction may change PC. */
  BRANCH = 1u << 2u,

  /* Instruction may change PC and SPC. */
  CALL = 1u << 3u,

  /* Instruction is a branch that has a delay slot. */
  DELAY_SLOT = 1u << 4u,

  /* Instruction is a conditional branch (requires BRANCH). */
  CONDITIONAL = 1u << 5u,

  /* Instruction accesses memory. */
  MEMORY = 1u << 6u,

  /* Instruction cannot be directly JIT'd, and must use an upcall. This also
   * places an implicit barrier to prevent re-ordering before/after the
   * instruction. */
  DISABLE_JIT = 1u << 7u,

  /* This is an FPU instruction. */
  USES_FPU = 1u << 8u,

  /* The instruction changes behaviour based on FPSCR FR bit. */
  FPU_FR = 1u << 9u,

  /* The instruction changes behaviour based on FPSCR SZ bit. */
  FPU_SZ = 1u << 10u,

  /* The instruction changes behaviour based on FPSCR PR bit. */
  FPU_PR = 1u << 11u,

  /* The instruction changes CPU mode in a way that the JIT must start a new
   * basic block following its execution. */
  BARRIER = 1u << 12u,

  /* The instruction returns from a subroutine/function. */
  RETURN = 1u << 13u,
};

/*!
 * @brief Dispatch interface for Opcode implementations
 */
struct Opcode {
  void (SH4::*execute)(u16);
  const std::function<std::string(u16, u32)> disassemble;
  bool (SH4Assembler::*ir)(u16, u32, u32);
  uint64_t flags;
  uint64_t cycles;

  Opcode(decltype(execute) execute,
         decltype(disassemble) disassemble,
         decltype(ir) ir,
         uint64_t flags,
         uint64_t cycles = 1)
    : execute(execute),
      disassemble(disassemble),
      ir(ir),
      flags(flags),
      cycles(cycles)
  {
    return;
  }
};

};
