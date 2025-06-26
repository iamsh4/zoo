// vim: expandtab:ts=2:sw=2

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cassert>

#include "fox/fox_types.h"
#include "fox/ir_operand.h"

namespace fox {

/* TODO */
class Guest;

namespace ir {

class ExecutionUnit;

/*!
 * @class fox::ir::Assembler
 * @brief State for creating an IR program. Provides methods that can be
 *        called to append instructions to EBBs in the resulting IR program.
 */
class Assembler {
public:
  Assembler();
  ~Assembler();

  /*!
   * @brief Allocate a new IR register of the given type. Used internally to
   *        create opcode destination registers.
   */
  Operand allocate_register(Type type);

  /*
   * Methods to append instructions to the assembly stream. Most will return a
   * new Operand to reference their result register.
   */

  /* Guest register operations. Indexes must be constant values. */
  Operand readgr(Type type, Operand index);
  void writegr(Operand index, Operand value);

  /* Memory operations. */
  Operand load(Type type, Operand address);
  void store(Operand address, Operand value);

  /* Bit operations - integer targets only. */
  Operand rotr(Operand value, Operand count);
  Operand rotl(Operand value, Operand count);
  Operand shiftr(Operand value, Operand count);
  Operand shiftl(Operand value, Operand count);
  Operand ashiftr(Operand value, Operand count);
  Operand _and(Operand source_a, Operand source_b);
  Operand _or(Operand source_a, Operand source_b);
  Operand _xor(Operand source_a, Operand source_b);
  Operand _not(Operand source);
  Operand bsc(Operand value, Operand control, Operand position);

  /* Arithmetic operations */
  Operand add(Operand source_a, Operand source_b);
  Operand sub(Operand source_a, Operand source_b);
  Operand mul(Operand source_a, Operand source_b);
  Operand umul(Operand source_a, Operand source_b);
  Operand div(Operand source_a, Operand source_b);
  Operand udiv(Operand source_a, Operand source_b);
  Operand mod(Operand source_a, Operand source_b);
  Operand sqrt(Operand source);

  /* Conversion operations. */
  Operand extend16(Operand source);
  Operand extend32(Operand source);
  Operand extend64(Operand source);
  Operand bitcast(Type out_type, Operand source);
  Operand castf2i(Type out_type, Operand source);
  Operand casti2f(Type out_type, Operand source);
  Operand resizef(Type out_type, Operand source);

  /* Comparison operations. */
  Operand test(Operand source_a, Operand source_b);
  Operand cmp_eq(Operand source_a, Operand source_b);
  Operand cmp_lt(Operand source_a, Operand source_b);
  Operand cmp_lte(Operand source_a, Operand source_b);
  Operand cmp_gt(Operand source_a, Operand source_b);
  Operand cmp_gte(Operand source_a, Operand source_b);
  Operand cmp_ult(Operand source_a, Operand source_b);
  Operand cmp_ulte(Operand source_a, Operand source_b);
  Operand cmp_ugt(Operand source_a, Operand source_b);
  Operand cmp_ugte(Operand source_a, Operand source_b);

  /* Control flow operations. */
  void br(Operand target);
  void ifbr(Operand decision, Operand target);
  Operand select(Operand decision, Operand if_false, Operand if_true);
  void exit(Operand decision, Operand result);

  /* Host function call variants. */
  void call(void (*host_function)(Guest *));
  Operand call(Type return_type, Constant (*host_function)(Guest *));
  Operand call(Type return_type,
               Constant (*host_function)(Guest *, Constant),
               Operand arg1);
  Operand call(Type return_type,
               Constant (*host_function)(Guest *, Constant, Constant),
               Operand arg1,
               Operand arg2);

protected:
  /*!
   * @brief Return the generated ExecutionUnit and clear internal state to
   *        prepare for assembly of a new unit. Should be called by the guest-
   *        specific implementations of assemble().
   */
  ExecutionUnit &&export_unit();

  /*!
   * @brief Return the total number of instructions stored in the current
   *        assembler instance.
   */
  u32 instruction_count() const;

private:
  /*!
   * @brief Container for the generated IR program.
   */
  std::unique_ptr<ExecutionUnit> m_ebb;
};

}
}
