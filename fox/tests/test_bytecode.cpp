// vim: expandtab:ts=2:sw=2

#include <cstring>
#include <fmt/core.h>

#include "fox/memtable.h"
#include "fox/ir_assembler.h"
#include "fox/guest.h"
#include "fox/bytecode/compiler.h"
#include "fox/bytecode/opcode.h"
#include "fox/tests/dummy.h"

using namespace fox;
using fox::ir::Operand;

/* Helper function for testing host function calls from bytecode. */
Value
host_function(Guest *const guest, const Value arg)
{
  fmt::print("[host_function] {:#018x}\n", arg.u64_value);

  return Value{ .u64_value = (arg.u64_value & 0xff00) };
}

void
test_run_bytecode()
{
  printf("======== test_run_bytecode ========\n");

  u8 *const data = new u8[1024lu];
  size_t offset = 0;

  /* Load 8-bit constant to R0 and zero extend. */
  data[offset++] = (u8)bytecode::Opcodes::Constant8;
  data[offset++] = 0x00;
  data[offset++] = 0x99;

  /* Load 16-bit constant R1 and sign extend. */
  data[offset++] = (u8)bytecode::Opcodes::ExtendConstant16;
  data[offset++] = 0x01;
  data[offset++] = 0xff;
  data[offset++] = 0xff;

  /* Write R1 to G1 */
  data[offset++] = (u8)bytecode::Opcodes::WriteRegister32;
  data[offset++] = 0x01;
  data[offset++] = 0x01;
  data[offset++] = 0x00;

  /* Load G1 to R2 */
  data[offset++] = (u8)bytecode::Opcodes::ReadRegister32;
  data[offset++] = 0x02;
  data[offset++] = 0x01;
  data[offset++] = 0x00;

  /* Store R2 to G2 */
  data[offset++] = (u8)bytecode::Opcodes::WriteRegister32;
  data[offset++] = 0x02;
  data[offset++] = 0x02;
  data[offset++] = 0x00;

  /* Load constant to R3 and zero extend. */
  data[offset++] = (u8)bytecode::Opcodes::Constant8;
  data[offset++] = 0x03;
  data[offset++] = 0x05;

  /* Load a 64-bit constant (function pointer) into R4 */
  {
    static_assert(sizeof(uintptr_t) == sizeof(u64));
    const uintptr_t address = reinterpret_cast<uintptr_t>(&host_function);
    data[offset++] = (u8)bytecode::Opcodes::Constant64;
    data[offset++] = 0x04;
    memcpy(&data[offset], &address, sizeof(address));
    offset += 8;
  }

  /* Execute upcall to guest with argument R3 and store result in R3 */
  data[offset++] = (u8)bytecode::Opcodes::HostCall1;
  data[offset++] = 0x43;
  data[offset++] = 0x03;
  data[offset++] = 0x00;

  /* Store R3 to G3 */
  data[offset++] = (u8)bytecode::Opcodes::WriteRegister32;
  data[offset++] = 0x03;
  data[offset++] = 0x03;
  data[offset++] = 0x00;

  /* Load 8-bit constant to R4 and zero extend. */
  data[offset++] = (u8)bytecode::Opcodes::Constant8;
  data[offset++] = 0x04;
  data[offset++] = 0x04;

  /* Rotate lower 16 bits of R0 by R4 bits right. */
  data[offset++] = (u8)bytecode::Opcodes::RotateRight16;
  data[offset++] = 0x00;
  data[offset++] = 0x04;
  data[offset++] = 0x00;

  /* Write R0 to G0 */
  data[offset++] = (u8)bytecode::Opcodes::WriteRegister32;
  data[offset++] = 0x00;
  data[offset++] = 0x00;
  data[offset++] = 0x00;

  /* Exit */
  data[offset++] = (u8)bytecode::Opcodes::Exit;
  data[offset++] = 0x00;
  data[offset++] = 0x00;
  data[offset++] = 0x00;

  /* Store R3 to G0 (should not run) */
  data[offset++] = (u8)bytecode::Opcodes::WriteRegister32;
  data[offset++] = 0x03;
  data[offset++] = 0x00;
  data[offset++] = 0x00;

  bytecode::Routine routine(data, offset);
  DummyGuest guest;
  guest.print_state();
  routine.execute(&guest);
  guest.print_state();

  printf("\n");
}

void
test_compile_bytecode()
{
  printf("======== test_compile_bytecode ========\n");

  DummyAssembler assembler;

  const Operand always = Operand::constant<bool>(true);
  const Operand zero = Operand::constant<u64>(0u);
  const Operand one = Operand::constant<u64>(1u);
  const Operand two = Operand::constant<u64>(2u);
  const Operand address = Operand::constant<u32>(0x108u);
  const Operand smallneg = Operand::constant<u8>(0xffu);
  const Operand pi = Operand::constant<u32>(0x314u);

  assembler.call(ir::Type::Integer64, host_function, one);
  assembler.writegr(Operand::constant<u16>(0u), pi);
  Operand tmp = assembler.readgr(ir::Type::Integer32, Operand::constant<u16>(0u));
  assembler.writegr(Operand::constant<u16>(1u), assembler.add(tmp, pi));
  assembler.store(address, two);
  tmp = assembler.load(ir::Type::Integer32, address);
  assembler.writegr(Operand::constant<u16>(2u), tmp);
  assembler.writegr(Operand::constant<u16>(3u), assembler.extend32(assembler.extend16(smallneg)));
  assembler.exit(always, zero);

  ir::ExecutionUnit unit = assembler.assemble();
  bytecode::Compiler compiler;
  std::unique_ptr<jit::Routine> routine = compiler.compile(unit.copy());
  unit.debug_print();

  DummyGuest guest;
  guest.print_state();
  routine->execute(&guest);
  guest.print_state();

  printf("\n");
}

void
test_early_register_store()
{
  printf("======== test_early_register_store ========\n");

  DummyAssembler assembler;

  const Operand always = Operand::constant<bool>(true);
  const Operand zero = Operand::constant<u64>(0u);
  const Operand pi = Operand::constant<u32>(0x314u);

  Operand gpr0 = assembler.readgr(ir::Type::Integer32, Operand::constant<u16>(0u));
  Operand gpr1 = assembler.readgr(ir::Type::Integer32, Operand::constant<u16>(1u));
  Operand gpr2 = assembler.readgr(ir::Type::Integer32, Operand::constant<u16>(2u));
  gpr0 = assembler.add(gpr0, pi);
  gpr1 = assembler.add(gpr1, pi);
  gpr1 = assembler.add(gpr1, pi);
  gpr1 = assembler.add(gpr1, pi);
  gpr1 = assembler.add(gpr1, pi);
  gpr2 = assembler.add(gpr2, pi);
  gpr2 = assembler.add(gpr2, pi);
  gpr2 = assembler.add(gpr2, pi);
  gpr2 = assembler.readgr(ir::Type::Integer32, Operand::constant<u16>(1u));
  assembler.writegr(Operand::constant<u16>(0u), gpr0);
  assembler.writegr(Operand::constant<u16>(1u), gpr1);
  assembler.writegr(Operand::constant<u16>(2u), gpr2);
  assembler.exit(always, zero);

  ir::ExecutionUnit unit = assembler.assemble();
  unit.debug_print();

  bytecode::Compiler compiler;
  std::unique_ptr<jit::Routine> routine = compiler.compile(unit.copy());

  DummyGuest guest;
  guest.print_state();
  routine->execute(&guest);
  guest.print_state();

  printf("\n");
}

void
test_bit_operations()
{
  printf("======== test_bit_operations ========\n");

  DummyAssembler assembler;

  const Operand always = Operand::constant<bool>(true);
  const Operand never = Operand::constant<bool>(false);
  const Operand zero = Operand::constant<u64>(0u);
  const Operand two = Operand::constant<u64>(2u);

  Operand gpr0 = assembler.readgr(ir::Type::Integer32, Operand::constant<u16>(0u));
  Operand gpr1 = assembler.readgr(ir::Type::Integer32, Operand::constant<u16>(1u));
  gpr0 = assembler.bsc(gpr0, always, two);
  gpr1 = assembler.bsc(gpr0, never, two);
  assembler.writegr(Operand::constant<u16>(0u), gpr0);
  assembler.writegr(Operand::constant<u16>(1u), gpr1);
  assembler.exit(always, zero);

  ir::ExecutionUnit unit = assembler.assemble();
  unit.debug_print();

  bytecode::Compiler compiler;
  std::unique_ptr<jit::Routine> routine = compiler.compile(unit.copy());

  DummyGuest guest;
  guest.print_state();
  routine->execute(&guest);
  guest.print_state();

  printf("\n");
}

void
test_arithmetic_shift()
{
  printf("======== test_arithmetic_shift ========\n");

  DummyAssembler assembler;

  const Operand always = Operand::constant<bool>(true);
  const Operand target = Operand::constant<i32>(-32);
  const Operand zero = Operand::constant<u64>(0u);
  const Operand two = Operand::constant<u8>(2u);

  assembler.writegr(Operand::constant<u16>(0u), assembler.ashiftr(target, two));
  assembler.exit(always, zero);

  ir::ExecutionUnit unit = assembler.assemble();
  unit.debug_print();

  bytecode::Compiler compiler;
  std::unique_ptr<jit::Routine> routine = compiler.compile(unit.copy());

  DummyGuest guest;
  guest.print_state();
  routine->execute(&guest);
  guest.print_state();

  printf("\n");
}

int
main(int argc, char *argv[])
{
  test_run_bytecode();
  test_compile_bytecode();
  test_early_register_store();
  test_bit_operations();
  test_arithmetic_shift();
  return 0;
}
