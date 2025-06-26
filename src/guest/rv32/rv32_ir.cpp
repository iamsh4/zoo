#include "rv32.h"
#include "rv32_ir.h"
#include "shared/bitmanip.h"

using namespace fox::ir;

namespace guest::rv32 {

fox::ir::Operand
RV32Assembler::read_reg(u16 index)
{
  if (index == 0) {
    return const_u32(0);
  }
  return readgr(Type::Integer32, const_u16(index));
}

fox::ir::ExecutionUnit &&
RV32Assembler::assemble(RV32 *cpu, u32 address, u32 end_address)
{
#if 1
  u32 cycle_count = 0;
  for (u32 i = 0; i < 100 && address < end_address; ++i) {
    const u32 instruction_word = cpu->mem_read<u32>(address);
    const Encoding encoded { .raw = instruction_word, .pc = address };

    bool did_decode = false;
    for (auto &isa : cpu->m_instruction_sets) {
      const Decoding decoding = isa->decode(encoded);
      if (decoding.instruction == Instruction::__NOT_DECODED__) {
        continue;
      }
      did_decode = true;

      const Result asm_result = isa->assemble(this, decoding);
      cycle_count += asm_result.cycle_count;

      // Exit on branches
      const bool conditional_branch =
        decoding.flags & u32(Decoding::Flag::ConditionalJump);
      const bool unconditional_branch =
        decoding.flags & u32(Decoding::Flag::UnconditionalJump);
      if (conditional_branch || unconditional_branch) {
        exit(asm_result.result, fox::ir::Operand::constant<u64>(cycle_count));
      } else {
        // Non-branching instructions increment the PC by 4
        write_reg(Registers::REG_PC, Operand::constant<u32>(address + 4));
      }

      // If this was unconditional, then
      if (unconditional_branch) {
        break;
      }

      address += 4;
    }

    if (!did_decode) {
      throw std::runtime_error("Failed to decode rv32i");
    }
  }

  // Exit due to over the limit
  exit(Operand::constant<bool>(true), Operand::constant<u64>(cycle_count));

// TODO: add assert that unconditional jumps are constant
#else

  /////////////////////////////////

  const u32 instruction_word = cpu->mem_read<u32>(address);
  const Encoding encoded { .raw = instruction_word, .pc = address };

  bool did_decode = false;
  for (auto &isa : cpu->m_instruction_sets) {

    const Decoding decoding = isa->decode(encoded);
    if (decoding.instruction == Instruction::__NOT_DECODED__) {
      continue;
    }

    printf("decoded 0x%08x -> ins %u\n", encoded.pc, decoding.instruction);
    did_decode = true;

    isa->assemble(this, decoding);

    const bool is_branch = decoding.flags & u32(Decoding::Flag::ConditionalJump) ||
                           decoding.flags & u32(Decoding::Flag::UnconditionalJump);
    if (!is_branch) {
      // If the branch is taken, the PC will be updated by the branch instruction.
      // If the branch is not taken, the PC will be updated by the next instruction.
      // In either case, we need to skip the next instruction.
      write_reg(Registers::REG_PC, const_u32(address + 4));
    }

    exit(fox::ir::Operand::constant<bool>(true), fox::ir::Operand::constant<u64>(1));
    break;
  }

  if (!did_decode) {
    throw std::runtime_error("Failed to decode rv32i");
  }
#endif

  return export_unit();
}

void
RV32Assembler::write_reg(u16 index, fox::ir::Operand value)
{
  if (index == 0)
    return;

  assert(value.type() == Type::Integer32);
  writegr(const_u16(index), value);
}

Decoding::Decoding(Encoding encoding, Instruction instruction, EncodingType encoding_type)
  : instruction(instruction),
    encoding(encoding),
    encoding_type(encoding_type)
{
  imm = 0;

  switch (encoding_type) {
    case EncodingType::R:
      rd     = encoding.R.rd;
      funct3 = encoding.R.funct3;
      rs1    = encoding.R.rs1;
      rs2    = encoding.R.rs2;
      funct7 = encoding.R.funct7;
      break;
    case EncodingType::I:
      rd     = encoding.I.rd;
      funct3 = encoding.I.funct3;
      rs1    = encoding.I.rs1;
      imm    = extend_sign<12>(encoding.I.imm_11_0);
      break;
    case EncodingType::S:
      imm = extend_sign<7>(encoding.S.imm_11_5) << 5;
      imm |= encoding.S.imm_4_0;
      funct3 = encoding.S.funct3;
      rs1    = encoding.S.rs1;
      rs2    = encoding.S.rs2;
      break;
    case EncodingType::B:
      imm |= encoding.B.imm_4_1 << 1;
      imm |= encoding.B.imm_10_5 << 5;
      imm |= encoding.B.imm_11 << 11;
      imm |= encoding.B.imm_12 << 12;
      imm    = extend_sign<13>(imm);
      funct3 = encoding.B.funct3;
      rs1    = encoding.B.rs1;
      rs2    = encoding.B.rs2;
      break;
    case EncodingType::U:
      rd  = encoding.U.rd;
      imm = encoding.U.imm_31_12 << 12;
      break;
    case EncodingType::J:
      rd = encoding.J.rd;
      imm |= encoding.J.imm_10_1 << 1;
      imm |= encoding.J.imm_11 << 11;
      imm |= encoding.J.imm_19_12 << 12;
      imm |= encoding.J.imm_20 << 20;
      imm = extend_sign<21>(imm);
      break;
    default:
      break;
  }
}

} // namespace guest::rv32
