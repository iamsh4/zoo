#include "fox/ir/instruction.h"

namespace fox {
namespace ir {

template<>
Operand
Operand::constant(const u8 value)
{
  Constant constant;
  constant.u8_value = value;
  return Operand(Type::Integer8, constant);
}

template<>
Operand
Operand::constant(const u16 value)
{
  Constant constant;
  constant.u16_value = value;
  return Operand(Type::Integer16, constant);
}

template<>
Operand
Operand::constant(const u32 value)
{
  Constant constant;
  constant.u32_value = value;
  return Operand(Type::Integer32, constant);
}

template<>
Operand
Operand::constant(const u64 value)
{
  Constant constant;
  constant.u64_value = value;
  return Operand(Type::Integer64, constant);
}

template<>
Operand
Operand::constant(const i8 value)
{
  Constant constant;
  constant.i8_value = value;
  return Operand(Type::Integer8, constant);
}

template<>
Operand
Operand::constant(const i16 value)
{
  Constant constant;
  constant.i16_value = value;
  return Operand(Type::Integer16, constant);
}

template<>
Operand
Operand::constant(const i32 value)
{
  Constant constant;
  constant.i32_value = value;
  return Operand(Type::Integer32, constant);
}

template<>
Operand
Operand::constant(const i64 value)
{
  Constant constant;
  constant.i64_value = value;
  return Operand(Type::Integer64, constant);
}

template<>
Operand
Operand::constant(const f32 value)
{
  Constant constant;
  constant.f32_value = value;
  return Operand(Type::Float32, constant);
}

template<>
Operand
Operand::constant(const f64 value)
{
  Constant constant;
  constant.f64_value = value;
  return Operand(Type::Float64, constant);
}

template<>
Operand
Operand::constant(const bool value)
{
  Constant constant;
  constant.bool_value = value;
  return Operand(Type::Bool, constant);
}

}
}
