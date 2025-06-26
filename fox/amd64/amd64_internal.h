#pragma once

#include "fox/fox_types.h"
#include "amd64/amd64_types.h"

namespace fox {
namespace codegen {
namespace amd64 {

/*
 * Quick overview of x86 instruction encoding. Instructions have the following
 * basic format:
 *
 *     [PREFIX] [OPCODE] [MOD-REG-R/M] [SIB] [DISPLACEMENT] [IMMEDIATE]
 *
 * PREFIX:        Zero to four bytes, which affect meaning of opcode
 * OPCODE:        One to three bytes. More than one byte only if first byte is
 *                0x0F. Three bytes only if second byte is 0x38 or 0x3a.
 * MOD-REG-R/M:   One byte. Controls addressing mode, operand size, and
 *                register target.
 * SIB:           Zero or one byte. Controls scaled indexing mode for memory
 *                access.
 * DISPLACEMENT:  Zero, one, two, or four bytes. Specifies a byte-granular
 *                displacement for memory operands.
 * IMMEDIATE:     Zero, one, two, or four bytes. Provides an constant value
 *                as an operand or base address.
 */

/*!
 * @struct fox::codegen::amd64::ModRM
 * @brief  Operand and addressing mode control for instructions. Shortened to
 *         mrr in other code.
 *
 * For opcodes that only take a single operand, the R/M field specifies which
 * register to use. For other opcodes, the destination bit of opcodes indicates
 * which of the two operands are source and destination.
 *
 * The stack pointer's R/M encoding is used instead to indicate the presence
 * of the SIB.
 */
struct ModRM {
  enum ModeValue
  {
    ModeSpecial = 0u,
    ModeByteDisp = 1u,
    ModeDwordDisp = 2u,
    ModeRegister = 3u
  };

  union {
    struct {
      u8 rm : 3;  /* Register + Memory Operand */
      u8 reg : 3; /* Register Operand */
      u8 mod : 2; /* Addressing Mode */
    };

    u8 raw;
  };
};

/*!
 * @struct fox::codegen::amd64::SIB
 * @brief  Scaled Index Byte, the optional encoding for the scaled indexed
 *         addressing mode.
 *
 * It is illegal to specify the stack pointer as the offset source. Using
 * EBP as the base source indicates displacement-only mode based on the value
 * of the ModRegRM's mod field.
 */
struct SIB {
  union {
    struct {
      u8 base : 3;  /* Register Base Source */
      u8 index : 3; /* Register Offset Source */
      u8 scale : 2; /* Offset Scale */
    };

    u8 raw;
  };
};

/*!
 * @struct fox::codegen::amd64::REX
 * @brief  Instruction prefix byte used to specify alternate register sets,
 *         scaling modes, and operand sizes.
 */
struct REX {
  union {
    struct {
      u8 b : 1;           /* ModRegRM.rm or SIB.base Extension */
      u8 x : 1;           /* SIB.scale Extension */
      u8 r : 1;           /* ModRegRM.reg Extension */
      u8 w : 1;           /* Operand Size Override */
      const u8 fixed : 4; /* Fixed - Must Be 0100 */
    };

    u8 raw;
  };

  PrefixRex() : b(0u), x(0u), r(0u), fixed(4u)
  {
    return;
  }
};

}
}
}
