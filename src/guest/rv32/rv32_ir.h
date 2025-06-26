#pragma once

#include "shared/types.h"
#include "fox/ir_assembler.h"

namespace guest::rv32 {

class RV32;

namespace Branch {
enum BranchType : u32
{
  BEQ = 0b000,
  BNE = 0b001,
  BLT = 0b100,
  BGE = 0b101,
  BLTU = 0b110,
  BGEU = 0b111,
};
}

enum class EncodingType
{
  R,
  I,
  S,
  B,
  U,
  J
};

enum class Instruction : u32
{
  __NOT_DECODED__ = 0,

  // RV32I Base
  LUI,
  AUIPC,
  JAL,
  JALR,
  BEQ,
  BNE,
  BLT,
  BGE,
  BLTU,
  BGEU,
  LB,
  LH,
  LW,
  LBU,
  LHU,
  SB,
  SH,
  SW,
  ADDI,
  SLTI,
  SLTIU,
  XORI,
  ORI,
  ANDI,
  SLLI,
  SRLI,
  SRAI,
  ADD,
  SUB,
  SLL,
  SLT,
  SLTU,
  XOR,
  SRL,
  SRA,
  OR,
  AND,
  ECALL,
  EBREAK,

  // RV32M
  MUL,
  MULH,
  MULHSU,
  MULHU,
  DIV,
  DIVU,
  REM,
  REMU,

  // Zicsr
  CSRRW,
  CSRRS,
  CSRRC,
  CSRRWI,
  CSRRSI,
  CSRRCI,

  // Zicond
  CZERO_EQZ,
  CZERO_NEZ,

  // Currently Unorganized
  // ...

  // TODO
  //   Zba intersect RV32
  // andn
  // xnor
  // orn
  // zbkb (32.3.1) (not the last 4)
  // clz
  // orc.b
  // mret
  // fence
  // fence.i
  // cbo.clean
  // cbo.flush
  // cbo.invalelf

};

namespace Registers {
enum Register
{
  REG_X_START = 0,
  REG_PC = 32,
  REG_CSR_START = REG_PC + 1,
  // TODO : CSRs
  __NUM_REGISTERS,
};
}

struct Encoding {
  union {
    struct {
      u32 opcode : 7;
      u32 rd : 5;
      u32 funct3 : 3;
      u32 rs1 : 5;
      u32 rs2 : 5;
      u32 funct7 : 7;
    } R;
    struct {
      u32 opcode : 7;
      u32 rd : 5;
      u32 funct3 : 3;
      u32 rs1 : 5;
      u32 imm_11_0 : 12;
    } I;
    struct {
      u32 opcode : 7;
      u32 imm_4_0 : 5;
      u32 funct3 : 3;
      u32 rs1 : 5;
      u32 rs2 : 5;
      u32 imm_11_5 : 7;
    } S;
    struct {
      u32 opcode : 7;
      u32 imm_11 : 1;
      u32 imm_4_1 : 4;
      u32 funct3 : 3;
      u32 rs1 : 5;
      u32 rs2 : 5;
      u32 imm_10_5 : 6;
      u32 imm_12 : 1;
    } B;
    struct {
      u32 opcode : 7;
      u32 rd : 5;
      u32 imm_31_12 : 20;
    } U;
    struct {
      u32 opcode : 7;
      u32 rd : 5;
      u32 imm_19_12 : 8;
      u32 imm_11 : 1;
      u32 imm_10_1 : 10;
      u32 imm_20 : 1;
    } J;
    u32 raw;
  };

  u32 pc;
};

class RV32Assembler : public fox::ir::Assembler {
public:
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

  fox::ir::ExecutionUnit &&assemble(RV32* cpu, u32 address, u32 end_address);

  fox::ir::Operand read_reg(u16 index);
  void write_reg(u16 index, fox::ir::Operand value);

  void disassemble(char *buffer, size_t buffer_size);
};

struct Decoding {
  enum class Flag : u32
  {
    ConditionalJump = 1 << 0,
    UnconditionalJump = 1 << 1,
  };

  Instruction instruction = Instruction::__NOT_DECODED__;
  Encoding encoding;
  EncodingType encoding_type;

  u32 flags = 0;
  u32 rd = 0;
  u32 rs1 = 0;
  u32 rs2 = 0;
  u32 imm = 0;
  u32 funct3 = 0;
  u32 funct7 = 0;

  Decoding() {}
  Decoding(Encoding encoding, Instruction instruction, EncodingType encoding_type);
  Decoding flag(Flag f)
  {
    flags |= u32(f);
    return *this;
  }
  bool valid() const
  {
    return instruction != Instruction::__NOT_DECODED__;
  }
};

struct Result {
  fox::ir::Operand result = fox::ir::Operand::constant<bool>(false);
  u32 cycle_count = 1;
};

class RV32InstructionSet {
public:
  virtual ~RV32InstructionSet() = default;

  virtual Decoding decode(Encoding) = 0;

  // Attempt to assemble to instruction.
  virtual Result assemble(RV32Assembler *, Decoding) = 0;

  // Attempt to disassemble the encoded instruction
  virtual std::string disassemble(Decoding) = 0;
};

class RV32I final : public RV32InstructionSet {
public:
  Decoding decode(Encoding);
  Result assemble(RV32Assembler *, Decoding);
  std::string disassemble(Decoding decoding);

  Result LUI(RV32Assembler *, Decoding decoding);
  Result AUIPC(RV32Assembler *, Decoding decoding);
  Result JAL(RV32Assembler *, Decoding decoding);
  Result JALR(RV32Assembler *, Decoding decoding);
  Result BEQ(RV32Assembler *, Decoding decoding);
  Result BNE(RV32Assembler *, Decoding decoding);
  Result BLT(RV32Assembler *, Decoding decoding);
  Result BGE(RV32Assembler *, Decoding decoding);
  Result BLTU(RV32Assembler *, Decoding decoding);
  Result BGEU(RV32Assembler *, Decoding decoding);
  Result LB(RV32Assembler *, Decoding decoding);
  Result LH(RV32Assembler *, Decoding decoding);
  Result LW(RV32Assembler *, Decoding decoding);
  Result LBU(RV32Assembler *, Decoding decoding);
  Result LHU(RV32Assembler *, Decoding decoding);
  Result SB(RV32Assembler *, Decoding decoding);
  Result SH(RV32Assembler *, Decoding decoding);
  Result SW(RV32Assembler *, Decoding decoding);
  Result ADDI(RV32Assembler *, Decoding decoding);
  Result SLTI(RV32Assembler *, Decoding decoding);
  Result SLTIU(RV32Assembler *, Decoding decoding);
  Result XORI(RV32Assembler *, Decoding decoding);
  Result ORI(RV32Assembler *, Decoding decoding);
  Result ANDI(RV32Assembler *, Decoding decoding);
  Result SLLI(RV32Assembler *, Decoding decoding);
  Result SRLI(RV32Assembler *, Decoding decoding);
  Result SRAI(RV32Assembler *, Decoding decoding);
  Result ADD(RV32Assembler *, Decoding decoding);
  Result SUB(RV32Assembler *, Decoding decoding);
  Result SLL(RV32Assembler *, Decoding decoding);
  Result SLT(RV32Assembler *, Decoding decoding);
  Result SLTU(RV32Assembler *, Decoding decoding);
  Result XOR(RV32Assembler *, Decoding decoding);
  Result SRL(RV32Assembler *, Decoding decoding);
  Result SRA(RV32Assembler *, Decoding decoding);
  Result OR(RV32Assembler *, Decoding decoding);
  Result AND(RV32Assembler *, Decoding decoding);
  Result ECALL(RV32Assembler *, Decoding decoding);
  Result EBREAK(RV32Assembler *, Decoding decoding);
};

class RV32M final : public RV32InstructionSet {
public:
  Decoding decode(Encoding);
  Result assemble(RV32Assembler *, Decoding);
  std::string disassemble(Decoding decoding);

  Result MUL(RV32Assembler *, Decoding decoding);
  Result MULH(RV32Assembler *, Decoding decoding);
  Result MULHSU(RV32Assembler *, Decoding decoding);
  Result MULHU(RV32Assembler *, Decoding decoding);
  Result DIV(RV32Assembler *, Decoding decoding);
  Result DIVU(RV32Assembler *, Decoding decoding);
  Result REM(RV32Assembler *, Decoding decoding);
  Result REMU(RV32Assembler *, Decoding decoding);
};

//  Extension for Control and Status Register (CSR) Instructions
class RV32Zicsr final : public RV32InstructionSet {
public:
  Decoding decode(Encoding);
  Result assemble(RV32Assembler *, Decoding);
  std::string disassemble(Decoding decoding);

  Result CSRRW(RV32Assembler *, Decoding decoding);
  Result CSRRS(RV32Assembler *, Decoding decoding);
  Result CSRRC(RV32Assembler *, Decoding decoding);
  Result CSRRWI(RV32Assembler *, Decoding decoding);
  Result CSRRSI(RV32Assembler *, Decoding decoding);
  Result CSRRCI(RV32Assembler *, Decoding decoding);

protected:
  void csr_write(RV32Assembler *as, u16 csr_index, fox::ir::Operand value);
  fox::ir::Operand csr_read(RV32Assembler *as, u16 csr_index);
};

class RV32Zicond final : public RV32InstructionSet {
public:
  Decoding decode(Encoding);
  Result assemble(RV32Assembler *, Decoding);
  std::string disassemble(Decoding decoding);

  Result CZERO_EQZ(RV32Assembler *, Decoding decoding);
  Result CZERO_NEZ(RV32Assembler *, Decoding decoding);
};

}
