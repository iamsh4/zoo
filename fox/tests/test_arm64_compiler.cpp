// vim: expandtab:ts=2:sw=2

#include <string>
#include <cstdio>
#include <cstring>
#include <cassert>

#include <sys/mman.h>
#include <cstring>

#include "fox/ir_assembler.h"
#include "arm64/arm64_compiler.h"
#include "fox/tests/dummy.h"

using namespace fox;
using namespace fox::codegen::arm64;
using fox::ir::Operand;

void
test()
{
  DummyAssembler assembler;

  const Operand pi = Operand::constant<u32>(0x314u);

  assembler.writegr(Operand::constant<u32>(1u), pi);
  auto read_reg = assembler.readgr(ir::Type::Integer32, Operand::constant<u32>(1u));
  assembler.writegr(Operand::constant<u32>(2u), read_reg);
  ir::ExecutionUnit unit = assembler.assemble();

  std::unique_ptr<jit::Routine> routine = Compiler().compile(std::move(unit));
  routine->disassemble();
}

void
host_call()
{
  DummyAssembler assembler;

  assembler.call([](Guest *guest) { return; });
  const Operand pass1 = assembler.call(ir::Type::Integer64, [](Guest *guest) {
    // printf("[host] guest=%p\n", guest);
    return ir::Constant { .u64_value = 5 };
  });
  const Operand pass2 = assembler.call(
    ir::Type::Integer64,
    [](Guest *guest, ir::Constant arg1) {
      // printf("[host] guest=%p, arg1=%llu\n", guest, arg1.u64_value);
      return ir::Constant { .u64_value = arg1.u64_value * 2 };
    },
    pass1);
  const Operand pass3 = assembler.call(
    ir::Type::Integer64,
    [](Guest *guest, ir::Constant arg1, ir::Constant arg2) {
      // printf(
      //   "[host] guest=%p, arg1=%llu, arg2=%llu\n", guest, arg1.u64_value,
      //   arg2.u64_value);
      return ir::Constant { .u64_value = arg1.u64_value * 2 + arg2.u64_value };
    },
    pass1,
    pass2);

  assembler.writegr(Operand::constant<u32>(0u), pass3);

  const Operand always = Operand::constant<bool>(true);
  const Operand zero = Operand::constant<u64>(0u);
  assembler.exit(always, zero);

  Compiler compiler;
  compiler.set_register_address_cb([](unsigned index) { return index; });

  ir::ExecutionUnit unit = assembler.assemble();
  std::unique_ptr<codegen::Routine> routine = compiler.compile(unit.copy());
  routine->prepare(true);

  DummyGuest guest;
  routine->execute(&guest, nullptr, guest.register_base());

  // ASSERT_EQ(20lu, guest.registers()[0]);
}

int
main()
{
  // test();
  host_call();
  return 0;
}
