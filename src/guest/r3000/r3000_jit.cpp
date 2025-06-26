#include "fox/bytecode/compiler.h"
#include "fox/amd64/amd64_compiler.h"
#include "fox/arm64/arm64_compiler.h"
#include "fox/ir/optimizer.h"

#include "guest/r3000/r3000_jit.h"
#include "guest/r3000/r3000_ir.h"
#include "shared/profiling.h"

namespace guest::r3000 {

bool
R3000::BasicBlock::compile()
{
  ProfileZone;

  assert(!is_compiled());

  // Run optimizations
  if (0) {
    using namespace fox::ir::optimize;
    m_execution_unit = ConstantPropagation().execute(std::move(m_execution_unit.copy()));
    m_execution_unit = DeadCodeElimination().execute(std::move(m_execution_unit.copy()));
  }

  try {
    fox::bytecode::Compiler bytecode_compiler;
    m_bytecode = bytecode_compiler.compile(m_execution_unit.copy());

#ifdef __APPLE__
    if (1) {
      fox::codegen::arm64::Compiler compiler;
      compiler.set_use_fastmem(false);
      compiler.set_register_address_cb([](unsigned index) { return index; });

      m_native = compiler.compile(m_execution_unit.copy());
      m_native->prepare(true);
    }
#else
    fox::codegen::amd64::Compiler compiler;
    compiler.set_register_address_cb([](unsigned index) {
      using namespace fox::codegen::amd64;
      assert(index < Registers::NUM_REGS);
      const Register<QWORD> opaque(Compiler::gpr_guest_registers);
      return RegMemAny(Address<ANY>(opaque, index * sizeof(u32)));
    });

    m_native = compiler.compile(std::move(m_execution_unit.copy()));
    m_native->prepare(true);
#endif
  }

  catch (std::exception &e) {
    // printf("compilation failure: %s\n", e.what());
    m_execution_unit.debug_print();
    return false;
  }

  // fmt::print("Basic block compiled, address 0x{:08x} n_instructions {}\n",
  //            m_instructions[0].address,
  //  instructions().size());

  // printf("---\n");
  // m_bytecode.debug_print();

  // printf("============== %lu bytecode / %lu native @ %p ==============\n",
  //       m_bytecode.bytes,
  //       m_native ? m_native->size() : 0lu,
  //       m_native ? m_native->data() : nullptr);
  // printf("\n");

  return true;
}

u64
R3000::BasicBlock::execute(R3000 *cpu, u64 cycle_limit)
{
  // If not compiled yet, force compilation
  if (!is_compiled()) {
    // TODO: Will need some work once jit compilation is on another thread
    cpu->m_jit_cache.queue_compile_unit(this);
  }

  if (m_native) {
    return m_native->execute(cpu, (void *)(cpu->m_mem->root()), cpu->m_regs);
  } else {
    return m_bytecode->execute(cpu, (void *)(cpu->m_mem->root()), cpu->m_regs);
  }
}

} // namespace guest::r3000
