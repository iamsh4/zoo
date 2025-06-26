// vim: expandtab:ts=2:sw=2

#include <cassert>

#include "fox/ir_opcode.h"

namespace fox {
namespace ir {

/*!
 * @brief Basic container for details on an Opcode's implementation. Used to
 *        build the table of Opcode details below.
 */
struct OpcodeInfo {
  const char *name;
  const unsigned source_count;
  const bool has_result;
};

static const OpcodeInfo opcode_info[] = {
  [u8(Opcode::ReadGuest)]            = OpcodeInfo { "readgr", 1, true },
  [u8(Opcode::WriteGuest)]           = OpcodeInfo { "writegr", 2, false },
  [u8(Opcode::Load)]                 = OpcodeInfo { "load", 1, true },
  [u8(Opcode::Store)]                = OpcodeInfo { "store", 2, false },
  [u8(Opcode::RotateRight)]          = OpcodeInfo { "rotr", 2, true },
  [u8(Opcode::RotateLeft)]           = OpcodeInfo { "rotl", 2, true },
  [u8(Opcode::LogicalShiftRight)]    = OpcodeInfo { "shiftr", 2, true },
  [u8(Opcode::LogicalShiftLeft)]     = OpcodeInfo { "shiftl", 2, true },
  [u8(Opcode::ArithmeticShiftRight)] = OpcodeInfo { "ashiftr", 2, true },
  [u8(Opcode::And)]                  = OpcodeInfo { "and", 2, true },
  [u8(Opcode::Or)]                   = OpcodeInfo { "or", 2, true },
  [u8(Opcode::ExclusiveOr)]          = OpcodeInfo { "xor", 2, true },
  [u8(Opcode::Not)]                  = OpcodeInfo { "not", 1, true },
  [u8(Opcode::BitSetClear)]          = OpcodeInfo { "bsc", 3, true },
  [u8(Opcode::Add)]                  = OpcodeInfo { "add", 2, true },
  [u8(Opcode::Subtract)]             = OpcodeInfo { "sub", 2, true },
  [u8(Opcode::Multiply)]             = OpcodeInfo { "mul", 2, true },
  [u8(Opcode::Multiply_u)]           = OpcodeInfo { "umul", 2, true },
  [u8(Opcode::Divide)]               = OpcodeInfo { "div", 2, true },
  [u8(Opcode::Divide_u)]             = OpcodeInfo { "udiv", 2, true },
  [u8(Opcode::Modulus)]              = OpcodeInfo { "mod", 2, true },
  [u8(Opcode::SquareRoot)]           = OpcodeInfo { "sqrt", 1, true },
  [u8(Opcode::Extend16)]             = OpcodeInfo { "extend16", 1, true },
  [u8(Opcode::Extend32)]             = OpcodeInfo { "extend32", 1, true },
  [u8(Opcode::Extend64)]             = OpcodeInfo { "extend64", 1, true },
  [u8(Opcode::BitCast)]              = OpcodeInfo { "bitcast", 1, true },
  [u8(Opcode::CastFloatInt)]         = OpcodeInfo { "castf2i", 1, true },
  [u8(Opcode::CastIntFloat)]         = OpcodeInfo { "casti2f", 1, true },
  [u8(Opcode::ResizeFloat)]          = OpcodeInfo { "resizef", 1, true },
  [u8(Opcode::Test)]                 = OpcodeInfo { "test", 2, true },
  [u8(Opcode::Compare_eq)]           = OpcodeInfo { "cmp.eq", 2, true },
  [u8(Opcode::Compare_lt)]           = OpcodeInfo { "cmp.lt", 2, true },
  [u8(Opcode::Compare_lte)]          = OpcodeInfo { "cmp.lte", 2, true },
  [u8(Opcode::Compare_ult)]          = OpcodeInfo { "cmp.ult", 2, true },
  [u8(Opcode::Compare_ulte)]         = OpcodeInfo { "cmp.ulte", 2, true },
  [u8(Opcode::Branch)]               = OpcodeInfo { "br", 1, false },
  [u8(Opcode::IfBranch)]             = OpcodeInfo { "ifbr", 2, false },
  [u8(Opcode::Select)]               = OpcodeInfo { "select", 3, false },
  [u8(Opcode::Exit)]                 = OpcodeInfo { "exit", 2, false },
  [u8(Opcode::Call)]                 = OpcodeInfo { "call", 3, true },
  [u8(Opcode::None)]                 = OpcodeInfo { "nop", 0, false },
};

const char *
opcode_to_name(const Opcode opcode)
{
  assert(unsigned(opcode) < sizeof(opcode_info) / sizeof(opcode_info[0]));
  return opcode_info[unsigned(opcode)].name;
}

unsigned
opcode_source_count(const Opcode opcode)
{
  assert(unsigned(opcode) < sizeof(opcode_info) / sizeof(opcode_info[0]));
  return opcode_info[unsigned(opcode)].source_count;
}

bool
opcode_has_result(const Opcode opcode)
{
  assert(unsigned(opcode) < sizeof(opcode_info) / sizeof(opcode_info[0]));
  return opcode_info[unsigned(opcode)].has_result;
}

}
}
