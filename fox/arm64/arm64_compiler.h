#pragma once

#include <map>
#include <memory>
#include <functional>
#include <cassert>
#include <stdexcept>

#include "fox/jit/register_allocator.h"
#include "fox/guest.h"
#include "fox/ir/execution_unit.h"
#include "fox/arm64/arm64_routine.h"
#include "fox/arm64/arm64_assembler.h"

namespace fox {
namespace codegen {
namespace arm64 {

/*!
 * @class codegen::arm64::Compiler
 * @brief Implementation of an IR-based arm64 compiler.
 */
class Compiler {
public:
  typedef std::function<unsigned(unsigned)> register_address_cb_t;

  // General process looks like
  // IR -> RTL -> Register Allocation -> Binary Synthesis
  std::unique_ptr<codegen::arm64::Routine> compile(ir::ExecutionUnit &&source);

private:
  //! Source IR for the routine being compiled.
  ir::ExecutionUnit m_source;

  //! Mapping from IR SSA registers to RTL registers.
  std::vector<jit::RtlRegister> m_ir_to_rtl;

  //! Whether or not this routine makes any memory accesses.
  bool m_uses_memory;

  //! The RTL opcodes synthesized by the initial IR scan, used for register assignments.
  jit::RtlProgram m_rtl;

  //! Storage for the executable routine produced by the compiler, until it is returned to
  //! the caller.
  std::unique_ptr<codegen::arm64::Routine> m_routine;

  /*!
   * @brief If set to true during compilation, the disassembled routine will
   *        be dumped to stdout.
   */
  bool m_debug = false;

  /*!
   * @brief Convert the incoming IR to RTL that can be used for register
   *        allocation and synthesis.
   */
  void generate_rtl();

  /*!
   * @brief Perform register allocation on the RTL.
   */
  void assign_registers();

  /*!
   * @brief Emit x86 instructions from the processed RTL.
   */
  void assemble();

  jit::RtlRegister make_rtl_ssa(const ir::Operand operand);

  u16 m_labels = 0;
  u16 allocate_label()
  {
    return m_labels++;
  }

  /*!
   * @brief Either return the existing RTL SSA assignment for the operand or
   *        generate RTL instructions to load a constant and return its RTL
   *        SSA assignment.
   *
   * If operand is not a constant, it must already be in the IR->RTL mapping.
   */
  jit::RtlRegister get_rtl_ssa(ir::Operand operand);

  register_address_cb_t m_register_address_cb;

  bool m_use_fastmem = true;

public:
  // XXX : Construct this object with the register_address_cb_t

  void set_register_address_cb(register_address_cb_t emitter)
  {
    m_register_address_cb = emitter;
  }

  void set_use_fastmem(bool use_fastmem)
  {
    m_use_fastmem = use_fastmem;
  }
};

}
}
}
