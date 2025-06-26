#include "rv32_ir.h"
#include "shared/bitmanip.h"
#include "fmt/core.h"

using namespace fox::ir;

namespace guest::rv32 {

const Operand const_u32_0 = Operand::constant<u32>(0);
const Operand const_u32_1 = Operand::constant<u32>(1);
const Decoding FAILED_TO_DECODE;

Decoding
RV32M::decode(Encoding enc)
{
  using D = Decoding;
  using I = Instruction;

  if (enc.R.opcode == 0b0110011 && enc.R.funct7 == 1) {
    switch (enc.R.funct3) {
        // clang-format off
      case 0b000: return D(enc, I::MUL,    EncodingType::R);
      case 0b001: return D(enc, I::MULH,   EncodingType::R);
      case 0b010: return D(enc, I::MULHSU, EncodingType::R);
      case 0b011: return D(enc, I::MULHU,  EncodingType::R);
      case 0b100: return D(enc, I::DIV,    EncodingType::R);
      case 0b101: return D(enc, I::DIVU,   EncodingType::R);
      case 0b110: return D(enc, I::REM,    EncodingType::R);
      case 0b111: return D(enc, I::REMU,   EncodingType::R);
      default:    break;
        // clang-format on
    }
  }

  return FAILED_TO_DECODE;
}

Result
RV32M::assemble(RV32Assembler *as, Decoding decoding)
{
  switch (decoding.instruction) {
      // clang-format off
    case Instruction::MUL:    return MUL   (as, decoding); break;
    case Instruction::MULH:   return MULH  (as, decoding); break;
    case Instruction::MULHSU: return MULHSU(as, decoding); break;
    case Instruction::MULHU:  return MULHU (as, decoding); break;
    case Instruction::DIV:    return DIV   (as, decoding); break;
    case Instruction::DIVU:   return DIVU  (as, decoding); break;
    case Instruction::REM:    return REM   (as, decoding); break;
    case Instruction::REMU:   return REMU  (as, decoding); break;
    default:
      assert(false && "Assembly failed");
      // clang-format on
  }
  throw std::runtime_error("assemble failed");
}

Result
RV32M::MUL(RV32Assembler *as, Decoding decoding)
{
  const Operand rs1 = as->read_reg(decoding.rs1);
  const Operand rs2 = as->read_reg(decoding.rs2);
  const Operand result = as->mul(rs1, rs2);
  as->write_reg(decoding.rd, result);
  return Result {.cycle_count = 4};
}

Result
RV32M::MULH(RV32Assembler *as, Decoding decoding)
{
  Operand rs1 = as->extend64(as->read_reg(decoding.rs1));
  Operand rs2 = as->extend64(as->read_reg(decoding.rs2));
  Operand result = as->mul(rs1, rs2);
  result = as->shiftr(result, as->const_u32(32));
  result = as->bitcast(Type::Integer32, result);
  as->write_reg(decoding.rd, result);
  return Result {.cycle_count = 4};
}

Result
RV32M::MULHSU(RV32Assembler *as, Decoding decoding)
{
  throw "unimplemented mulhsu";
  return Result {};
}

Result
RV32M::MULHU(RV32Assembler *as, Decoding decoding)
{
  throw "unimplemented mulhu";
  return Result {};
}

Result
RV32M::DIV(RV32Assembler *as, Decoding decoding)
{
  const Operand rs1 = as->read_reg(decoding.rs1);
  const Operand rs2 = as->read_reg(decoding.rs2);
  // const Operand result = as->div(rs1, rs2);

  const Operand result = as->call(
    fox::ir::Type::Integer32,
    [](fox::Guest *guest, fox::Value rs, fox::Value rt) {
      const i32 rsi = i32(rs.u32_value);
      const i32 rti = i32(rt.u32_value);
      return fox::Value { .u32_value = u32(rsi / rti) };
      // return fox::Value { .u32_value = static_cast<i32>(rs.u32_value) / static_cast<i32>(rt.u32_value) };
    },
    rs1,
    rs2);

  as->write_reg(decoding.rd, result);

  return Result {.cycle_count = 40};
}

Result
RV32M::DIVU(RV32Assembler *as, Decoding decoding)
{
  Operand rs1 = as->read_reg(decoding.rs1);
  Operand rs2 = as->read_reg(decoding.rs2);

  rs1 = as->bitcast(Type::Integer64, rs1);
  rs2 = as->bitcast(Type::Integer64, rs2);

  // Operand result = as->div(rs1, rs2);
  Operand result = as->call(
    fox::ir::Type::Integer32,
    [](fox::Guest *guest, fox::Value rs, fox::Value rt) {
      return fox::Value { .u32_value = rs.u32_value / rt.u32_value };
    },
    rs1,
    rs2);

  result = as->bitcast(Type::Integer32, result);

  as->write_reg(decoding.rd, result);
  return Result {.cycle_count = 40};
}

Result
RV32M::REM(RV32Assembler *as, Decoding decoding)
{
  throw "unimplemented rem";
  return Result {.cycle_count = 40};
}

Result
RV32M::REMU(RV32Assembler *as, Decoding decoding)
{
  // TODO : Handle edge cases
  const Operand rs1 = as->read_reg(decoding.rs1);
  const Operand rs2 = as->read_reg(decoding.rs2);
  // const Operand x = as->div(rs1, rs2);

  Operand x = as->call(
    fox::ir::Type::Integer32,
    [](fox::Guest *guest, fox::Value rs, fox::Value rt) {
      return fox::Value { .u32_value = rs.u32_value / rt.u32_value };
    },
    rs1,
    rs2);

  const Operand y = as->mul(rs2, x);
  const Operand remainder = as->sub(rs1, y);
  as->write_reg(decoding.rd, remainder);
  return Result {.cycle_count = 40};
}

std::string
RV32M::disassemble(Decoding decoding)
{
  char buff[64];

  const u32 rd = decoding.rd;
  const u32 rs1 = decoding.rs1;
  const u32 rs2 = decoding.rs2;

  switch (decoding.instruction) {
    case Instruction::MUL:
      snprintf(buff, sizeof(buff), "mul x%d, x%d, x%d", rd, rs1, rs2);
      break;
    case Instruction::MULH:
      snprintf(buff, sizeof(buff), "mulh x%d, x%d, x%d", rd, rs1, rs2);
      break;
    case Instruction::MULHSU:
      snprintf(buff, sizeof(buff), "mulhsu x%d, x%d, x%d", rd, rs1, rs2);
      break;
    case Instruction::MULHU:
      snprintf(buff, sizeof(buff), "mulhu x%d, x%d, x%d", rd, rs1, rs2);
      break;
    case Instruction::DIV:
      snprintf(buff, sizeof(buff), "div x%d, x%d, x%d", rd, rs1, rs2);
      break;
    case Instruction::DIVU:
      snprintf(buff, sizeof(buff), "divu x%d, x%d, x%d", rd, rs1, rs2);
      break;
    case Instruction::REM:
      snprintf(buff, sizeof(buff), "rem x%d, x%d, x%d", rd, rs1, rs2);
      break;
    case Instruction::REMU:
      snprintf(buff, sizeof(buff), "remu x%d, x%d, x%d", rd, rs1, rs2);
      break;
    default:
      throw "Failed to disassemble rv32m instruction provided";
  }

  return buff;
}

} // namespace guest::rv32
