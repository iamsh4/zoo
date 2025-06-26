#include "rv32_ir.h"
#include "shared/bitmanip.h"
#include "fmt/core.h"

using namespace fox::ir;

namespace guest::rv32 {

const Operand const_u32_0 = Operand::constant<u32>(0);
const Operand const_u32_1 = Operand::constant<u32>(1);

const Decoding FAILED_TO_DECODE;

Decoding
RV32I::decode(Encoding enc)
{
  const bool bit30 = enc.raw & 0x40000000;
  using D = Decoding;
  using I = Instruction;
  using Flag = Decoding::Flag;

  const u32 func7_func3 = (enc.R.funct7 << 3) | enc.R.funct3;

  switch (enc.R.opcode) {
      // clang-format off
    case 0b0110111: return D(enc, I::LUI,   EncodingType::U);
    case 0b0010111: return D(enc, I::AUIPC, EncodingType::U);
    case 0b1101111: return D(enc, I::JAL,   EncodingType::J).flag(Flag::UnconditionalJump);
    case 0b1100111:
      if (enc.I.funct3 == 0b000) {
        return D(enc, I::JALR, EncodingType::I).flag(Flag::UnconditionalJump);
      }
      return FAILED_TO_DECODE;

    case 0b1100011: // Conditional Branch
      switch (enc.B.funct3) {
        case Branch::BEQ:   return D(enc, I::BEQ,  EncodingType::B).flag(Flag::ConditionalJump);
        case Branch::BNE:   return D(enc, I::BNE,  EncodingType::B).flag(Flag::ConditionalJump);
        case Branch::BLT:   return D(enc, I::BLT,  EncodingType::B).flag(Flag::ConditionalJump);
        case Branch::BGE:   return D(enc, I::BGE,  EncodingType::B).flag(Flag::ConditionalJump);
        case Branch::BLTU:  return D(enc, I::BLTU, EncodingType::B).flag(Flag::ConditionalJump);
        case Branch::BGEU:  return D(enc, I::BGEU, EncodingType::B).flag(Flag::ConditionalJump);
        default:
          return FAILED_TO_DECODE;
      }
      break;

    case 0b0000011: // Load
      switch (enc.I.funct3) {
        case 0b000: return D(enc, I::LB, EncodingType::I); 
        case 0b001: return D(enc, I::LH, EncodingType::I); 
        case 0b010: return D(enc, I::LW, EncodingType::I); 
        case 0b100: return D(enc, I::LBU, EncodingType::I);
        case 0b101: return D(enc, I::LHU, EncodingType::I);
        default:
          return FAILED_TO_DECODE;
      }
      break;

    case 0b0100011: // Stores
      switch (enc.S.funct3) {
        case 0b000: return D(enc, I::SB, EncodingType::S);
        case 0b001: return D(enc, I::SH, EncodingType::S);
        case 0b010: return D(enc, I::SW, EncodingType::S);
        default:
          return FAILED_TO_DECODE;
      }
      break;

    case 0b0010011: // ALU Immediate
      switch (enc.I.funct3) {
        case 0b000: return D(enc, I::ADDI,  EncodingType::I);
        case 0b010: return D(enc, I::SLTI,  EncodingType::I);
        case 0b011: return D(enc, I::SLTIU, EncodingType::I);
        case 0b100: return D(enc, I::XORI,  EncodingType::I);
        case 0b110: return D(enc, I::ORI,   EncodingType::I);
        case 0b111: return D(enc, I::ANDI,  EncodingType::I);
        case 0b001: return D(enc, I::SLLI,  EncodingType::I); // Special encoding
        case 0b101: {
          if (bit30) {
            return D(enc, I::SRAI,  EncodingType::I);
          } else {
            return D(enc, I::SRLI,  EncodingType::I);
          }
        } break;
        default:
          return FAILED_TO_DECODE;
      }
      break;

    case 0b0110011: // Register ALU
      switch(func7_func3) {
        case 0b0000000'000: return D(enc, I::ADD,  EncodingType::R);
        case 0b0100000'000: return D(enc, I::SUB,  EncodingType::R);
        case 0b0000000'001: return D(enc, I::SLL,  EncodingType::R);
        case 0b0000000'010: return D(enc, I::SLT,  EncodingType::R);
        case 0b0000000'011: return D(enc, I::SLTU, EncodingType::R);
        case 0b0000000'100: return D(enc, I::XOR,  EncodingType::R);
        case 0b0000000'101: return D(enc, I::SRL,  EncodingType::R);
        case 0b0100000'101: return D(enc, I::SRA,  EncodingType::R);
        case 0b0000000'110: return D(enc, I::OR,   EncodingType::R);
        case 0b0000000'111: return D(enc, I::AND,  EncodingType::R);
        default:            return FAILED_TO_DECODE;
      }
      break;

    case 0b1110011: {
      if (enc.I.imm_11_0 == 0) {
        return D(enc, I::ECALL, EncodingType::I); 
      } else {
        return D(enc, I::EBREAK, EncodingType::I); 
      }
    } break;
      // clang-format on

    default:
      return FAILED_TO_DECODE;
  }

  return FAILED_TO_DECODE;
}

Result
RV32I::assemble(RV32Assembler *as, Decoding decoding)
{
  switch (decoding.instruction) {
    case Instruction::LUI:
      return LUI(as, decoding);
      break;
    case Instruction::AUIPC:
      return AUIPC(as, decoding);
      break;
    case Instruction::JAL:
      return JAL(as, decoding);
      break;
    case Instruction::JALR:
      return JALR(as, decoding);
      break;
    case Instruction::BEQ:
      return BEQ(as, decoding);
      break;
    case Instruction::BNE:
      return BNE(as, decoding);
      break;
    case Instruction::BLT:
      return BLT(as, decoding);
      break;
    case Instruction::BGE:
      return BGE(as, decoding);
      break;
    case Instruction::BLTU:
      return BLTU(as, decoding);
      break;
    case Instruction::BGEU:
      return BGEU(as, decoding);
      break;
    case Instruction::LB:
      return LB(as, decoding);
      break;
    case Instruction::LH:
      return LH(as, decoding);
      break;
    case Instruction::LW:
      return LW(as, decoding);
      break;
    case Instruction::LBU:
      return LBU(as, decoding);
      break;
    case Instruction::LHU:
      return LHU(as, decoding);
      break;
    case Instruction::SB:
      return SB(as, decoding);
      break;
    case Instruction::SH:
      return SH(as, decoding);
      break;
    case Instruction::SW:
      return SW(as, decoding);
      break;
    case Instruction::ADDI:
      return ADDI(as, decoding);
      break;
    case Instruction::SLTI:
      return SLTI(as, decoding);
      break;
    case Instruction::SLTIU:
      return SLTIU(as, decoding);
      break;
    case Instruction::XORI:
      return XORI(as, decoding);
      break;
    case Instruction::ORI:
      return ORI(as, decoding);
      break;
    case Instruction::ANDI:
      return ANDI(as, decoding);
      break;
    case Instruction::SLLI:
      return SLLI(as, decoding);
      break;
    case Instruction::SRLI:
      return SRLI(as, decoding);
      break;
    case Instruction::SRAI:
      return SRAI(as, decoding);
      break;
    case Instruction::ADD:
      return ADD(as, decoding);
      break;
    case Instruction::SUB:
      return SUB(as, decoding);
      break;
    case Instruction::SLL:
      return SLL(as, decoding);
      break;
    case Instruction::SLT:
      return SLT(as, decoding);
      break;
    case Instruction::SLTU:
      return SLTU(as, decoding);
      break;
    case Instruction::XOR:
      return XOR(as, decoding);
      break;
    case Instruction::SRL:
      return SRL(as, decoding);
      break;
    case Instruction::SRA:
      return SRA(as, decoding);
      break;
    case Instruction::OR:
      return OR(as, decoding);
      break;
    case Instruction::AND:
      return AND(as, decoding);
      break;
    case Instruction::ECALL:
      return ECALL(as, decoding);
      break;
    case Instruction::EBREAK:
      return EBREAK(as, decoding);
      break;
    default:
      throw std::runtime_error("rv32 base assembly failed for decoded instruction'");
      return Result {};
  }

  // TODO : Remove breaks

  return Result {};
}

Result
RV32I::LUI(RV32Assembler *as, Decoding decoding)
{
  Operand imm = as->const_u32(decoding.imm);
  as->write_reg(decoding.rd, imm);
  return Result {};
}

Result
RV32I::AUIPC(RV32Assembler *as, Decoding decoding)
{
  Operand address = as->const_u32(decoding.imm + decoding.encoding.pc);
  as->write_reg(decoding.rd, address);
  return Result {};
}

Result
RV32I::JAL(RV32Assembler *as, Decoding decoding)
{
  // rd ← pc + length(inst)
  as->write_reg(decoding.rd, as->const_u32(decoding.encoding.pc + 4));

  // pc ← pc + offset
  const Operand new_pc = as->const_u32(decoding.encoding.pc + decoding.imm);
  as->write_reg(Registers::REG_PC, new_pc);
  return Result { .result = Operand::constant<bool>(true), .cycle_count = 6 };
}

Result
RV32I::JALR(RV32Assembler *as, Decoding decoding)
{
  Operand rs1 = as->read_reg(decoding.rs1);
  Operand offset = as->const_u32(decoding.imm);
  Operand address = as->add(rs1, offset);
  address = as->_and(address, as->const_u32(0xfffffffe));
  as->write_reg(Registers::REG_PC, address);

  Operand return_address = as->const_u32(decoding.encoding.pc + 4);
  as->write_reg(decoding.rd, return_address);
  return Result { .result = Operand::constant<bool>(true), .cycle_count = 6 };
}

Result
RV32I::BEQ(RV32Assembler *as, Decoding decoding)
{
  const Operand rs1 = as->read_reg(decoding.rs1);
  const Operand rs2 = as->read_reg(decoding.rs2);
  const Operand old_pc = as->const_u32(decoding.encoding.pc);

  Operand branch_offset = as->const_u32(decoding.imm);

  Operand cond = as->cmp_eq(rs1, rs2);
  Operand branch_distance = as->select(cond, as->const_u32(4), branch_offset);
  as->write_reg(Registers::REG_PC, as->add(old_pc, branch_distance));
  return Result { .result = cond, .cycle_count = 3 };
}

Result
RV32I::BNE(RV32Assembler *as, Decoding decoding)
{
  const Operand rs1 = as->read_reg(decoding.rs1);
  const Operand rs2 = as->read_reg(decoding.rs2);
  const Operand old_pc = as->const_u32(decoding.encoding.pc);

  Operand branch_offset = as->const_u32(decoding.imm);

  Operand cond = as->cmp_eq(rs1, rs2);
  Operand branch_distance = as->select(cond, branch_offset, as->const_u32(4));
  as->write_reg(Registers::REG_PC, as->add(old_pc, branch_distance));
  return Result { .result = as->_not(cond), .cycle_count = 3 };
}

Result
RV32I::BLT(RV32Assembler *as, Decoding decoding)
{
  const Operand rs1 = as->read_reg(decoding.rs1);
  const Operand rs2 = as->read_reg(decoding.rs2);
  const Operand old_pc = as->const_u32(decoding.encoding.pc);

  Operand branch_offset = as->const_u32(decoding.imm);

  Operand cond = as->cmp_lt(rs1, rs2);
  Operand branch_distance = as->select(cond, as->const_u32(4), branch_offset);
  as->write_reg(Registers::REG_PC, as->add(old_pc, branch_distance));
  return Result { .result = cond, .cycle_count = 3 };
}

Result
RV32I::BGE(RV32Assembler *as, Decoding decoding)
{
  const Operand rs1 = as->read_reg(decoding.rs1);
  const Operand rs2 = as->read_reg(decoding.rs2);
  const Operand old_pc = as->const_u32(decoding.encoding.pc);

  Operand branch_offset = as->const_u32(decoding.imm);

  Operand cond = as->cmp_gte(rs1, rs2);
  Operand branch_distance = as->select(cond, as->const_u32(4), branch_offset);
  as->write_reg(Registers::REG_PC, as->add(old_pc, branch_distance));
  return Result {
    .result = cond,
    .cycle_count = 3,
  };
}

Result
RV32I::BLTU(RV32Assembler *as, Decoding decoding)
{
  const Operand rs1 = as->read_reg(decoding.rs1);
  const Operand rs2 = as->read_reg(decoding.rs2);
  const Operand old_pc = as->const_u32(decoding.encoding.pc);

  // printf("Branch imm %d\n", decoding.imm);
  Operand branch_offset = as->const_u32(decoding.imm);

  Operand cond = as->cmp_ult(rs1, rs2);
  Operand branch_distance = as->select(cond, as->const_u32(4), branch_offset);
  as->write_reg(Registers::REG_PC, as->add(old_pc, branch_distance));
  return Result { .result = cond, .cycle_count = 3 };
}

Result
RV32I::BGEU(RV32Assembler *as, Decoding decoding)
{
  const Operand rs1 = as->read_reg(decoding.rs1);
  const Operand rs2 = as->read_reg(decoding.rs2);
  const Operand old_pc = as->const_u32(decoding.encoding.pc);

  Operand branch_offset = as->const_u32(decoding.imm);

  Operand cond = as->cmp_ugte(rs1, rs2);
  Operand branch_distance = as->select(cond, as->const_u32(4), branch_offset);
  as->write_reg(Registers::REG_PC, as->add(old_pc, branch_distance));
  return Result { .result = cond, .cycle_count = 3 };
}

// TODO: low-priority, variable load time. 3 is minimum

Result
RV32I::LB(RV32Assembler *as, Decoding decoding)
{
  Operand base = as->read_reg(decoding.rs1);
  Operand offset = as->const_u32(decoding.imm);
  Operand address = as->add(base, offset);

  Operand value = as->load(Type::Integer8, address);
  value = as->extend32(value);
  as->write_reg(decoding.rd, value);
  return Result { .cycle_count = 3 };
}

Result
RV32I::LH(RV32Assembler *as, Decoding decoding)
{
  Operand base = as->read_reg(decoding.rs1);
  Operand offset = as->const_u32(decoding.imm);
  Operand address = as->add(base, offset);

  Operand value = as->load(Type::Integer16, address);
  value = as->extend32(value);
  as->write_reg(decoding.rd, value);
  return Result { .cycle_count = 3 };
}

Result
RV32I::LW(RV32Assembler *as, Decoding decoding)
{
  Operand base = as->read_reg(decoding.rs1);
  Operand offset = as->const_u32(decoding.imm);
  Operand address = as->add(base, offset);

  Operand value = as->load(Type::Integer32, address);
  as->write_reg(decoding.rd, value);
  return Result { .cycle_count = 3 };
}

Result
RV32I::LBU(RV32Assembler *as, Decoding decoding)
{
  Operand base = as->read_reg(decoding.rs1);
  Operand offset = as->const_u32(decoding.imm);
  Operand address = as->add(base, offset);

  Operand value = as->load(Type::Integer8, address);
  value = as->bitcast(Type::Integer32, value);
  as->write_reg(decoding.rd, value);
  return Result { .cycle_count = 3 };
}

Result
RV32I::LHU(RV32Assembler *as, Decoding decoding)
{
  Operand base = as->read_reg(decoding.rs1);
  Operand offset = as->const_u32(decoding.imm);
  Operand address = as->add(base, offset);

  Operand value = as->load(Type::Integer16, address);
  value = as->bitcast(Type::Integer32, value);
  as->write_reg(decoding.rd, value);
  return Result { .cycle_count = 3 };
}

// Load and store instructions transfer a value between the registers and memory. Loads
// are encoded in the I-type format and stores are S-type. The effective address is
// obtained by adding register rs1 to the sign-extended 12-bit offset. Loads copy a
// value from memory to register rd. Stores copy the value in register rs2 to memory.

Result
RV32I::SB(RV32Assembler *as, Decoding decoding)
{
  Operand base = as->read_reg(decoding.rs1);
  Operand offset = as->const_u32(decoding.imm);
  Operand address = as->add(base, offset);

  Operand value = as->read_reg(decoding.rs2);
  value = as->bitcast(Type::Integer8, value);
  as->store(address, value);
  return Result { .cycle_count = 2 };
}

Result
RV32I::SH(RV32Assembler *as, Decoding decoding)
{
  Operand base = as->read_reg(decoding.rs1);
  Operand offset = as->const_u32(decoding.imm);
  Operand address = as->add(base, offset);

  Operand value = as->read_reg(decoding.rs2);
  value = as->bitcast(Type::Integer16, value);
  as->store(address, value);
  return Result { .cycle_count = 2 };
}

Result
RV32I::SW(RV32Assembler *as, Decoding decoding)
{
  Operand base = as->read_reg(decoding.rs1);
  Operand offset = as->const_u32(decoding.imm);
  Operand address = as->add(base, offset);

  Operand value = as->read_reg(decoding.rs2);
  as->store(address, value);
  return Result { .cycle_count = 2 };
}

Result
RV32I::ADDI(RV32Assembler *as, Decoding decoding)
{
  Operand rs1 = as->read_reg(decoding.rs1);
  Operand imm = as->const_u32(decoding.imm);
  Operand result = as->add(rs1, imm);
  as->write_reg(decoding.rd, result);
  return Result {};
}

Result
RV32I::SLTI(RV32Assembler *as, Decoding decoding)
{
  Operand rs1 = as->read_reg(decoding.rs1);
  Operand imm = as->const_u32(decoding.imm);
  Operand result = as->select(as->cmp_lt(rs1, imm), const_u32_0, const_u32_1);
  as->write_reg(decoding.rd, result);
  return Result {};
}

Result
RV32I::SLTIU(RV32Assembler *as, Decoding decoding)
{
  Operand rs1 = as->read_reg(decoding.rs1);
  Operand imm = as->const_u32(decoding.imm);
  Operand result = as->select(as->cmp_ult(rs1, imm), const_u32_0, const_u32_1);
  as->write_reg(decoding.rd, result);
  return Result {};
}

Result
RV32I::XORI(RV32Assembler *as, Decoding decoding)
{
  Operand rs1 = as->read_reg(decoding.rs1);
  Operand imm = as->const_u32(decoding.imm);
  Operand result = as->_xor(rs1, imm);
  as->write_reg(decoding.rd, result);
  return Result {};
}

Result
RV32I::ORI(RV32Assembler *as, Decoding decoding)
{
  Operand rs1 = as->read_reg(decoding.rs1);
  Operand imm = as->const_u32(decoding.imm);
  Operand result = as->_or(rs1, imm);
  as->write_reg(decoding.rd, result);
  return Result {};
}

Result
RV32I::ANDI(RV32Assembler *as, Decoding decoding)
{
  Operand rs1 = as->read_reg(decoding.rs1);
  Operand imm = as->const_u32(decoding.imm);
  Operand result = as->_and(rs1, imm);
  as->write_reg(decoding.rd, result);
  return Result {};
}

Result
RV32I::SLLI(RV32Assembler *as, Decoding decoding)
{
  Operand rs1 = as->read_reg(decoding.rs1);
  Operand imm = as->const_u32(decoding.imm & 0x1f);
  Operand result = as->shiftl(rs1, imm);
  as->write_reg(decoding.rd, result);
  return Result {};
}

Result
RV32I::SRLI(RV32Assembler *as, Decoding decoding)
{
  Operand rs1 = as->read_reg(decoding.rs1);
  Operand imm = as->const_u32(decoding.imm & 0x1f);
  Operand result = as->shiftr(rs1, imm);
  as->write_reg(decoding.rd, result);
  return Result {};
}

Result
RV32I::SRAI(RV32Assembler *as, Decoding decoding)
{
  Operand rs1 = as->read_reg(decoding.rs1);
  Operand imm = as->const_u32(decoding.imm & 0x1f);
  Operand result = as->ashiftr(rs1, imm);
  as->write_reg(decoding.rd, result);
  return Result {};
}

Result
RV32I::ADD(RV32Assembler *as, Decoding decoding)
{
  Operand rs1 = as->read_reg(decoding.rs1);
  Operand rs2 = as->read_reg(decoding.rs2);
  Operand result = as->add(rs1, rs2);
  as->write_reg(decoding.rd, result);
  return Result {};
}

Result
RV32I::SUB(RV32Assembler *as, Decoding decoding)
{
  Operand rs1 = as->read_reg(decoding.rs1);
  Operand rs2 = as->read_reg(decoding.rs2);
  Operand result = as->sub(rs1, rs2);
  as->write_reg(decoding.rd, result);
  return Result {};
}

Result
RV32I::SLL(RV32Assembler *as, Decoding decoding)
{
  Operand rs1 = as->read_reg(decoding.rs1);
  Operand rs2 = as->read_reg(decoding.rs2);
  Operand result = as->shiftl(rs1, as->_and(rs2, as->const_u32(0x1f)));
  as->write_reg(decoding.rd, result);
  return Result {};
}

Result
RV32I::SLT(RV32Assembler *as, Decoding decoding)
{
  Operand rs1 = as->read_reg(decoding.rs1);
  Operand rs2 = as->read_reg(decoding.rs2);
  Operand result = as->select(as->cmp_lt(rs1, rs2), const_u32_0, const_u32_1);
  as->write_reg(decoding.rd, result);
  return Result {};
}

Result
RV32I::SLTU(RV32Assembler *as, Decoding decoding)
{
  Operand rs1 = as->read_reg(decoding.rs1);
  Operand rs2 = as->read_reg(decoding.rs2);
  Operand result = as->select(as->cmp_ult(rs1, rs2), const_u32_0, const_u32_1);
  as->write_reg(decoding.rd, result);
  return Result {};
}

Result
RV32I::XOR(RV32Assembler *as, Decoding decoding)
{
  Operand rs1 = as->read_reg(decoding.rs1);
  Operand rs2 = as->read_reg(decoding.rs2);
  Operand result = as->_xor(rs1, rs2);
  as->write_reg(decoding.rd, result);
  return Result {};
}

Result
RV32I::SRL(RV32Assembler *as, Decoding decoding)
{
  Operand rs1 = as->read_reg(decoding.rs1);
  Operand rs2 = as->read_reg(decoding.rs2);
  Operand result = as->shiftr(rs1, rs2);
  as->write_reg(decoding.rd, result);
  return Result {};
}

Result
RV32I::SRA(RV32Assembler *as, Decoding decoding)
{
  Operand rs1 = as->read_reg(decoding.rs1);
  Operand amount = as->read_reg(decoding.rs2);
  amount = as->_and(amount, as->const_u32(0x1f));

  Operand result = as->ashiftr(rs1, amount);
  as->write_reg(decoding.rd, result);
  return Result {};
}

Result
RV32I::OR(RV32Assembler *as, Decoding decoding)
{
  Operand rs1 = as->read_reg(decoding.rs1);
  Operand rs2 = as->read_reg(decoding.rs2);
  Operand result = as->_or(rs1, rs2);
  as->write_reg(decoding.rd, result);
  return Result {};
}

Result
RV32I::AND(RV32Assembler *as, Decoding decoding)
{
  Operand rs1 = as->read_reg(decoding.rs1);
  Operand rs2 = as->read_reg(decoding.rs2);
  Operand result = as->_and(rs1, rs2);
  as->write_reg(decoding.rd, result);
  return Result {};
}

Result
RV32I::ECALL(RV32Assembler *as, Decoding decoding)
{
  throw std::runtime_error("ECALL not implemented");
  return Result {};
}

Result
RV32I::EBREAK(RV32Assembler *as, Decoding decoding)
{
  throw std::runtime_error("EBREAK not implemented");
  return Result {};
}

std::string
RV32I::disassemble(Decoding decoding)
{
  char buff[64];

  const u32 pc_plus_imm = decoding.encoding.pc + decoding.imm;

  const u32 rd = decoding.rd;
  const u32 rs1 = decoding.rs1;
  const u32 rs2 = decoding.rs2;

  const i32 imm = *(i32 *)&decoding.imm;
  const bool imm_probably_negative = imm < 0;

  switch (decoding.instruction) {
    case Instruction::LUI:
      snprintf(buff, sizeof(buff), "lui x%u, 0x%x", rd, imm >> 12);
      break;
    case Instruction::AUIPC:
      snprintf(buff, sizeof(buff), "todo instr=%d", u32(decoding.instruction));
      break;
    case Instruction::JAL:
      snprintf(buff, sizeof(buff), "jal x%d, 0x%08x", rd, pc_plus_imm);
      break;
    case Instruction::JALR:
      snprintf(buff, sizeof(buff), "jalr x%d, %d(x%d)", rd, imm, rs1);
      break;
    case Instruction::BEQ:
      snprintf(buff, sizeof(buff), "beq x%d, x%d, 0x%08x", rs1, rs2, pc_plus_imm);
      break;
    case Instruction::BNE:
      snprintf(buff, sizeof(buff), "bne x%d, x%d, 0x%08x", rs1, rs2, pc_plus_imm);
      break;
    case Instruction::BLT:
      snprintf(buff, sizeof(buff), "blt x%d, x%d, 0x%08x", rs1, rs2, pc_plus_imm);
      break;
    case Instruction::BGE:
      snprintf(buff, sizeof(buff), "bge x%d, x%d, 0x%08x", rs1, rs2, pc_plus_imm);
      break;
    case Instruction::BLTU:
      snprintf(buff, sizeof(buff), "bltu x%d, x%d, 0x%08x", rs1, rs2, pc_plus_imm);
      break;
    case Instruction::BGEU:
      snprintf(buff, sizeof(buff), "bgeu x%d, x%d, 0x%08x", rs1, rs2, pc_plus_imm);
      break;
    case Instruction::LB:
      snprintf(buff, sizeof(buff), "lb x%d, %d(x%d)", rd, imm, rs1);
      break;
    case Instruction::LH:
      snprintf(buff, sizeof(buff), "lh x%d, %d(x%d)", rd, imm, rs1);
      break;
    case Instruction::LW:
      snprintf(buff, sizeof(buff), "lw x%d, %d(x%d)", rd, imm, rs1);
      break;
    case Instruction::LBU:
      snprintf(buff, sizeof(buff), "lbu x%d, %d(x%d)", rd, imm, rs1);
      break;
    case Instruction::LHU:
      snprintf(buff, sizeof(buff), "lhu x%d, %d(x%d)", rd, imm, rs1);
      break;
    case Instruction::SB:
      snprintf(buff, sizeof(buff), "sb x%d, %d(x%d)", rs2, imm, rs1);
      break;
    case Instruction::SH:
      snprintf(buff, sizeof(buff), "sh x%d, %d(x%d)", rs2, imm, rs1);
      break;
    case Instruction::SW:
      snprintf(buff, sizeof(buff), "sw x%d, %d(x%d)", rs2, imm, rs1);
      break;
    case Instruction::ADDI:
      if (imm_probably_negative)
        snprintf(buff, sizeof(buff), "addi x%d, x%d, %d", rd, rs1, imm);
      else
        snprintf(buff, sizeof(buff), "addi x%d, x%d, 0x%x", rd, rs1, imm);
      break;
    case Instruction::SLTI:
      snprintf(buff, sizeof(buff), "slti x%d, x%d, %d", rd, rs1, imm);
      break;
    case Instruction::SLTIU:
      snprintf(buff, sizeof(buff), "sltiu x%d, x%d, 0x%x", rd, rs1, imm);
      break;
    case Instruction::XORI:
      snprintf(buff, sizeof(buff), "xori x%d, x%d, 0x%x", rd, rs1, imm);
      break;
    case Instruction::ORI:
      snprintf(buff, sizeof(buff), "ori x%d, x%d, 0x%x", rd, rs1, imm);
      break;
    case Instruction::ANDI:
      snprintf(buff, sizeof(buff), "andi x%d, x%d, 0x%x", rd, rs1, imm);
      break;
    case Instruction::SLLI:
      snprintf(buff, sizeof(buff), "slli x%d, x%d, %d", rd, rs1, imm);
      break;
    case Instruction::SRLI:
      snprintf(buff, sizeof(buff), "srli x%d, x%d, %d", rd, rs1, imm);
      break;
    case Instruction::SRAI:
      snprintf(buff, sizeof(buff), "srai x%d, x%d, %d", rd, rs1, imm);
      break;
    case Instruction::ADD:
      snprintf(buff, sizeof(buff), "add x%d, x%d, x%d", rd, rs1, rs2);
      break;
    case Instruction::SUB:
      snprintf(buff, sizeof(buff), "sub x%d, x%d, x%d", rd, rs1, rs2);
      break;
    case Instruction::SLL:
      snprintf(buff, sizeof(buff), "sll x%d, x%d, x%d", rd, rs1, rs2);
      break;
    case Instruction::SLT:
      snprintf(buff, sizeof(buff), "slt x%d, x%d, x%d", rd, rs1, rs2);
      break;
    case Instruction::SLTU:
      snprintf(buff, sizeof(buff), "sltu x%d, x%d, x%d", rd, rs1, rs2);
      break;
    case Instruction::XOR:
      snprintf(buff, sizeof(buff), "xor x%d, x%d, x%d", rd, rs1, rs2);
      break;
    case Instruction::SRL:
      snprintf(buff, sizeof(buff), "srl x%d, x%d, x%d", rd, rs1, rs2);
      break;
    case Instruction::SRA:
      snprintf(buff, sizeof(buff), "sra x%d, x%d, x%d", rd, rs1, rs2);
      break;
    case Instruction::OR:
      snprintf(buff, sizeof(buff), "or x%d, x%d, x%d", rd, rs1, rs2);
      break;
    case Instruction::AND:
      snprintf(buff, sizeof(buff), "and x%d, x%d, x%d", rd, rs1, rs2);
      break;
    case Instruction::ECALL:
      snprintf(buff, sizeof(buff), "todo instr=%d", u32(decoding.instruction));
      break;
    case Instruction::EBREAK:
      snprintf(buff, sizeof(buff), "todo instr=%d", u32(decoding.instruction));
      break;
    default:
      throw "Failed to decode rv32i instruction provided";
  }

  return buff;
}

} // namespace guest::rv32
