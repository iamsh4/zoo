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
RV32Zicond::decode(Encoding enc)
{
  using D = Decoding;
  using I = Instruction;

  if (enc.R.opcode == 0b0110011 && enc.R.funct7 == 0b0000111) {
    switch (enc.R.funct3) {
        // clang-format off
      case 0b101: return D(enc, I::CZERO_EQZ, EncodingType::R);
      case 0b111: return D(enc, I::CZERO_NEZ, EncodingType::R);
      default:    break;
        // clang-format on
    }
  }

  return FAILED_TO_DECODE;
}

Result
RV32Zicond::assemble(RV32Assembler *as, Decoding decoding)
{
  switch (decoding.instruction) {
      // clang-format off
    case Instruction::CZERO_EQZ: return CZERO_EQZ (as, decoding); break;
    case Instruction::CZERO_NEZ: return CZERO_NEZ (as, decoding); break;
    default:
      assert(false && "Assembly failed");
      throw std::runtime_error("assemble failed");
      // clang-format on
  }
}

Result
RV32Zicond::CZERO_EQZ(RV32Assembler *as, Decoding decoding)
{
  const Operand rs1 = as->read_reg(decoding.rs1);
  const Operand rs2 = as->read_reg(decoding.rs2);
  const Operand value = as->select(as->cmp_eq(rs2, const_u32_0), rs1, const_u32_0);
  as->write_reg(decoding.rd, value);
  return Result{};
}

Result
RV32Zicond::CZERO_NEZ(RV32Assembler *as, Decoding decoding)
{
  const Operand rs1 = as->read_reg(decoding.rs1);
  const Operand rs2 = as->read_reg(decoding.rs2);
  const Operand value = as->select(as->cmp_eq(rs2, const_u32_0), const_u32_0, rs1);
  as->write_reg(decoding.rd, value);
  return Result{};
}

std::string
RV32Zicond::disassemble(Decoding decoding)
{
  char buff[64];

  const u32 rd = decoding.rd;
  const u32 rs1 = decoding.rs1;
  const u32 rs2 = decoding.rs2;

  switch (decoding.instruction) {
    case Instruction::CZERO_EQZ:
      snprintf(buff, sizeof(buff), "czero.eqz x%d, x%d, x%d", rd, rs1, rs2);
      break;
    case Instruction::CZERO_NEZ:
      snprintf(buff, sizeof(buff), "czero.nez x%d, x%d, x%d", rd, rs1, rs2);
      break;

    default:
      throw "Failed to disassemble rv32zicsr instruction provided";
  }

  return buff;
}

} // namespace guest::rv32
