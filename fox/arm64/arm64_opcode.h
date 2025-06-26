#pragma once

#include "fox/fox_types.h"

namespace fox {
namespace codegen {
namespace arm64 {

enum class Opcode : u16 {
  INVALID = 0,

  // Push/Pop a subset of x0-x31
  PUSH_GPRS,
  POP_GPRS,

  LABEL,
  COND_RET,
  RET,

  LOAD_IMM32,
  LOAD_IMM64,

  READ_GUEST_REGISTER32,
  WRITE_GUEST_REGISTER32,
  READ_GUEST_REGISTER64,
  WRITE_GUEST_REGISTER64,

  LOAD_GUEST_MEMORY,
  FMOV32,
  FMOV64,

  ADD_BYTE,
  ADD_32,
  ADD_32_IMM,
  ADD_64,

  SUB_32,
  SUB_64,

  UMUL_32,

  MUL_32,
  MUL_64,

  DIV_32,
  DIV_64,

  SQRT_32,

  OR_32,
  AND_32,
  AND_64,
  XOR_32,

  OR_32_IMM,
  AND_32_IMM,
  AND_64_IMM,
  XOR_32_IMM,

  SHIFTL_32_IMM,
  SHIFTL_32,

  SHIFTR_32_IMM,
  SHIFTR_32,

  ASHIFTR_32_IMM,
  ASHIFTR_32,

  TEST_32,

  COND_SELECT_32,
  CMP,

  EXTEND32_BYTE,
  EXTEND32_WORD,

  CALL_FRAMED,
};

}
}
}
