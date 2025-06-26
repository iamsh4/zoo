// vim: expandtab:ts=2:sw=2

#include <map>
#include <cassert>

#include "ir/execution_unit.h"
#include "fox/ir_assembler.h"

namespace fox {
namespace ir {

Assembler::Assembler() : m_ebb(std::make_unique<ExecutionUnit>())
{
  return;
}

Assembler::~Assembler()
{
  return;
}

Operand
Assembler::allocate_register(const Type type)
{
  return m_ebb->allocate_register(type);
}

Operand
Assembler::readgr(const Type type, const Operand index)
{
  assert(is_numeric_type(type));
  assert(index.is_valid() && index.is_constant() /* XXX */);
  assert(index.type() == Type::Integer16);

  const Operand result = m_ebb->allocate_register(type);
  m_ebb->add_instruction(Instruction(Opcode::ReadGuest, type, { result }, { index }));

  return result;
}

void
Assembler::writegr(const Operand index, const Operand value)
{
  assert(value.is_valid());
  assert(is_numeric_type(value.type()));
  assert(index.is_valid() && index.is_constant() /* XXX */);
  assert(index.type() == Type::Integer16);

  m_ebb->add_instruction(
    Instruction(Opcode::WriteGuest, value.type(), {}, { index, value }));
}

Operand
Assembler::load(const Type type, const Operand address)
{
  assert(is_numeric_type(type));
  assert(address.is_valid());
  assert(address.type() == Type::Integer32); /* XXX */

  const Operand result = m_ebb->allocate_register(type);
  m_ebb->add_instruction(Instruction(Opcode::Load, type, { result }, { address }));

  return result;
}

void
Assembler::store(const Operand address, const Operand value)
{
  assert(address.is_valid() && value.is_valid());
  assert(is_numeric_type(value.type()));
  assert(address.type() == Type::Integer32); /* XXX */

  m_ebb->add_instruction(
    Instruction(Opcode::Store, value.type(), {}, { address, value }));

  return;
}

Operand
Assembler::rotr(const Operand value, const Operand count)
{
  assert(value.is_valid() && count.is_valid());
  assert(is_integer_type(value.type()));
  assert(is_integer_type(count.type()));

  const Operand result = m_ebb->allocate_register(value.type());
  m_ebb->add_instruction(
    Instruction(Opcode::RotateRight, value.type(), { result }, { value, count }));

  return result;
}

Operand
Assembler::rotl(const Operand value, const Operand count)
{
  assert(value.is_valid() && count.is_valid());
  assert(is_integer_type(value.type()));
  assert(is_integer_type(count.type()));

  const Operand result = m_ebb->allocate_register(value.type());
  m_ebb->add_instruction(
    Instruction(Opcode::RotateLeft, value.type(), { result }, { value, count }));

  return result;
}

Operand
Assembler::shiftr(const Operand value, const Operand count)
{
  assert(value.is_valid() && count.is_valid());
  assert(is_integer_type(value.type()));
  assert(is_integer_type(count.type()));

  const Operand result = m_ebb->allocate_register(value.type());
  m_ebb->add_instruction(
    Instruction(Opcode::LogicalShiftRight, value.type(), { result }, { value, count }));

  return result;
}

Operand
Assembler::shiftl(const Operand value, const Operand count)
{
  assert(value.is_valid() && count.is_valid());
  assert(is_integer_type(value.type()));
  assert(is_integer_type(count.type()));

  const Operand result = m_ebb->allocate_register(value.type());
  m_ebb->add_instruction(
    Instruction(Opcode::LogicalShiftLeft, value.type(), { result }, { value, count }));

  return result;
}

Operand
Assembler::ashiftr(const Operand value, const Operand count)
{
  assert(value.is_valid() && count.is_valid());
  assert(is_integer_type(value.type()));
  assert(is_integer_type(count.type()));

  const Operand result = m_ebb->allocate_register(value.type());
  m_ebb->add_instruction(Instruction(
    Opcode::ArithmeticShiftRight, value.type(), { result }, { value, count }));

  return result;
}

Operand
Assembler::_and(const Operand source_a, const Operand source_b)
{
  assert(source_a.is_valid() && source_b.is_valid());
  assert(is_integer_type(source_a.type()) || source_a.type() == Type::Bool);
  assert(source_a.type() == source_b.type());

  const Operand result = m_ebb->allocate_register(source_a.type());
  m_ebb->add_instruction(
    Instruction(Opcode::And, source_a.type(), { result }, { source_a, source_b }));

  return result;
}

Operand
Assembler::_or(const Operand source_a, const Operand source_b)
{
  assert(source_a.is_valid() && source_b.is_valid());
  assert(is_integer_type(source_a.type()) || source_a.type() == Type::Bool);
  assert(source_a.type() == source_b.type());

  const Operand result = m_ebb->allocate_register(source_a.type());
  m_ebb->add_instruction(
    Instruction(Opcode::Or, source_a.type(), { result }, { source_a, source_b }));

  return result;
}

Operand
Assembler::_xor(const Operand source_a, const Operand source_b)
{
  assert(source_a.is_valid() && source_b.is_valid());
  assert(is_integer_type(source_a.type()) || source_a.type() == Type::Bool);
  assert(source_a.type() == source_b.type());

  const Operand result = m_ebb->allocate_register(source_a.type());
  m_ebb->add_instruction(Instruction(
    Opcode::ExclusiveOr, source_a.type(), { result }, { source_a, source_b }));

  return result;
}

Operand
Assembler::_not(const Operand source)
{
  assert(source.is_valid());
  assert(is_integer_type(source.type()) || source.type() == Type::Bool);

  const Operand result = m_ebb->allocate_register(source.type());
  m_ebb->add_instruction(Instruction(Opcode::Not, source.type(), { result }, { source }));

  return result;
}

Operand
Assembler::bsc(const Operand value, const Operand control, const Operand position)
{
  assert(control.is_valid() && position.is_valid() && value.is_valid());
  assert(is_integer_type(value.type()));
  assert(is_integer_type(position.type()));
  assert(control.type() == Type::Bool);

  const Operand result = m_ebb->allocate_register(value.type());
  m_ebb->add_instruction(Instruction(
    Opcode::BitSetClear, value.type(), { result }, { value, control, position }));

  return result;
}

Operand
Assembler::add(const Operand source_a, const Operand source_b)
{
  assert(source_a.is_valid() && source_b.is_valid());
  assert(is_numeric_type(source_a.type()));
  assert(source_a.type() == source_b.type());

  const Operand result = m_ebb->allocate_register(source_a.type());
  m_ebb->add_instruction(
    Instruction(Opcode::Add, source_a.type(), { result }, { source_a, source_b }));

  return result;
}

Operand
Assembler::sub(const Operand source_a, const Operand source_b)
{
  assert(source_a.is_valid() && source_b.is_valid());
  assert(is_numeric_type(source_a.type()));
  assert(source_a.type() == source_b.type());

  const Operand result = m_ebb->allocate_register(source_a.type());
  m_ebb->add_instruction(
    Instruction(Opcode::Subtract, source_a.type(), { result }, { source_a, source_b }));

  return result;
}

Operand
Assembler::mul(const Operand source_a, const Operand source_b)
{
  assert(source_a.is_valid() && source_b.is_valid());
  assert(is_numeric_type(source_a.type()));
  assert(source_a.type() == source_b.type());

  const Operand result = m_ebb->allocate_register(source_a.type());
  m_ebb->add_instruction(
    Instruction(Opcode::Multiply, source_a.type(), { result }, { source_a, source_b }));

  return result;
}

Operand
Assembler::umul(const Operand source_a, const Operand source_b)
{
  assert(source_a.is_valid() && source_b.is_valid());
  assert(is_integer_type(source_a.type()));
  assert(source_a.type() == source_b.type());

  const Operand result = m_ebb->allocate_register(source_a.type());
  m_ebb->add_instruction(
    Instruction(Opcode::Multiply_u, source_a.type(), { result }, { source_a, source_b }));

  return result;
}

Operand
Assembler::div(const Operand source_a, const Operand source_b)
{
  assert(source_a.is_valid() && source_b.is_valid());
  assert(is_numeric_type(source_a.type()));
  assert(source_a.type() == source_b.type());

  const Operand result = m_ebb->allocate_register(source_a.type());
  m_ebb->add_instruction(
    Instruction(Opcode::Divide, source_a.type(), { result }, { source_a, source_b }));

  return result;
}

Operand
Assembler::udiv(const Operand source_a, const Operand source_b)
{
  assert(source_a.is_valid() && source_b.is_valid());
  assert(is_integer_type(source_a.type()));
  assert(source_a.type() == source_b.type());

  const Operand result = m_ebb->allocate_register(source_a.type());
  m_ebb->add_instruction(
    Instruction(Opcode::Divide_u, source_a.type(), { result }, { source_a, source_b }));

  return result;
}

Operand
Assembler::mod(const Operand source_a, const Operand source_b)
{
  assert(source_a.is_valid() && source_b.is_valid());
  assert(is_numeric_type(source_a.type()));
  assert(source_a.type() == source_b.type());

  const Operand result = m_ebb->allocate_register(source_a.type());
  m_ebb->add_instruction(
    Instruction(Opcode::Modulus, source_a.type(), { result }, { source_a, source_b }));

  return result;
}

Operand
Assembler::sqrt(const Operand source)
{
  assert(source.is_valid());
  assert(is_float_type(source.type()));

  const Operand result = m_ebb->allocate_register(source.type());
  m_ebb->add_instruction(
    Instruction(Opcode::SquareRoot, result.type(), { result }, { source }));

  return result;
}

Operand
Assembler::extend16(const Operand source)
{
  assert(source.is_valid());
  assert(source.type() == Type::Integer8);

  const Operand result = m_ebb->allocate_register(Type::Integer16);
  m_ebb->add_instruction(
    Instruction(Opcode::Extend16, Type::Integer8, { result }, { source }));

  return result;
}

Operand
Assembler::extend32(const Operand source)
{
  assert(source.is_valid());
  assert(source.type() == Type::Integer8 || source.type() == Type::Integer16);

  const Operand result = m_ebb->allocate_register(Type::Integer32);
  m_ebb->add_instruction(
    Instruction(Opcode::Extend32, source.type(), { result }, { source }));

  return result;
}

Operand
Assembler::extend64(const Operand source)
{
  assert(source.is_valid());
  assert(source.type() == Type::Integer8 || source.type() == Type::Integer16 ||
         source.type() == Type::Integer32);

  const Operand result = m_ebb->allocate_register(Type::Integer64);
  m_ebb->add_instruction(
    Instruction(Opcode::Extend64, source.type(), { result }, { source }));

  return result;
}

Operand
Assembler::bitcast(const Type out_type, const Operand source)
{
  assert(is_numeric_type(out_type));
  assert(source.is_valid());
  assert(is_numeric_type(source.type()));

  /* Instead of forcing extra checks at call sites, just avoid emitting any
   * unnecessary conversions. */
  if (source.type() == out_type) {
    return source;
  }

  const Operand result = m_ebb->allocate_register(out_type);
  m_ebb->add_instruction(Instruction(Opcode::BitCast, out_type, { result }, { source }));

  return result;
}

Operand
Assembler::castf2i(const Type out_type, const Operand source)
{
  assert(is_integer_type(out_type));
  assert(source.is_valid());
  assert(is_float_type(source.type()));

  const Operand result = m_ebb->allocate_register(out_type);
  m_ebb->add_instruction(
    Instruction(Opcode::CastFloatInt, out_type, { result }, { source }));

  return result;
}

Operand
Assembler::casti2f(const Type out_type, const Operand source)
{
  assert(is_float_type(out_type));
  assert(source.is_valid());
  assert(is_integer_type(source.type()));

  const Operand result = m_ebb->allocate_register(out_type);
  m_ebb->add_instruction(
    Instruction(Opcode::CastIntFloat, out_type, { result }, { source }));

  return result;
}

Operand
Assembler::resizef(const Type out_type, const Operand source)
{
  assert(is_float_type(out_type));
  assert(source.is_valid());
  assert(is_float_type(source.type()));
  assert(source.type() != out_type);

  const Operand result = m_ebb->allocate_register(out_type);
  m_ebb->add_instruction(
    Instruction(Opcode::ResizeFloat, out_type, { result }, { source }));

  return result;
}

Operand
Assembler::test(const Operand source_a, const Operand source_b)
{
  assert(source_a.is_valid() && source_b.is_valid());
  assert(is_integer_type(source_a.type()));
  assert(source_a.type() == source_b.type());

  const Operand result = m_ebb->allocate_register(Type::Bool);
  m_ebb->add_instruction(
    Instruction(Opcode::Test, source_a.type(), { result }, { source_a, source_b }));

  return result;
}

Operand
Assembler::cmp_eq(const Operand source_a, const Operand source_b)
{
  assert(source_a.is_valid() && source_b.is_valid());
  assert(source_a.type() == source_b.type());
  assert(is_numeric_type(source_a.type()) || source_a.type() == Type::Bool);

  const Operand result = m_ebb->allocate_register(Type::Bool);
  m_ebb->add_instruction(
    Instruction(Opcode::Compare_eq, source_a.type(), { result }, { source_a, source_b }));

  return result;
}

Operand
Assembler::cmp_lt(const Operand source_a, const Operand source_b)
{
  assert(source_a.is_valid() && source_b.is_valid());
  assert(is_numeric_type(source_a.type()));
  assert(source_a.type() == source_b.type());

  const Operand result = m_ebb->allocate_register(Type::Bool);
  m_ebb->add_instruction(
    Instruction(Opcode::Compare_lt, source_a.type(), { result }, { source_a, source_b }));

  return result;
}

Operand
Assembler::cmp_lte(const Operand source_a, const Operand source_b)
{
  assert(source_a.is_valid() && source_b.is_valid());
  assert(is_numeric_type(source_a.type()));
  assert(source_a.type() == source_b.type());

  const Operand result = m_ebb->allocate_register(Type::Bool);
  m_ebb->add_instruction(Instruction(
    Opcode::Compare_lte, source_a.type(), { result }, { source_a, source_b }));

  return result;
}

Operand
Assembler::cmp_gt(const Operand source_a, const Operand source_b)
{
  assert(source_a.is_valid() && source_b.is_valid());
  assert(is_numeric_type(source_a.type()));
  assert(source_a.type() == source_b.type());

  const Operand result = m_ebb->allocate_register(Type::Bool);
  m_ebb->add_instruction(
    Instruction(Opcode::Compare_lt, source_a.type(), { result }, { source_b, source_a }));

  return result;
}

Operand
Assembler::cmp_gte(const Operand source_a, const Operand source_b)
{
  assert(source_a.is_valid() && source_b.is_valid());
  assert(is_numeric_type(source_a.type()));
  assert(source_a.type() == source_b.type());

  const Operand result = m_ebb->allocate_register(Type::Bool);
  m_ebb->add_instruction(Instruction(
    Opcode::Compare_lte, source_a.type(), { result }, { source_b, source_a }));

  return result;
}

Operand
Assembler::cmp_ult(const Operand source_a, const Operand source_b)
{
  assert(source_a.is_valid() && source_b.is_valid());
  assert(is_integer_type(source_a.type()));
  assert(source_a.type() == source_b.type());

  const Operand result = m_ebb->allocate_register(Type::Bool);
  m_ebb->add_instruction(Instruction(
    Opcode::Compare_ult, source_a.type(), { result }, { source_a, source_b }));

  return result;
}

Operand
Assembler::cmp_ulte(const Operand source_a, const Operand source_b)
{
  assert(source_a.is_valid() && source_b.is_valid());
  assert(is_integer_type(source_a.type()));
  assert(source_a.type() == source_b.type());

  const Operand result = m_ebb->allocate_register(Type::Bool);
  m_ebb->add_instruction(Instruction(
    Opcode::Compare_ulte, source_a.type(), { result }, { source_a, source_b }));

  return result;
}

Operand
Assembler::cmp_ugt(const Operand source_a, const Operand source_b)
{
  assert(source_a.is_valid() && source_b.is_valid());
  assert(is_integer_type(source_a.type()));
  assert(source_a.type() == source_b.type());

  const Operand result = m_ebb->allocate_register(Type::Bool);
  m_ebb->add_instruction(Instruction(
    Opcode::Compare_ult, source_a.type(), { result }, { source_b, source_a }));

  return result;
}

Operand
Assembler::cmp_ugte(const Operand source_a, const Operand source_b)
{
  assert(source_a.is_valid() && source_b.is_valid());
  assert(is_integer_type(source_a.type()));
  assert(source_a.type() == source_b.type());

  const Operand result = m_ebb->allocate_register(Type::Bool);
  m_ebb->add_instruction(Instruction(
    Opcode::Compare_ulte, source_a.type(), { result }, { source_b, source_a }));

  return result;
}

void
Assembler::br(const Operand target)
{
  assert(target.is_valid());
  assert(target.type() == Type::BranchLabel);

  m_ebb->add_instruction(Instruction(Opcode::Branch, Type::Integer64, {}, { target }));
}

void
Assembler::ifbr(const Operand decision, const Operand target)
{
  assert(decision.is_valid() && target.is_valid());
  assert(decision.type() == Type::Bool);
  assert(target.type() == Type::BranchLabel);

  m_ebb->add_instruction(
    Instruction(Opcode::IfBranch, Type::Integer64, {}, { decision, target }));
}

Operand
Assembler::select(const Operand decision, const Operand if_false, const Operand if_true)
{
  assert(decision.is_valid() && if_false.is_valid() && if_true.is_valid());
  assert(decision.type() == Type::Bool);
  assert(is_numeric_type(if_false.type()));
  assert(if_false.type() == if_true.type());

  const Operand result = m_ebb->allocate_register(if_false.type());
  m_ebb->add_instruction(Instruction(
    Opcode::Select, result.type(), { result }, { decision, if_false, if_true }));

  return result;
}

void
Assembler::exit(const Operand decision, const Operand result)
{
  assert(decision.is_valid());
  assert(decision.type() == Type::Bool);
  assert(result.type() == Type::Integer64);

  m_ebb->add_instruction(
    Instruction(Opcode::Exit, Type::Integer64, {}, { decision, result }));
}

void
Assembler::call(void (*host_function)(Guest *))
{
  static_assert(sizeof(host_function) == sizeof(void *));

  const Operand function(Type::HostAddress,
                         Constant { .hostptr_value = (void *)host_function });
  m_ebb->add_instruction(Instruction(Opcode::Call, Type::Integer64, {}, { function }));
}

Operand
Assembler::call(const Type return_type, Constant (*host_function)(Guest *))
{
  static_assert(sizeof(host_function) == sizeof(void *));
  assert(is_numeric_type(return_type) || return_type == Type::Bool);

  const Operand function(Type::HostAddress,
                         Constant { .hostptr_value = (void *)host_function });
  const Operand result = m_ebb->allocate_register(return_type);
  m_ebb->add_instruction(
    Instruction(Opcode::Call, return_type, { result }, { function }));

  return result;
}

Operand
Assembler::call(const Type return_type,
                Constant (*const host_function)(Guest *, Constant),
                const Operand arg1)
{
  static_assert(sizeof(host_function) == sizeof(void *));
  assert(is_numeric_type(return_type) || return_type == Type::Bool);
  assert(arg1.is_valid());
  assert(is_numeric_type(arg1.type()) || arg1.type() == Type::Bool);

  const Operand function(Type::HostAddress,
                         Constant { .hostptr_value = (void *)host_function });
  const Operand result = m_ebb->allocate_register(return_type);
  m_ebb->add_instruction(
    Instruction(Opcode::Call, return_type, { result }, { function, arg1 }));

  return result;
}

Operand
Assembler::call(const Type return_type,
                Constant (*const host_function)(Guest *, Constant, Constant),
                const Operand arg1,
                const Operand arg2)
{
  static_assert(sizeof(host_function) == sizeof(void *));
  assert(is_numeric_type(return_type) || return_type == Type::Bool);
  assert(arg1.is_valid() && arg2.is_valid());
  assert(is_numeric_type(arg1.type()) || arg1.type() == Type::Bool);
  assert(is_numeric_type(arg2.type()) || arg2.type() == Type::Bool);

  const Operand function(Type::HostAddress,
                         Constant { .hostptr_value = (void *)host_function });
  const Operand result = m_ebb->allocate_register(return_type);
  m_ebb->add_instruction(
    Instruction(Opcode::Call, return_type, { result }, { function, arg1, arg2 }));

  return result;
}

/*!
 * @brief Return the generated ExecutionUnit and clear internal state to
 *        prepare for assembly of a new unit. Should be called by the guest-
 *        specific implementations of assemble().
 */
ExecutionUnit &&
Assembler::export_unit()
{
  return std::move(*m_ebb);
}

u32
Assembler::instruction_count() const
{
  return m_ebb->instructions().size();
}

}
}
