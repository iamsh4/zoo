#pragma once

#include "fox/ir_assembler.h"
#include "shared/types.h"
#include "guest/arm7di/arm7di_shared.h"

namespace guest::arm7di {

class Arm7DIAssembler : public fox::ir::Assembler {
public:
  // XXX : This will need to be changed to support multiple opcodes, stop conditions, etc.
  fox::ir::ExecutionUnit &&assemble()
  {
    return export_unit();
  }

  /**! */
  void generate_ir(Arm7DIInstructionInfo &);

  /**! */
  void disassemble(char *buffer, size_t buffer_size);

private:
  /* Implementations of each opcode style - TODO cleanup */

  /*! Data passed to ir-generation/disassembly/etc instructions */
  struct Context {
    Arm7DIInstructionInfo info;
    char *disas_buffer = nullptr;
    size_t disas_size  = 0;
  };

  void opcode_data_processing(Context &context);
  void opcode_multiply(Context &context);
  void opcode_single_data_swap(Context &context);
  void opcode_single_data_transfer(Context &context);
  void opcode_undefined(Context &context);
  void opcode_block_data_transfer(Context &context);
  void opcode_branch(Context &context);
  void opcode_software_interrupt(Context &context);

  /* Types 8, 9, 10 are for coprocessor, do we need them? */

  /*! Write an ir::Operand to register with the given index */
  void write_reg(u16 reg_index, fox::ir::Operand value)
  {
    writegr(const_u16(reg_index), value);
  }

  /*! Read a Integer32 value of the register with the given index. If the index is R15
   * (e.g. PC), then 8 is added to the returned result. This is to mimic the
   * software-observed value of the PC register which is always 2 instructions ahead of
   * the currently executing instruction. */
  fox::ir::Operand read_reg(u16 reg_index)
  {
    const auto reg_value = readgr(fox::ir::Type::Integer32, const_u16(reg_index));

    if (reg_index == Arm7DIRegisterIndex_PC) {
      return add(reg_value, const_u32(8));
    }
    return reg_value;
  }

  /*! Generate an Integer64 ir::Operand with the given value. */
  fox::ir::Operand const_u64(u32 value)
  {
    return fox::ir::Operand::constant<u64>(value);
  }

  /*! Generate an Integer32 ir::Operand with the given value. */
  fox::ir::Operand const_u32(u32 value)
  {
    return fox::ir::Operand::constant<u32>(value);
  }

  /*! Generate an Integer16 ir::Operand with the given value. */
  fox::ir::Operand const_u16(u16 value)
  {
    return fox::ir::Operand::constant<u16>(value);
  }

  /*! Generate an Bool ir::Operand with the given value. */
  fox::ir::Operand const_bool(bool value)
  {
    return fox::ir::Operand::constant<bool>(value);
  }

  /*! Return a Bool ir::Operand which is true if any bits are set in input, false
   * otherwise. */
  fox::ir::Operand any_bits_set(fox::ir::Operand input)
  {
    return test(input, input);
  }

  /*! Return an Integer32 ir::Operand which has the LSB set to the value of bit_index
   * within the reg_index'th register. */
  fox::ir::Operand read_reg_bit(u32 reg_index, u32 bit_index)
  {
    return _and(shiftr(read_reg(reg_index), const_u32(bit_index)), const_u32(1));
  }

  /*! Reinterpret a Integer32 ir::Operand as a signed 32-bit integer, negate the signed
   * integer. */
  fox::ir::Operand neg32(fox::ir::Operand input)
  {
    assert(input.type() == fox::ir::Type::Integer32);
    return sub(const_u32(0), input);
  }

  /*! Logic for computing the operand2 value for Data Processing instructions. */
  void opcode1_decode_op2_reg(OpcodeDataProcessing op,
                              fox::ir::Operand &output,
                              fox::ir::Operand &carry_out);

  void shift_constant(fox::ir::Operand input,
                      const u32 shift_amount,
                      const u32 shift_type,
                      fox::ir::Operand &output,
                      fox::ir::Operand &carry_out);

  void shift_register(fox::ir::Operand input,
                      const u32 reg_index,
                      const u32 shift_type,
                      fox::ir::Operand &output,
                      fox::ir::Operand &carry_out);

  void shift_logic(fox::ir::Operand Rm,
                   fox::ir::Operand shift_amount,
                   u8 shift_type,
                   fox::ir::Operand &output,
                   fox::ir::Operand &carry_out);

  fox::ir::Operand ldr_byte(fox::ir::Operand address);
  fox::ir::Operand ldr_word(fox::ir::Operand address);
  void str_byte(fox::ir::Operand address, fox::ir::Operand value);
  void str_word(fox::ir::Operand address, fox::ir::Operand value);

  void handle_msr_write(bool write_which, fox::ir::Operand value);

  void restore_saved_mode();

  fox::ir::Operand check_condition_code(fox::ir::Operand CPSC, u32 cond);

  template<typename T>
  fox::ir::Operand nth_bit(const fox::ir::Operand &input, u32 n)
  {
    return _and(shiftr(input, fox::ir::Operand::constant<T>(n)),
                fox::ir::Operand::constant<T>(1));
  }
};

}
