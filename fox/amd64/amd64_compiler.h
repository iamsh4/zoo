// vim: expandtab:ts=2:sw=2

#pragma once

#include <map>
#include <memory>
#include <functional>
#include <cassert>

#include "fox/jit/register_allocator.h"
#include "fox/ir/execution_unit.h"
#include "fox/codegen/routine.h"
#include "fox/amd64/amd64_assembler.h"
#include "fox/amd64/amd64_routine.h"

namespace fox {
namespace codegen {
namespace amd64 {

/*!
 * @class fox::codegen::amd64::Compiler
 * @brief Implementation of a IR-based amd64 compiler.
 *
 * The general approach is to try and follow the IR 1:1 or 1:N, with each IR
 * instruction generating one or more x86 instructions. Immediate constants will
 * be stored as instruction immediates. Registers are assigned as follows:
 *
 *     RDI:
 *         Guest* pointer of the emulated CPU.
 *     RBX:
 *         Base address of the Guest register structure.
 *     R12:
 *         Base address of the Guest memory maps. If the generated routine has
 *         no memory access, it is available for general allocation.
 *     RBP:
 *         Pointer to the start of the memory spill region. Each spill entry
 *         is a full 8 bytes (regardless of actual variable size).
 *     RSP:
 *         Normal stack pointer.
 *     R8:
 *         Scratch register. Used to temporarily store values from spill memory
 *         used with instructions that don't have memory operand support.
 *
 * All other registers are available for general purpose allocation.
 */
class Compiler {
public:
  /* Registers that have a fixed meaning in all compiled blocks. */
  /*!
   * @brief Register that will always store the Guest instance pointer. For now,
   *        this must be the same register as the first argument in the calling
   *        ABI.
   */
  static constexpr GeneralRegister gpr_guest = RDI;

  /*!
   * @brief Register that will store the base address of the Guest's register
   *        structure.
   */
  static constexpr GeneralRegister gpr_guest_registers = RBX;

  /*!
   * @brief Register that will store the base address of the Guest's memory
   *        map.
   */
  static constexpr GeneralRegister gpr_guest_memory = R12;

  /*!
   * @brief Register that can be used as temporary storage for operations. The
   *        value is not preserved across RTL entries.
   */
  static constexpr GeneralRegister gpr_scratch = R8;

  /*!
   * @brief Registers that can be used as temporary storage for SSE operations.
   *        The values are not preserved across RTL entries.
   */
  static constexpr VectorRegister vec_scratch = XMM8;

public:
  /*!
   * @brief Create an executable amd64 routine from an IR translation. The
   *        incoming source will be modified by the optimization pass, so
   *        must be given entirely to the compiler.
   */
  std::unique_ptr<Routine> compile(ir::ExecutionUnit &&source);

  /*!
   * @brief Provides the callback function that will be used to map guest
   *        register indexes to memory addresses that can be read / written to.
   *
   * This method must be registered before compilation starts. The addresses
   * can be in base plus offset or SIB form. The callback signature should be:
   *
   * RegMemAny cb(unsigned register_index);
   *
   * The emitter must only modify the result register and optionally the scratch
   * register.
   */
  typedef std::function<RegMemAny(unsigned)> register_address_cb_t;
  void set_register_address_cb(register_address_cb_t emitter)
  {
    m_register_address_cb = emitter;
  }

  /*!
   * @brief Provide a function that will emit specialized RTL to load values
   *        from Guest memory.
   *
   * If the method returns false, the virtual interface will be used during
   * runtime. The emitter signature should match:
   *
   * bool emitter(Assembler* target,
   *              RegisterSize read_size,
   *              GeneralRegister address_register,
   *              GeneralRegister result_register);
   */
  typedef std::function<void(Assembler *, unsigned, GeneralRegister, GeneralRegister)>
    load_emitter_t;
  void set_memory_load_emitter(load_emitter_t emitter)
  {
    m_load_emitter = emitter;
  }

private:
  typedef unsigned LabelId;

  /*!
   * @brief The input IR code block that will be compiled, possibly with some
   *        modifications from optimization passes.
   */
  ir::ExecutionUnit m_source;

  /*!
   * @brief Callback method to a guest-specific interface for calculating memory
   *        addresses of guest registers.
   */
  register_address_cb_t m_register_address_cb;

  /*!
   * @brief Callback method to a guest-specific interface for emitting optimized
   *        memory load operations.
   */
  load_emitter_t m_load_emitter;

  /*!
   * @brief Mapping from IR SSA registers to RTL registers.
   */
  std::vector<jit::RtlRegister> m_ir_to_rtl;

  /*!
   * @brief Map from instruction labels to their offset in the instruction
   *        stream.
   *
   * Label IDs are just integers incremented starting from 0. Labels are created
   * and referenced during the RTL phase, without an offset value. In the emit
   * phase jumps to labels are encoded as 32-bit displacements and fixed up at
   * the end once all label locations have been determined.
   *
   * An offset of UINT32_MAX means the label hasn't been bound by the emit
   * phase yet.
   */
  std::vector<unsigned> m_labels;

  /*!
   * @brief Set to true if there is at least one memory load / store in the code
   *        block. If there's no memory access, gpr_guest_memory is available
   *        for general allocation.
   */
  bool m_uses_memory;

  /*!
   * @brief The RTL opcodes synthesized by the initial IR scan, used for
   *        register assignments.
   */
  jit::RtlProgram m_rtl;

  /*!
   * @brief Assembler instance used to turn synthesized RTL code into native
   *        machine instructions.
   */
  Assembler m_asm;

  /*!
   * @brief Storage for the executable routine produced by the compiler, until
   *        it is returned to the caller.
   */
  std::unique_ptr<Routine> m_routine;

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

  /*!
   * @brief Either return the existing RTL SSA assignment for the operand or
   *        generate RTL instructions to load a constant and return its RTL
   *        SSA assignment.
   *
   * If operand is not a constant, it must already be in the IR->RTL mapping.
   */
  jit::RtlRegister get_rtl_ssa(ir::Operand operand);

  /*!
   * @brief Allocate a new RTL register to represent an IR operand. The operand
   *        must be a register (not a constant) and the mapping will be stored
   *        so it can be returned later by calls to get_rtl_ssa.
   */
  jit::RtlRegister make_rtl_ssa(ir::Operand operand);

  /*!
   * @brief ...
   */
  LabelId allocate_label(const char *name);
};

}
}
}
