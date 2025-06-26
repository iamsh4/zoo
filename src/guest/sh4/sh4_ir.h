// vim: expandtab:ts=2:sw=2

#pragma once

#include "fox/ir_assembler.h"

namespace cpu {

struct InstructionDetail {
  u32 address;
  u16 raw;
  u16 id;
};

/*!
 * @class SH4Assembler
 * @brief Specialized implementatino of an IR Assembler that handles linking
 *        the IR execution to the context of an emulated SH4 CPU's state.
 */
class SH4Assembler final : public fox::ir::Assembler {
public:
  /* Constants used by IR for accessing register bitfields. */
  /* Status register bit positions. */
  static constexpr unsigned SR_Bit_T = 0u;
  static constexpr unsigned SR_Bit_S = 1u;
  static constexpr unsigned SR_Bit_Q = 8u;
  static constexpr unsigned SR_Bit_M = 9u;
  static constexpr unsigned SR_Bit_RB = 29u;

  /* FPU status / control register bit positions. */
  static constexpr unsigned FPSCR_Bit_FR = 21u;
  static constexpr unsigned FPSCR_Bit_SZ = 20u;
  static constexpr unsigned FPSCR_Bit_PR = 19u;

  /*!
   * @brief Map of register names / values to the internal constraint table.
   *
   * General purpose registers are mapped to IR register indexes 0-24.
   *     0-7:   Current bank R0-R7
   *     8-15:  R8-R15
   *     16-23: Alternate bank R0-R7
   *
   * Banked floating point registers are mapped with the active bank followed
   * by the alternate bank.
   *
   * All integer / status register types are Integer32. The order in the enum
   * needs to match the order of the register structure in SH4, since the values
   * are used to index its memory directly.
   */
  enum {
    /* Standard / Integer CPU registers */
    R0,
    R1,
    R2,
    R3,
    R4,
    R5,
    R6,
    R7,
    R8,
    R9,
    R10,
    R11,
    R12,
    R13,
    R14,
    R15,
    R0alt,
    R1alt,
    R2alt,
    R3alt,
    R4alt,
    R5alt,
    R6alt,
    R7alt,
    SR,
    SSR,
    PC,
    SPC,
    GBR,
    VBR,
    MACL,
    MACH,
    PR,
    SPR,
    SGR,
    DBR,

    /* FPU registers */
    SP0,
    SP1,
    SP2,
    SP3,
    SP4,
    SP5,
    SP6,
    SP7,
    SP8,
    SP9,
    SP10,
    SP11,
    SP12,
    SP13,
    SP14,
    SP15,
    SP0alt,
    SP1alt,
    SP2alt,
    SP3alt,
    SP4alt,
    SP5alt,
    SP6alt,
    SP7alt,
    SP8alt,
    SP9alt,
    SP10alt,
    SP11alt,
    SP12alt,
    SP13alt,
    SP14alt,
    SP15alt,
    FPSCR,
    FPUL,

    /* Pseudo register (don't exist on real hardware) */
   CycleCount,

    _RegisterCount
  };

  /* Constructor / destructor */
  SH4Assembler();
  ~SH4Assembler();

  /*!
   * @brief Construction an IR ExecutionUnit from the sequence of decoded SH4
   *        instructions.
   */
  fox::ir::ExecutionUnit &&assemble(uint32_t cpu_flags,
                                    const InstructionDetail *instructions,
                                    size_t count);

  /*!
   * @brief Translate and assemble an EBB (Extended Basic Block) from SH4
   *        to IR.
   */

  /*!
   * @brief Return the Operand that currently maps to the indicated Rn register.
   */
  fox::ir::Operand read_GPR(const unsigned id)
  {
    assert(id < 16);
    return read_i32(R0 + id);
  }

  /*!
   * @brief Return the Operand that currently maps to the indicated Rn from the
   *        alternate register bank.
   */
  fox::ir::Operand read_GPR_alt(const unsigned id)
  {
    /* Only R0-R7 are banked. */
    assert(id < 8);
    return read_i32(R0alt + id);
  }

  /*!
   * @brief Return the Operand that currently maps to the indicated SPn register.
   */
  fox::ir::Operand read_SP(const unsigned id)
  {
    assert(id < 16);
    return read_f32(SP0 + id);
  }

  /*!
   * @brief Return the Operand that currently maps to the indicated SPn from the
   *        alternate register bank.
   */
  fox::ir::Operand read_SP_alt(const unsigned id)
  {
    assert(id < 16);
    return read_f32(SP0alt + id);
  }

  /*!
   * @brief Return the Operand that currently maps to the indicated DPn register.
   */
  fox::ir::Operand read_DP(const unsigned id)
  {
    assert(id < 8);
    return read_f64(SP0 + id * 2);
  }

  /*!
   * @brief Return the Operand that currently maps to the indicated DPn from the
   *        alternate register bank.
   */
  fox::ir::Operand read_DP_alt(const unsigned id)
  {
    assert(id < 8);
    return read_f64(SP0alt + id * 2);
  }

  /*!
   * @brief Update the value of the indicated Rn register with a new IR result
   *        register.
   */
  void write_GPR(const unsigned id, const fox::ir::Operand value)
  {
    assert(id < 16);
    write_i32(R0 + id, value);
  }

  /*!
   * @brief Update the value of the indicated Rn register in the alternate
   *        register bank with a new IR result register.
   */
  void write_GPR_alt(const unsigned id, const fox::ir::Operand value)
  {
    /* Only R0-R7 are banked. */
    assert(id < 8);
    write_i32(R0alt + id, value);
  }

  /*!
   * @brief Update the value of the indicated SPn register with a new IR result
   *        register.
   */
  void write_SP(const unsigned id, const fox::ir::Operand value)
  {
    assert(id < 16);
    write_f32(SP0 + id, value);
  }

  /*!
   * @brief Update the value of the indicated SPn register in the alternate
   *        register bank with a new IR result register.
   */
  void write_SP_alt(const unsigned id, const fox::ir::Operand value)
  {
    assert(id < 16);
    write_f32(SP0alt + id, value);
  }

  /*!
   * @brief Update the value of the indicated DPn register with a new IR result
   *        register.
   */
  void write_DP(const unsigned id, const fox::ir::Operand value)
  {
    assert(id < 8);
    write_f64(SP0 + id * 2, value);
  }

  /*!
   * @brief Update the value of the indicated DPn register in the alternate
   *        register bank with a new IR result register.
   */
  void write_DP_alt(const unsigned id, const fox::ir::Operand value)
  {
    assert(id < 8);
    write_f64(SP0alt + id * 2, value);
  }

  /* PC has special handling because it needs to be implicitly updated after
   * each instruction. */

  /*!
   * @brief Read value of PC referring to the current instruction. This does
   *        not account for pipelining.
   */
  fox::ir::Operand read_PC();

  /*!
   * @brief Write a new value to PC causing a control flow change. The effect
   *        is immediate, so this must be called after delay slot translation.
   */
  void write_PC(const fox::ir::Operand value);

  /*
   * Specialized register read wrappers.
   */

  fox::ir::Operand read_SR()
  {
    return read_i32(SR);
  }

  fox::ir::Operand read_SSR()
  {
    return read_i32(SSR);
  }

  fox::ir::Operand read_SPC()
  {
    return read_i32(SPC);
  }

  fox::ir::Operand read_GBR()
  {
    return read_i32(GBR);
  }

  fox::ir::Operand read_VBR()
  {
    return read_i32(VBR);
  }

  fox::ir::Operand read_MACH()
  {
    return read_i32(MACH);
  }

  fox::ir::Operand read_MACL()
  {
    return read_i32(MACL);
  }

  fox::ir::Operand read_PR()
  {
    return read_i32(PR);
  }

  fox::ir::Operand read_SPR()
  {
    return read_i32(SPR);
  }

  fox::ir::Operand read_SGR()
  {
    return read_i32(SGR);
  }

  fox::ir::Operand read_DBR()
  {
    return read_i32(DBR);
  }

  fox::ir::Operand read_FPSCR()
  {
    return read_i32(FPSCR);
  }

  fox::ir::Operand read_FPUL()
  {
    return read_i32(FPUL);
  }

  /*
   * Specialized register write wrappers.
   */

  void write_SR(const fox::ir::Operand value)
  {
    write_i32(SR, value);
  }

  void write_SSR(const fox::ir::Operand value)
  {
    write_i32(SSR, value);
  }

  void write_SPC(const fox::ir::Operand value)
  {
    write_i32(SPC, value);
  }

  void write_GBR(const fox::ir::Operand value)
  {
    write_i32(GBR, value);
  }

  void write_VBR(const fox::ir::Operand value)
  {
    write_i32(VBR, value);
  }

  void write_MACH(const fox::ir::Operand value)
  {
    write_i32(MACH, value);
  }

  void write_MACL(const fox::ir::Operand value)
  {
    write_i32(MACL, value);
  }

  void write_PR(const fox::ir::Operand value)
  {
    write_i32(PR, value);
  }

  void write_SPR(const fox::ir::Operand value)
  {
    write_i32(SPR, value);
  }

  void write_SGR(const fox::ir::Operand value)
  {
    write_i32(SGR, value);
  }

  void write_DBR(const fox::ir::Operand value)
  {
    write_i32(DBR, value);
  }

  void write_FPSCR(const fox::ir::Operand value)
  {
    write_i32(FPSCR, value);
  }

  void write_FPUL(const fox::ir::Operand value)
  {
    write_i32(FPUL, value);
  }

  /*!
   * @brief Override for the base Assembler instance's exit() opcode emitter,
   *        which automatically flushes all dirty register state before the
   *        opcode is added and returns the correct cycle count.
   */
  void exit(fox::ir::Operand decision);

  using Assembler::exit;

public: /* XXX Needs to be accessed by opcode table. */
  /*!
   * @brief Implementation of SH4 -> IR translation for each opcode.
   *
   * Templated on the generated opcode decoding structure. Returns false if the
   * instruction does not have an IR translation.
   *
   * XXX Integrate with basic block logic, make non-public.
   */
  template<typename T>
  bool translate_instruction(u16 opcode, u32 PC, u32 flags);

protected:
  /* Methods exposed only to instruction translation routines. */

  /*!
   * @brief Flush all dirty register state back to the guest CPU. Must be
   *        called before returning from the IR to the emulation environment.
   *
   * Note: Flush happens automatically on exit() calls.
   */
  void flush();

  /*!
   * @brief Flush a single register's dirty state back to the guest CPU, if
   *        it currently contains valid and dirty data.
   */
  void flush(unsigned index);

  /*!
   * @brief Invalidate all IR register states. The next access for any guest
   *        register will force a 'readgr' instruction.
   */
  void invalidate();

  /*!
   * @brief Invalidate a single register's IR state. The next access for the
   *        guest register will force a 'readgr' instruction if not written
   *        first.
   */
  void invalidate(unsigned index, bool allow_dirty = false);

  /*!
   * @brief Translate the delay slot of the current instruction. Called from
   *        translate_instruction<> implementations of branches with delay
   *        slots.
   */
  void translate_delay_slot();

  /*!
   * @brief Wrapper for the internal interpret_upcall() method that needs
   *        details of the instruction that will be interpreted. Can be used by
   *        SH4 IR generation routines that don't want to implement the
   *        functionality directly, but do want to control register flushing and
   *        invalidation.
   */
  void interpret_upcall();

  /*!
   * @brief Handle a potential general register bank swap (R0 - R8). The passed
   *        operand indicates whether the bank swap should actually happen.
   */
  void gpr_maybe_swap(fox::ir::Operand do_swap);

  /*!
   * @brief Handle a potential FPU bank swap. FPU registers already loaded into
   *        IR are updated to either use new references or be reloaded as
   *        necessary.
   */
  void fpu_maybe_swap(fox::ir::Operand do_swap);

private:
  /*!
   * @brief Map from SH4 CPU registers to IR registers during assembly.
   *
   * valid indicates value is an IR register with up-to-date contents, and dirty
   * indicates the IR register has modifications not yet flushed to the SH4
   * state with writegr instructions.
   */
  struct {
    fox::ir::Operand value;
    bool valid = false;
    bool dirty = false;
  } m_registers[_RegisterCount];

  /*!
   * @brief The sequence of SH4 instructions currently being assembled. Set to
   *        nullptr outside calls to assemble().
   */
  const InstructionDetail *m_source = nullptr;

  /*!
   * @brief The set of CPU flags affecting IR translation that should be used.
   *        This is passed to the assemble() method.
   */
  u32 m_cpu_flags = 0;

  /*!
   * @brief The position in m_source of the SH4 instruction currently being
   *        assembled.
   */
  size_t m_source_index = 0;

  /*!
   * @brief If the current m_source_index points to a branch with a delay slot,
   *        indicates whether that delay slot has been translated yet.
   */
  bool m_delay_slot_processed = false;

  /*!
   * @brief True if a delay slot is currently being converted to IR. Used to
   *        determine if the current instruction is at m_source_index or
   *        m_source_index + 1.
   */
  bool m_in_delay_slot = false;

  /*!
   * @brief The number of SH4 cycles that instructions translated since the
   *        last cycle flush should have consumed.
   */
  uint32_t m_cpu_cycles = 0;

  /*!
   * @brief Internal implementation of register read logic. Inserts load of
   *        current value if not already available.
   */
  fox::ir::Operand read_i32(const unsigned index)
  {
    if (m_registers[index].valid) {
      return m_registers[index].value;
    }

    const fox::ir::Operand ssr_index = fox::ir::Operand::constant<u16>(index);
    m_registers[index].value = readgr(fox::ir::Type::Integer32, ssr_index);
    m_registers[index].valid = true;
    return m_registers[index].value;
  }

  /*!
   * @brief Internal implementation of register write logic. Updates dirty /
   *        validity state of SSR <-> guest register map.
   */
  void write_i32(const unsigned index, const fox::ir::Operand value)
  {
    assert(value.type() == fox::ir::Type::Integer32);

    m_registers[index].value = value;
    m_registers[index].valid = true;
    m_registers[index].dirty = true;
  }

  /*!
   * @brief Internal implementation of register read logic. Inserts load of
   *        current value if not already available.
   */
  fox::ir::Operand read_f32(const unsigned index)
  {
    /* If this register was previously combined with its pair register to form
     * an f64, flush and invalidate the f64 register. */
    if (m_registers[index & ~1].valid) {
      if (m_registers[index & ~1].value.type() == fox::ir::Type::Float64) {
        flush(index & ~1);
        invalidate(index & ~1);
      }
    }

    if (m_registers[index].valid) {
      assert(m_registers[index].value.type() == fox::ir::Type::Float32);
      return m_registers[index].value;
    }

    const fox::ir::Operand ssr_index = fox::ir::Operand::constant<u16>(index);
    m_registers[index].value = readgr(fox::ir::Type::Float32, ssr_index);
    m_registers[index].valid = true;
    return m_registers[index].value;
  }

  /*!
   * @brief Internal implementation of register write logic. Updates dirty /
   *        validity state of SSR <-> guest register map.
   */
  void write_f32(const unsigned index, const fox::ir::Operand value)
  {
    assert(value.type() == fox::ir::Type::Float32);

    /* If this register was previously combined with the prior register to form
     * an f64, flush and invalidate the register pairing. */
    if (m_registers[index & ~1].valid) {
      if (m_registers[index & ~1].value.type() == fox::ir::Type::Float64) {
        flush(index & ~1);
        invalidate(index & ~1);
      }
    }

    m_registers[index].value = value;
    m_registers[index].valid = true;
    m_registers[index].dirty = true;
  }

  /*!
   * @brief Internal implementation of register read logic. Inserts load of
   *        current value if not already available.
   */
  fox::ir::Operand read_f64(const unsigned index)
  {
    if (m_registers[index].valid &&
        m_registers[index].value.type() == fox::ir::Type::Float64) {
      return m_registers[index].value;
    }

    /* If either f32 pair that constitutes this f64 was previously in use, flush
     * and invalidate their existing values. */
    if (m_registers[index].valid) {
      assert(m_registers[index].value.type() == fox::ir::Type::Float32);
      flush(index);
      invalidate(index);
    }

    if (m_registers[index + 1].valid) {
      assert(m_registers[index + 1].value.type() == fox::ir::Type::Float32);
      flush(index + 1);
      invalidate(index + 1);
    }

    const fox::ir::Operand ssr_index = fox::ir::Operand::constant<u16>(index);
    m_registers[index].value = readgr(fox::ir::Type::Float64, ssr_index);
    m_registers[index].valid = true;
    return m_registers[index].value;
  }

  /*!
   * @brief Internal implementation of register write logic. Updates dirty /
   *        validity state of SSR <-> guest register map.
   */
  void write_f64(const unsigned index, const fox::ir::Operand value)
  {
    assert(value.type() == fox::ir::Type::Float64);

    /* If either f32 pair that constitutes this f64 was previously in use,
     * invalidate their existing values. */
    if (m_registers[index].valid) {
      if (m_registers[index].value.type() == fox::ir::Type::Float32) {
        invalidate(index, true);
      }
    }

    if (m_registers[index + 1].valid) {
      assert(m_registers[index + 1].value.type() == fox::ir::Type::Float32);
      invalidate(index + 1, true);
    }

    m_registers[index].value = value;
    m_registers[index].valid = true;
    m_registers[index].dirty = true;
  }

  /*!
   * @brief Generate IR instructions to perform an upcall to the SH4 interpreter.
   *        Used in place of full IR translation when IR translation isn't
   *        available or is disabled.
   *
   * Interprets the provided instruction directly. Does not handle delay slot
   * logic. Returns the call() result.
   */
  fox::ir::Operand interpret_upcall(InstructionDetail instruction,
                                    bool add_cycles = true);
};

}
