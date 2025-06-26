#include <gtest/gtest.h>

#include "fox/ir_assembler.h"
#include "ir/optimizer.h"

using namespace fox;
using fox::ir::Operand;

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

TEST(ConstantPropagation, Simple)
{
  DummyAssembler assembler;

  const Operand zero = Operand::constant<u32>(0u);
  const Operand one = Operand::constant<u32>(1u);
  const Operand result =
    assembler.rotr(assembler._not(assembler.add(zero, one)), one);
  assembler.writegr(Operand::constant<u16>(0u), result);

  ir::ExecutionUnit unit = assembler.assemble();
  printf("== Input ==\n");
  unit.debug_print();

  ir::optimize::ConstantPropagation optimizer;
  unit = optimizer.execute(unit);

  printf("== Output ==\n");
  unit.debug_print();
}

TEST(ConstantPropagation, NonConstantInput)
{
  DummyAssembler assembler;

  const Operand zero = Operand::constant<u32>(0u);
  const Operand variable = assembler.readgr(ir::Type::Integer32, Operand::constant<u16>(1u));
  assembler.writegr(Operand::constant<u16>(0u), assembler._and(variable, zero));

  ir::ExecutionUnit unit = assembler.assemble();
  printf("== Input ==\n");
  unit.debug_print();

  ir::optimize::ConstantPropagation optimizer;
  unit = optimizer.execute(unit);

  printf("== Output ==\n");
  unit.debug_print();
}

TEST(DeadCodeElimination, DanglingOperation)
{
  DummyAssembler assembler;

  const Operand zero = Operand::constant<u32>(0u);
  const Operand load_result = assembler.load(ir::Type::Integer32, zero);
  assembler._not(load_result);

  ir::ExecutionUnit unit = assembler.assemble();
  printf("== Input ==\n");
  unit.debug_print();

  ir::optimize::DeadCodeElimination optimizer;
  unit = optimizer.execute(unit);

  printf("== Output ==\n");
  unit.debug_print();
}

TEST(DeadCodeElimination, NoDeadCode)
{
  DummyAssembler assembler;

  const Operand zero = Operand::constant<u32>(0u);
  const Operand load_result = assembler.load(ir::Type::Integer32, zero);
  const Operand not_result = assembler._not(load_result);
  assembler.store(zero, not_result);

  ir::ExecutionUnit unit = assembler.assemble();
  printf("== Input ==\n");
  unit.debug_print();

  ir::optimize::DeadCodeElimination optimizer;
  unit = optimizer.execute(unit);

  printf("== Output ==\n");
  unit.debug_print();
}

int
main(int argc, char *argv[])
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
