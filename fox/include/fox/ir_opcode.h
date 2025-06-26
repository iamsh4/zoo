// vim: expandtab:ts=2:sw=2

#pragma once

#include "fox/fox_types.h"
#include "fox/ir_types.h"

namespace fox {
namespace ir {

/*
 * Overview of the JIT intermediate language, which is SSA form.
 *
 * All instructions take between one and three operands and produce zero or
 * one results. The sources can be a register or a constant.
 *
 * All operands / results have a base type from this list:
 *     i8:   Signed or unsigned 8-bit integer
 *     i16:  Signed or unsigned 16-bit integer
 *     i32:  Signed or unsigned 32-bit integer
 *     i64:  Signed or unsigned 64-bit integer
 *     f32:  32-bit floating point
 *     f64:  64-bit floating point
 *     bool: A true/false value (internal type only - no bit representation)
 *
 * Additionally, the operands can have vector sizes of 1, 2, or 4.
 *
 * Guest machine registers (any state not visible to other resources) are
 * specified outside the IR with constraints for pre-loading values and
 * storing results.
 *
 * The human-readable format for instructions will always be:
 *    ${target} := ${opcode} ${source1}[, ${source2}[, ${source3}]]
 *
 * Constants are specified as "#0x%x", "#%d", "#%u", or "#%f" depending on the
 * type and readability. Registers are always specified as "$%u".
 *
 * Instructions:
 *
 *   OPCODE      TYPES              DESCRIPTION
 *   readgr      integer/float      Read the current value of a guest CPU register.
 *                                  Accepts a single integer argument containing
 *                                  a guest CPU specific register index.
 *   writegr     integer/float      Write a value to a guest CPU register. Accepts
 *                                  two arguments. The first argument must be an
 *                                  integer guest CPU specific register index,
 *                                  and the second argument is the value to store.
 *   load        integer/float      Load a value from memory. The source must be
 *                                  a 32-bit integer, interpreted according to
 *                                  the guest CPU's semantics.
 *   store       integer/float      Store value to a memory location. The target
 *                                  address must be a 32-bit integer, and is
 *                                  interpreted according to the guest CPU's
 *                                  semantics.
 *   rotr        integer            Rotate bits right.
 *   rotl        integer            Rotate bits left.
 *   shiftr      integer            Logical shift bits right.
 *   shiftl      integer            Logical shift bits left.
 *   ashiftr     integer            Arithmetic shift bits right.
 *   and         integer/bool       AND bits.
 *   or          integer/bool       OR bits.
 *   xor         integer/bool       XOR bits.
 *   not         integer/bool       NOT bits.
 *   bsc         bool, integer[2]   Conditionally set / clear a specific bit in
 *                                  an integer. (Value, Control, Bit Index). Value is
 *                                  input, control is whether bit should be set/cleared,
 *                                  Bit Index is a bit index of a bit within the input.
 *   add         integer/float      Type-specific addition.
 *   sub         integer/float      Type-specific subtraction.
 *   mul         integer/float      Type-specific signed multiplication.
 *   div         integer/float      Type-specific signed division.
 *   udiv        integer            Unsigned division.
 *   umul        integer            Unsigned multiplication.
 *   mod         integer            Type-specific integer division returning the
 *                                  remainder.
 *   sqrt        float              Calculate the square-root of the source value.
 *   extend16    integer8           Sign extend the input integer to 16 bits.
 *   extend32    integer8/16        Sign extend the input integer to 32 bits.
 *   extend64    integer8/16/32     Sign extend the input integer to 64 bits.
 *   bitcast.{}  integer/float      Reinterpret the raw input bits to the opcode
 *                                  type. Truncates larger types or adds 0 bits
 *                                  as necessary. The {} specifies the target
 *                                  type, and can be one of:
 *                                      i8, i16, i32, i64
 *                                      f32, f64
 *   castf2i.{}  float              Convert the input floating point value to
 *                                  an integer type. The target type is
 *                                  specified by {} and must be one of the
 *                                  integer types: i8, i16, i32, i64
 *                                  Note: Output uses signed conversion
 *   casti2f.{}  integer            Convert the input integer value to a float
 *                                  type. The target type is specified by {}
 *                                  and must be a float type: f32, f64
 *                                  Note: Input is treated as signed
 *   resizef.{}  float              Convert between the two floating point bit
 *                                  depths. The output type is specified by {}.
 *                                  It must not match the input type and may be
 *                                  one of:
 *                                  f32, f64
 *   test        integer            Perform a bitwise and of the two arguments
 *                                  and produce a bool result. If the 'and' is
 *                                  0 the result is false, otherwise it's true.
 *   cmp.{}      integer/float      Compare two source values and produce a bool
 *                                  result. The {} specifies the mode of comparison
 *                                  which may be one of:
 *                                      eq (sign agnostic)
 *                                      lt, lte (signed, integer or float)
 *                                      ult, ulte (unsigned integer only)
 *                                  Comparisons using greater-than are exposed
 *                                  but implemented by reversing arguments and
 *                                  encoding less-than.
 *   br           integer           Unconditionally branch to the provided target
 *                                  address. ***
 *   ifbr         bool, integer     Branch to the provided target address only if
 *                                  the bool value is true. ***
 *   select       bool, *[2]        Select between two values. If the bool value
 *                                  is false, returns the first value, otherwise
 *                                  returns the second value.
 *   exit         bool, integer64   If the source bool is true, exits the basic
 *                                  block and returns the integer64 value to
 *                                  the caller.
 *   call         integer64         Perform a call to a native function
 *                                  pointer. The call can optionally return a
 *                                  value and accept zero, one, or two inputs.
 *                                  Values (input and output) can be integer,
 *                                  float, or boolean (host-defined format).
 *                                  The native function is always passed the
 *                                  ir::Guest pointer as its first argument.
 *   nop          N/A               Placeholder instruction. Can be used to
 *                                  simplify optimization pases that eliminate
 *                                  instructions. Not to be translated by any
 *                                  backend.
 *
 * The IR always consists of extended basic blocks - execution can only start
 * from the beginning, and any control flow must exit the block or return to
 * the start.
 *
 * ** Branches are not currently supported. The CPU implementations use
 *    conditional updates + exit and allow the calling code to handle the
 *    control flow change.
 */

/*!
 * @brief Basic opcodes supported by the intermediate language.
 */
// clang-format off
enum class Opcode : u8 {
  ReadGuest,
  WriteGuest,
  Load,
  Store,
  RotateRight,
  RotateLeft,
  LogicalShiftRight,
  LogicalShiftLeft,
  ArithmeticShiftRight,
  And,
  Or,
  ExclusiveOr,
  Not,
  BitSetClear,
  Add,
  Subtract,
  Multiply,
  Multiply_u,
  Divide,
  Divide_u,
  Modulus,
  SquareRoot,
  Extend16,
  Extend32,
  Extend64,
  BitCast,
  CastFloatInt,
  CastIntFloat,
  ResizeFloat,
  Test,
  Compare_eq,
  Compare_lt,
  Compare_lte,
  Compare_ult,
  Compare_ulte,
  Branch,
  IfBranch,
  Select,
  Exit,
  Call,
  None
};
// clang-format on

/*!
 * @brief Return a string containing the mnemonic for the indicated Opcode.
 */
const char *opcode_to_name(Opcode opcode);

/*!
 * @brief Return the number of source arguments expected for the indicated
 *        Opcode.
 */
unsigned opcode_source_count(Opcode opcode);

/*!
 * @brief Returns whether the indicated Opcode will produce a result value.
 */
bool opcode_has_result(Opcode opcode);

}
}
