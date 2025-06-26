// vim: expandtab:ts=2:sw=2

#include <functional>
#include <fmt/core.h>
#include <gtest/gtest.h>

#include "amd64/amd64_compiler.h"
#include "fox/tests/dummy.h"

using namespace fox;
using namespace fox::codegen::amd64;
using fox::ir::Operand;

TEST(AMD64Compiler, MultiplyUnsigned)
{
  DummyAssembler assembler;

  const Operand always = Operand::constant<bool>(true);
  const Operand zero = Operand::constant<u64>(0u);
  const Operand in1 = assembler.readgr(ir::Type::Integer32, Operand::constant<u16>(0u));
  const Operand in2 = assembler.readgr(ir::Type::Integer32, Operand::constant<u16>(1u));
  const Operand result = assembler.umul(in1, in2);
  assembler.writegr(Operand::constant<u16>(2u), result);
  assembler.exit(always, zero);

  Compiler compiler;
  compiler.set_register_address_cb([](unsigned index) {
    const Register<QWORD> opaque(Compiler::gpr_guest_registers);
    return RegMemAny(Address<ANY>(opaque, index * sizeof(u32)));
  });

  ir::ExecutionUnit unit = assembler.assemble();
  std::unique_ptr<codegen::Routine> routine = compiler.compile(unit.copy());
  routine->prepare(true);

  DummyGuest guest;
  guest.registers()[0] = 5;
  guest.registers()[1] = 7;
  routine->execute(&guest, nullptr, guest.register_base());

  ASSERT_EQ(35u, guest.registers()[2]);
}

TEST(AMD64Compiler, MultiplySigned)
{
  DummyAssembler assembler;

  const Operand always = Operand::constant<bool>(true);
  const Operand zero = Operand::constant<u64>(0u);
  const Operand in1 = assembler.readgr(ir::Type::Integer32, Operand::constant<u16>(0u));
  const Operand in2 = assembler.readgr(ir::Type::Integer32, Operand::constant<u16>(1u));
  const Operand result = assembler.mul(in1, in2);
  assembler.writegr(Operand::constant<u16>(2u), result);
  assembler.exit(always, zero);

  Compiler compiler;
  compiler.set_register_address_cb([](unsigned index) {
    const Register<QWORD> opaque(Compiler::gpr_guest_registers);
    return RegMemAny(Address<ANY>(opaque, index * sizeof(u32)));
  });

  ir::ExecutionUnit unit = assembler.assemble();
  std::unique_ptr<codegen::Routine> routine = compiler.compile(unit.copy());
  routine->prepare(true);

  DummyGuest guest;
  guest.registers()[0] = (u32)-5;
  guest.registers()[1] = (u32)7;
  routine->execute(&guest, nullptr, guest.register_base());

  ASSERT_EQ(-35, (i32)guest.registers()[2]);
}

TEST(AMD64Compiler, HostCall)
{
  DummyAssembler assembler;

  assembler.call([](Guest *guest) { return; });
  const Operand pass1 =
    assembler.call(ir::Type::Integer64, [](Guest *guest) {
      fmt::print("[host] guest={}\n", (void*)guest);
      return Value{ .u64_value = 5 };
    });
  const Operand pass2 =
    assembler.call(ir::Type::Integer64, [](Guest *guest, Value arg1) {
      fmt::print("[host] guest={}, arg1={}\n", (void*)guest, arg1.u64_value);
      return Value{ .u64_value = arg1.u64_value * 2 };
    }, pass1);
  const Operand pass3 =
    assembler.call(ir::Type::Integer64, [](Guest *guest, Value arg1, Value arg2) {
      fmt::print("[host] guest={}, arg1={}, arg2={}\n", (void*)guest, arg1.u64_value, arg2.u64_value);
      return Value{ .u64_value = arg1.u64_value * 2 + arg2.u64_value };
    }, pass1, pass2);

  assembler.writegr(Operand::constant<u16>(0u), pass3);

  const Operand always = Operand::constant<bool>(true);
  const Operand zero = Operand::constant<u64>(0u);
  assembler.exit(always, zero);

  Compiler compiler;
  compiler.set_register_address_cb([](unsigned index) {
    const Register<QWORD> opaque(Compiler::gpr_guest_registers);
    return RegMemAny(Address<ANY>(opaque, index * sizeof(u32)));
  });

  ir::ExecutionUnit unit = assembler.assemble();
  std::unique_ptr<codegen::Routine> routine = compiler.compile(unit.copy());
  routine->prepare(true);

  DummyGuest guest;
  routine->execute(&guest, nullptr, guest.register_base());

  ASSERT_EQ(20lu, guest.registers()[0]);
}

int
main(int argc, char *argv[])
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
