#pragma once

#include "fox/ir_operand.h"
#include "fox/guest.h"

namespace fox {
namespace ir {

/*!
 * @class fox::ir::Calculator
 * @brief Class capable of executing all constant-evaluable IR opcodes.
 *
 * This can be used to simplify code that evaluates constant evaluation passes
 * for IR optimization.
 */
class Calculator {
public:
  Calculator();

  /*
   * Methods to append instructions to the assembly stream. Most will return a
   * new Operand to reference their result register.
   */

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
  Operand select(Operand decision, Operand if_false, Operand if_true);
};

}
}
