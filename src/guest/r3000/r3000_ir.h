#pragma once

#include <vector>

#include "fox/ir_assembler.h"

namespace guest::r3000 {

class Coprocessor;

/*!
 * @class guest::r3000:Assembler
 * @brief Implementation of an IR assembler for the R3000 CPU.
 */
class Assembler final : public fox::ir::Assembler {
private:
  Coprocessor *m_cop_handlers[4] = {};

public:
  Assembler() {}

  void set_coprocessor_assembler(u32 cop_num, Coprocessor *);

  /*!
   * @brief Generate an IR program to execute a series of CPU instructions.
   */
  fox::ir::ExecutionUnit &&assemble(R3000 *r3000, u32 start_address, u32 limit);

private:
  void throw_if_coprocessor_not_present(unsigned z);

  void decode_instruction(Instruction ins);

  void op_cop_ins(Instruction ins);

  /*
   * Opcode implementations
   */

  void op_add(Instruction ins);
  void op_addi(Instruction ins);
  void op_addiu(Instruction ins);
  void op_addu(Instruction ins);
  void op_and(Instruction ins);
  void op_andi(Instruction ins);
  void op_beq(Instruction ins);
  void op_bgtz(Instruction ins);
  void op_blez(Instruction ins);
  void op_bne(Instruction ins);
  void op_break(Instruction ins);
  void op_bxx(Instruction ins);

  void op_cfc(Instruction ins);
  void op_cop(Instruction ins);
  void op_ctc(Instruction ins);

  void op_div(Instruction ins);
  void op_divu(Instruction ins);
  void op_j(Instruction ins);
  void op_jal(Instruction ins);
  void op_jalr(Instruction ins);
  void op_jr(Instruction ins);
  void op_lb(Instruction ins);
  void op_lbu(Instruction ins);
  void op_lh(Instruction ins);
  void op_lhu(Instruction ins);
  void op_lui(Instruction ins);
  void op_lw(Instruction ins);
  void op_lwc0(Instruction ins);
  void op_lwc1(Instruction ins);
  void op_lwc2(Instruction ins);
  void op_lwc3(Instruction ins);
  void op_lwl(Instruction ins);
  void op_lwr(Instruction ins);

  void op_mfc(Instruction ins);

  void op_mfhi(Instruction ins);
  void op_mflo(Instruction ins);

  void op_mtc(Instruction ins);

  void op_mthi(Instruction ins);
  void op_mtlo(Instruction ins);
  void op_mult(Instruction ins);
  void op_multu(Instruction ins);
  void op_nor(Instruction ins);
  void op_or(Instruction ins);
  void op_ori(Instruction ins);
  void op_sb(Instruction ins);
  void op_sh(Instruction ins);
  void op_sll(Instruction ins);
  void op_sllv(Instruction ins);
  void op_slt(Instruction ins);
  void op_slti(Instruction ins);
  void op_sltiu(Instruction ins);
  void op_sltu(Instruction ins);
  void op_sra(Instruction ins);
  void op_srav(Instruction ins);
  void op_srl(Instruction ins);
  void op_srlv(Instruction ins);
  void op_sub(Instruction ins);
  void op_subu(Instruction ins);
  void op_subiu(Instruction ins);
  void op_sw(Instruction ins);
  void op_swc0(Instruction ins);
  void op_swc1(Instruction ins);
  void op_swc2(Instruction ins);
  void op_swc3(Instruction ins);
  void op_swl(Instruction ins);
  void op_swr(Instruction ins);
  void op_syscall(Instruction ins);
  void op_xor(Instruction ins);
  void op_xori(Instruction ins);

  // Fallback for illegal instructions
  void op_illegal(Instruction ins);

private: /* Internal data. */
  /*!
   * @brief Translation state of CPU registers used for sources / destinations
   *        in instructions.
   */
  struct {
    fox::ir::Operand value;
    bool valid = false;
    bool dirty = false;
  } m_registers[Registers::NUM_REGS];

  fox::ir::Operand m_Rs;
  fox::ir::Operand m_Rt;

  /*!
   * @brief True if the instruction just decoded was a branch instruction that
   *        has updated PC.
   *
   * Note: Even if the branch was not taken, the branch instruction logic will
   *       unconditionally write to PC with the address of the next instruction.
   */
  bool m_branch_executed = false;

  /*!
   * @brief True if the branch just executed (i.e. m_executed_branch == true)
   *        has a delay slot.
   */
  bool m_branch_delayed = false;

  /*!
   * @brief The value that should be written to a register after a one cycle
   *        delay. If not valid(), no delayed write is pending.
   */
  fox::ir::Operand m_writeback_value;

  /*!
   * @brief The index of the register to write m_writeback_value to. Only
   *        valid if m_writeback_value is set.
   */
  u16 m_writeback_index;

  /*!
   * @brief PC for instruction currently being decoded.
   */
  u32 m_pc;

private: /* Internal helper and wrapper routines. */
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
   * @brief Write an ir::Operand to register with the given index.
   */
  void write_reg(u16 index, fox::ir::Operand value);

  /*!
   * @brief Write an ir::Operand to register with the given index with a delay
   *        of one cycle.
   *
   * The write will take effect after the next instruction reads its source
   * registers, but before it writes its own results.
   */
  void write_reg_delayed(u16 reg_index, fox::ir::Operand value);

  /*!
   * @brief Read a Integer32 value of the register with the given index.
   *
   * If the index is R15 (e.g. PC), then 8 is added to the returned result.
   * This is to mimic the software-observed value of the PC register which is
   * always 2 instructions ahead of the currently executing instruction.
   */
  fox::ir::Operand read_reg(u16 index);

  /*!
   * @brief Implement an (optionally conditional) jump to a new PC that has
   *        a delay slot.
   *
   * The PC update will take place after the delay slot (instruction that
   * immediately follows the one currently being translated) is executed.
   */
  void jmp_delay(fox::ir::Operand new_pc,
                 fox::ir::Operand condition = fox::ir::Operand::constant<bool>(true));

  /*!
   * @brief Implement an (optionally conditional) jump to a new PC that has
   *        no delay slot.
   *
   * The PC update will take place immediately after the instruction currently
   * being translated finishes execution.
   */
  void jmp_nodelay(fox::ir::Operand new_pc,
                   fox::ir::Operand condition = fox::ir::Operand::constant<bool>(true));

  void add_with_overflow(fox::ir::Operand a,
                         fox::ir::Operand b,
                         fox::ir::Operand *out_sum,
                         fox::ir::Operand *out_did_overflow);

  void exception(Exceptions::Exception cause);

  void exception_on_overflow(fox::ir::Operand condition);

  template<u32 bytes, Exceptions::Exception exception_type>
  void exception_on_unaligned_access(const fox::ir::Operand address);
};

} // namespace guest::cpu
