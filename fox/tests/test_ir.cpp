// vim: expandtab:ts=2:sw=2

#include "fox/ir_assembler.h"
#include "fox/ir/execution_unit.h"
#include "fox/ir/optimizer.h"

using namespace fox;
using ir::Operand;

/*!
 * @brief Assembler implementation that just natively uses the IR bytecode
 *        without any translation from a guest CPU.
 */
class DummyAssembler : public ir::Assembler {
public:
  ir::ExecutionUnit &&assemble()
  {
    return export_unit();
  }
};

void
test_assembler_print()
{
  printf("======== test_assembler_print ========\n");

  DummyAssembler assembler;

  const ir::Operand zero = Operand::constant<u32>(0u);
  const ir::Operand two = Operand::constant<u32>(2u);
  const ir::Operand boolean = Operand::constant<bool>(true);
  const ir::Operand gpr0 = assembler.readgr(ir::Type::Integer32, Operand::constant<u16>(0));
  const ir::Operand gpr1 = assembler.readgr(ir::Type::Integer32, Operand::constant<u16>(1));
  const ir::Operand gpr2 = assembler.readgr(ir::Type::Integer32, Operand::constant<u16>(2));
  const ir::Operand gpr3 = assembler.readgr(ir::Type::Integer32, Operand::constant<u16>(3));

  assembler.load(ir::Type::Integer32, gpr0);
  assembler.store(gpr0, gpr1);

  assembler.rotr(gpr1, gpr3);
  assembler.rotl(gpr2, gpr1);
  assembler.shiftr(gpr3, gpr1);
  assembler.shiftl(gpr2, gpr0);
  assembler._and(gpr2, gpr0);
  assembler._or(gpr2, gpr0);
  assembler._xor(gpr2, gpr0);
  assembler._not(gpr1);
  assembler.bsc(gpr0, boolean, two);

  assembler.add(gpr1, gpr2);
  assembler.sub(gpr3, gpr0);
  assembler.mul(gpr1, gpr2);
  assembler.umul(gpr1, gpr2);
  assembler.div(gpr3, gpr1);
  assembler.udiv(gpr3, gpr1);
  assembler.mod(gpr2, gpr0);
  assembler.sqrt(assembler.bitcast(ir::Type::Float32, gpr0));

  const ir::Operand tmp1 = assembler.bitcast(ir::Type::Integer8, gpr1);
  assembler.extend16(tmp1);
  assembler.extend32(tmp1);
  assembler.extend64(tmp1);
  const ir::Operand tmp2 = assembler.casti2f(ir::Type::Float32, gpr1);
  assembler.castf2i(ir::Type::Integer32, tmp2);
  assembler.resizef(ir::Type::Float64, tmp2);

  assembler.test(gpr0, gpr1);
  assembler.cmp_eq(gpr0, gpr1);
  assembler.cmp_lt(gpr0, gpr1);
  assembler.cmp_lte(gpr0, gpr1);
  assembler.cmp_gt(gpr0, gpr1);
  assembler.cmp_gte(gpr0, gpr1);
  assembler.cmp_ult(gpr0, gpr1);
  assembler.cmp_ulte(gpr0, gpr1);
  assembler.cmp_ugt(gpr0, gpr1);
  assembler.cmp_ugte(gpr0, gpr1);
  const ir::Operand decision = assembler.cmp_ugte(gpr0, gpr1);

  //assembler.br(zero); /* TODO Need branch labels */
  //assembler.ifbr(decision, zero); /* TODO Need branch labels */
  assembler.select(decision, zero, gpr0);
  assembler.exit(boolean, ir::Operand::constant<u64>(0u));
  assembler.call(ir::Type::Integer64, nullptr,
                 ir::Operand::constant<u64>(0u),
                 ir::Operand::constant<u64>(1u));

  const ir::ExecutionUnit unit = assembler.assemble();
  unit.debug_print();

  printf("\n");
}

void
test_assembler_assemble()
{
  printf("======== test_assembler_assemble ========\n");

  DummyAssembler assembler;

  const ir::Operand gpr0 = assembler.readgr(ir::Type::Integer32, Operand::constant<u16>(0));
  const ir::Operand gpr1 = assembler.readgr(ir::Type::Integer32, Operand::constant<u16>(1));

  assembler.add(gpr0, gpr1);
  assembler.add(gpr0, gpr1);

  ir::ExecutionUnit unit = assembler.assemble();
  unit.debug_print();

  printf("\n");
}

int
main(int argc, char *argv[])
{
  test_assembler_print();
  test_assembler_assemble();
  return 0;
}
