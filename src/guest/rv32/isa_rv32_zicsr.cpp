#include "rv32.h"
#include "rv32_ir.h"
#include "shared/bitmanip.h"
#include "fmt/core.h"

using namespace fox::ir;

namespace guest::rv32 {

const Operand const_u32_0 = Operand::constant<u32>(0);
const Operand const_u32_1 = Operand::constant<u32>(1);
const Decoding FAILED_TO_DECODE;

Decoding
RV32Zicsr::decode(Encoding enc)
{
  using D = Decoding;
  using I = Instruction;

  if (enc.R.opcode == 0b1110011) {
    switch (enc.R.funct3) {
        // clang-format off
      case 0b001: return D(enc, I::CSRRW,  EncodingType::I);
      case 0b010: return D(enc, I::CSRRS,  EncodingType::I);
      case 0b011: return D(enc, I::CSRRC,  EncodingType::I);
      case 0b101: return D(enc, I::CSRRWI, EncodingType::I);
      case 0b110: return D(enc, I::CSRRSI, EncodingType::I);
      case 0b111: return D(enc, I::CSRRCI, EncodingType::I);
      default:    break;
        // clang-format on
    }
  }

  return FAILED_TO_DECODE;
}

Result
RV32Zicsr::assemble(RV32Assembler *as, Decoding decoding)
{
  switch (decoding.instruction) {
      // clang-format off
    case Instruction::CSRRW:  return CSRRW (as, decoding); break;
    case Instruction::CSRRS:  return CSRRS (as, decoding); break;
    case Instruction::CSRRC:  return CSRRC (as, decoding); break;
    case Instruction::CSRRWI: return CSRRWI(as, decoding); break;
    case Instruction::CSRRSI: return CSRRSI(as, decoding); break;
    case Instruction::CSRRCI: return CSRRCI(as, decoding); break;
    default:
      assert(false && "Assembly failed");
      throw std::runtime_error("assemble failed");
      // clang-format on
  }
}

void
RV32Zicsr::csr_write(RV32Assembler *as, u16 csr_index, Operand value)
{
  switch (csr_index) {
    // TODO
    default:
      break;
  }

  throw std::runtime_error("unhandled csr_write index");
}

Operand
RV32Zicsr::csr_read(RV32Assembler *as, u16 csr_index)
{
  switch (csr_index) {
    // TODO
    default:
      break;
  }

  throw std::runtime_error("unhandled csr_read index");
}

Result
RV32Zicsr::CSRRW(RV32Assembler *as, Decoding decoding)
{
  const u32 csr_index = decoding.encoding.I.imm_11_0;

  // Need to capture rs1 before the rd-write below to cover the case where rd==rs1
  // properly
  const Operand rs1_old = as->read_reg(decoding.rs1);

  // "If rd=x0, then the instruction shall not read the CSR and shall not cause any of the
  // side effects that might occur on a CSR read."
  if (decoding.rd != 0) {
    const Operand csr_old = csr_read(as, csr_index);
    as->write_reg(decoding.rd, csr_old);
  }

  csr_write(as, csr_index, rs1_old);
  return Result {};
}

Result
RV32Zicsr::CSRRS(RV32Assembler *as, Decoding decoding)
{
  const u32 csr_index = decoding.encoding.I.imm_11_0;
  const Operand csr_value = csr_read(as, csr_index);

  // For both CSRRS and CSRRC, if rs1=x0, then the instruction will not write to the CSR
  // at all, and so shall not cause any of the side effects that might otherwise occur on
  // a CSR write, nor raise illegal-instruction exceptions on accesses to read-only CSRs.
  // Both CSRRS and CSRRC always read the addressed CSR and cause any read side effects
  // regardless of rs1 and rd fields.
  if (decoding.rs1 != 0) {
    const Operand setbits = as->read_reg(decoding.rs1);
    const Operand csr_new = as->_or(csr_value, setbits);
    csr_write(as, csr_index, csr_new);
  } else {
    // TODO: Today the optimizer likely sees the csr read as having no side-effects, so it
    // may be optimized away. Should make sure that any CSR reads that generate side
    // effects are handled.
  }
  return Result {};
}

Result
RV32Zicsr::CSRRC(RV32Assembler *as, Decoding decoding)
{
  const u32 csr_index = decoding.encoding.I.imm_11_0;
  const Operand csr_value = csr_read(as, csr_index);

  if (decoding.rs1 != 0) {
    const Operand bits = as->_not(as->read_reg(decoding.rs1));
    const Operand csr_new = as->_and(csr_value, bits);
    csr_write(as, csr_index, csr_new);
  } else {
    // TODO: Today the optimizer likely sees the csr read as having no side-effects, so it
    // may be optimized away. Should make sure that any CSR reads that generate side
    // effects are handled.
  }
  return Result {};
}

Result
RV32Zicsr::CSRRWI(RV32Assembler *as, Decoding decoding)
{
  const u32 csr_index = decoding.encoding.I.imm_11_0;

  // Need to capture rs1 before the rd-write below to cover the case where rd==rs1
  // properly
  const Operand csr_write_value = as->const_u32(decoding.rs1);

  // "If rd=x0, then the instruction shall not read the CSR and shall not cause any of the
  // side effects that might occur on a CSR read."
  if (decoding.rd != 0) {
    const Operand csr_old = csr_read(as, csr_index);
    as->write_reg(decoding.rd, csr_old);
  }

  csr_write(as, csr_index, csr_write_value);
  return Result {};
}

Result
RV32Zicsr::CSRRSI(RV32Assembler *as, Decoding decoding)
{
  const u32 csr_index = decoding.encoding.I.imm_11_0;
  const Operand csr_value = csr_read(as, csr_index);

  // For both CSRRS and CSRRC, if rs1=x0, then the instruction will not write to the CSR
  // at all, and so shall not cause any of the side effects that might otherwise occur on
  // a CSR write, nor raise illegal-instruction exceptions on accesses to read-only CSRs.
  // Both CSRRS and CSRRC always read the addressed CSR and cause any read side effects
  // regardless of rs1 and rd fields.
  if (decoding.rs1 != 0) {
    const Operand setbits = as->const_u32(decoding.rs1);
    const Operand csr_new = as->_or(csr_value, setbits);
    csr_write(as, csr_index, csr_new);
  } else {
    // TODO: Today the optimizer likely sees the csr read as having no side-effects, so it
    // may be optimized away. Should make sure that any CSR reads that generate side
    // effects are handled.
  }
  return Result {};
}

Result
RV32Zicsr::CSRRCI(RV32Assembler *as, Decoding decoding)
{
  const u32 csr_index = decoding.encoding.I.imm_11_0;
  const Operand csr_value = csr_read(as, csr_index);

  if (decoding.rs1 != 0) {
    const Operand bits = as->_not(as->const_u32(decoding.rs1));
    const Operand csr_new = as->_and(csr_value, bits);
    csr_write(as, csr_index, csr_new);
  } else {
    // TODO: Today the optimizer likely sees the csr read as having no side-effects, so it
    // may be optimized away. Should make sure that any CSR reads that generate side
    // effects are handled.
  }
  return Result {};
}

std::string
RV32Zicsr::disassemble(Decoding decoding)
{
  char buff[64];

  // const u32 pc_plus_imm = decoding.encoding.pc + decoding.imm;

  const u32 rd = decoding.rd;
  const u32 rs1 = decoding.rs1;
  const u32 rs2 = decoding.rs2;

  // const i32 imm = *(i32 *)&decoding.imm;
  // const i32 neg_imm = -imm;
  // const bool imm_probably_negative = imm < 0;

  switch (decoding.instruction) {
    case Instruction::CSRRW:
      snprintf(buff, sizeof(buff), "csrrw x%d, csr_0x%x, x%d", rd, rs2, rs1);
      break;
    case Instruction::CSRRS:
      snprintf(buff, sizeof(buff), "csrrs x%d, csr_0x%x, x%d", rd, rs2, rs1);
      break;
    case Instruction::CSRRC:
      snprintf(buff, sizeof(buff), "csrrc x%d, csr_0x%x, x%d", rd, rs2, rs1);
      break;

    case Instruction::CSRRWI:
      snprintf(buff, sizeof(buff), "csrrwi x%d, csr_0x%x, 0x%d", rd, rs2, rs1);
      break;
    case Instruction::CSRRSI:
      snprintf(buff, sizeof(buff), "csrrsi x%d, csr_0x%x, 0x%d", rd, rs2, rs1);
      break;
    case Instruction::CSRRCI:
      snprintf(buff, sizeof(buff), "csrrci x%d, csr_0x%x, 0x%d", rd, rs2, rs1);
      break;

    default:
      throw "Failed to disassemble rv32zicsr instruction provided";
  }

  return buff;
}

} // namespace guest::rv32
