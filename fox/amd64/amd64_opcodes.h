// vim: expandtab:ts=2:sw=2

#pragma once

#include "fox/fox_types.h"

namespace fox {
namespace codegen {
namespace amd64 {

/*!
 * @enum fox::codegen::amd64::Opcode
 * @brief Virtual (RTL) opcodes for amd64 that have a very clean mapping to
 *        actual hardware instructions for the assembly phase.
 *
 * In RTL, these opcodes are placed in the MSB DWORD (starting from bit 32).
 * The lower 32 bits are used for opcode specific data storage.
 */
enum class Opcode : u16
{
  /*****************************************************************************
   * High Level / Internal Operations
   ****************************************************************************/

  /* Placeholder to mark a position in the RTL stream. Not emitted. */
  LABEL,

  /* Save / restore state. Lower 32 bits are a bitmask of which registers need
   * to be saved/restored. */
  PUSH_REGISTERS,
  POP_REGISTERS,

  /* Prepare / cleanup stack frame with storage for spill variables. */
  ALLOCATE_SPILL,
  FREE_SPILL,

  /* Subroutine methods. */
  READ_GUEST_REGISTER32,
  READ_GUEST_REGISTER64,
  WRITE_GUEST_REGISTER32,
  WRITE_GUEST_REGISTER64,
  LOAD_GUEST_MEMORY,
  LOAD_GUEST_MEMORY_SSE, /* TODO - currently not used */
  STORE_GUEST_MEMORY,
  STORE_GUEST_MEMORY_SSE, /* TODO - currently not used */
  CALL_FRAMED,
  RET,

  /*****************************************************************************
   * General Purpose Instructions
   ****************************************************************************/

  /* Place an immediate value in a register. 'S' means immediate gets size
   * extended. */
  LOAD_BYTE_IMM8,
  LOAD_QWORD_IMM32,
  LOAD_QWORD_IMM64,

  /* Basic shift / rotate operations. */
  SHIFTR_BYTE,
  SHIFTR_WORD,
  SHIFTR_DWORD,
  SHIFTR_QWORD,
  SHIFTL_BYTE,
  SHIFTL_WORD,
  SHIFTL_DWORD,
  SHIFTL_QWORD,
  ASHIFTR_BYTE,
  ASHIFTR_WORD,
  ASHIFTR_DWORD,
  ASHIFTR_QWORD,
  ROL1_BYTE,
  ROL1_WORD,
  ROL1_DWORD,
  ROL1_QWORD,
  ROL_BYTE,
  ROL_WORD,
  ROL_DWORD,
  ROL_QWORD,
  ROR1_BYTE,
  ROR1_WORD,
  ROR1_DWORD,
  ROR1_QWORD,
  ROR_BYTE,
  ROR_WORD,
  ROR_DWORD,
  ROR_QWORD,

  /* Basic shift/ rotate operations with constants. */
  SHIFTR_DWORD_IMM8,
  SHIFTL_DWORD_IMM8,
  ASHIFTR_DWORD_IMM8,

  /* Basic bit operations. */
  AND_BYTE,
  AND_WORD,
  AND_DWORD,
  AND_QWORD,
  OR_BYTE,
  OR_WORD,
  OR_DWORD,
  OR_QWORD,
  XOR_BYTE,
  XOR_WORD,
  XOR_DWORD,
  XOR_QWORD,
  NOT_BYTE,
  NOT_WORD,
  NOT_DWORD,
  NOT_QWORD,

  /* Basic bit operations with constants. */
  AND_DWORD_IMM32,
  OR_DWORD_IMM32,
  XOR_BYTE_IMM8,

  /* Basic ALU operations. */
  ADD_BYTE,
  ADD_WORD,
  ADD_DWORD,
  ADD_QWORD,
  SUB_BYTE,
  SUB_WORD,
  SUB_DWORD,
  SUB_QWORD,
  MUL_BYTE,
  MUL_WORD,
  MUL_DWORD,
  MUL_QWORD,
  IMUL_BYTE,
  IMUL_WORD,
  IMUL_DWORD,
  IMUL_QWORD,

  /* Basic ALU with constants. */
  ADD_DWORD_IMM32,
  SUB_DWORD_IMM32,

  /* Sign extension and casting. */
  EXTEND32_BYTE,
  EXTEND32_WORD,
  ZEXTEND32_BYTE,
  ZEXTEND32_WORD,
  EXTEND64_BYTE,
  EXTEND64_WORD,
  EXTEND64_DWORD,
  ZEXTEND64_BYTE,
  ZEXTEND64_WORD,
  ZEXTEND64_DWORD,

  /* Conditional moves. */
  CMOVNZ_WORD,
  CMOVNZ_DWORD,
  CMOVNZ_QWORD,
  CMOVZ_WORD,
  CMOVZ_DWORD,
  CMOVZ_QWORD,
  CMOVL_WORD,
  CMOVL_DWORD,
  CMOVL_QWORD,
  CMOVLE_WORD,
  CMOVLE_DWORD,
  CMOVLE_QWORD,
  CMOVB_WORD,
  CMOVB_DWORD,
  CMOVB_QWORD,
  CMOVBE_WORD,
  CMOVBE_DWORD,
  CMOVBE_QWORD,

  /* Conditional byte set. */
  SETNZ,
  SETZ,
  SETL,
  SETLE,
  SETB,
  SETBE,

  /* Comparison / test operations. */
  TEST_BYTE,
  TEST_WORD,
  TEST_DWORD,
  TEST_QWORD,
  CMP_BYTE,
  CMP_WORD,
  CMP_DWORD,
  CMP_QWORD,

  TEST_DWORD_IMM32,
  CMP_DWORD_IMM32,

  /* Basic move operations. */
  MOV_BYTE,
  MOV_WORD,
  MOV_DWORD,
  MOV_QWORD,
  MOVD_DWORD,
  MOVD_QWORD,

  /* Branch operations. */
  JMP,
  JNZ,

  /*****************************************************************************
   * Float / Vector Instructions
   ****************************************************************************/

  /* Basic math operations */
  ADD_VECPS,
  ADD_VECPD,
  ADD_VECSS,
  ADD_VECSD,
  SUB_VECPS,
  SUB_VECPD,
  SUB_VECSS,
  SUB_VECSD,
  MUL_VECPS,
  MUL_VECPD,
  MUL_VECSS,
  MUL_VECSD,
  DIV_VECPS,
  DIV_VECPD,
  DIV_VECSS,
  DIV_VECSD,

  /* Square root */
  SQRT_VECPS,
  SQRT_VECPD,
  SQRT_VECSS,
  SQRT_VECSD,

  /* Conversion operations */
  CVT_VECSS_I32,
  CVT_VECSS_I64,
  CVT_VECSD_I32,
  CVT_VECSD_I64,
  //CVT_I32_VECSS,
  //CVT_I64_VECSS,
  //CVT_I32_VECSD,
  //CVT_I64_VECSD,
};

}
}
}
