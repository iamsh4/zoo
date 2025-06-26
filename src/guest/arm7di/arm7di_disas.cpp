#include <string>

#include "arm7di_disas.h"
#include "shared/bitmanip.h"

namespace guest::arm7di {

const std::string unknown = "???";

enum DataProcSubOperation
{
  Data_AND = 0,
  Data_EOR,
  Data_SUB,
  Data_RSB,
  Data_ADD,
  Data_ADC,
  Data_SBC,
  Data_RSC,
  Data_TST,
  Data_TEQ,
  Data_CMP,
  Data_CMN,
  Data_ORR,
  Data_MOV,
  Data_BIC,
  Data_MVN
};

enum class Opcode
{
  Unknown,
  AND,
  EOR,
  SUB,
  RSB,
  ADD,
  ADC,
  SBC,
  RSC,
  TST,
  TEQ,
  CMP,
  CMN,
  ORR,
  MOV,
  BIC,
  MVN,

  MUL,

  B,
  BL,

  SWI,

  LDR,
  LDRB,
  LDRT,
  LDRBT,

  STR,
  STRB,
  STRT,
  STRBT,
};

Opcode
decode_opcode(OpcodeClass opcode_class, Arm7DIInstructionInfo info)
{
  Opcode result = Opcode::Unknown;

  const u32 word = info.word;

  switch (opcode_class) {
    case OpcodeClass::DataProcessing: {
      OpcodeDataProcessing op { .raw = word };
      switch (op.opcode) {
          // clang-format off
        case Data_TST: result = Opcode::TST; break;
        case Data_AND: result = Opcode::AND; break;
        case Data_TEQ: result = Opcode::TEQ; break;
        case Data_EOR: result = Opcode::EOR; break;
        case Data_RSB: result = Opcode::RSB; break;
        case Data_CMP: result = Opcode::CMP; break;
        case Data_SUB: result = Opcode::SUB; break;
        case Data_CMN: result = Opcode::CMN; break;
        case Data_ADD: result = Opcode::ADD; break;
        case Data_ADC: result = Opcode::ADC; break;
        case Data_RSC: result = Opcode::RSC; break;
        case Data_SBC: result = Opcode::SBC; break;
        case Data_ORR: result = Opcode::ORR; break;
        case Data_MOV: result = Opcode::MOV; break;
        case Data_BIC: result = Opcode::BIC; break;
        case Data_MVN: result = Opcode::MVN; break;    
        default:                             break;
          // clang-format on
      }
      break;
    }
    case OpcodeClass::Multiply:
      result = Opcode::MUL;
      break;
    case OpcodeClass::SingleDataSwap:
      result = Opcode::Unknown;
      break;
    case OpcodeClass::SingleDataTransfer:
      // UBWL bits
      switch ((word >> 20) & 0b111) {
          // clang-format off
        case 0b000: result = Opcode::LDR;     break;
        case 0b001: result = Opcode::STR;     break;
        case 0b010: result = Opcode::LDRT;    break;
        case 0b011: result = Opcode::STRT;    break;
        case 0b100: result = Opcode::LDRB;    break;
        case 0b101: result = Opcode::STRB;    break;
        case 0b110: result = Opcode::LDRBT;   break;
        case 0b111: result = Opcode::STRBT;   break;
        default:     throw std::runtime_error("Invalid UBWL bits");
          // clang-format on
      }
      break;
    case OpcodeClass::Undefined:
      result = Opcode::Unknown;
      break;
    case OpcodeClass::BlockDataTransfer:
      result = Opcode::Unknown;
      break;
    case OpcodeClass::Branch:
      if (word & (1 << 24)) {
        result = Opcode::BL;
      } else {
        result = Opcode::B;
      }
      break;
    case OpcodeClass::CoprocDataTransfer:
      result = Opcode::Unknown;
      break;
    case OpcodeClass::CoprocDataOperation:
      result = Opcode::Unknown;
      break;
    case OpcodeClass::CoprocRegisterTransfer:
      result = Opcode::Unknown;
      break;
    case OpcodeClass::SoftwareInterrupt:
      result = Opcode::SWI;
      break;
    default:
      result = Opcode::Unknown;
      break;
  }

  return result;
}

std::string
disassemble(Arm7DIInstructionInfo info)
{
  char buff[256] = { 0 };
  #if 0
  char *result   = buff;
  const u32 word                 = info.word;
  const OpcodeClass opcode_class = decode_opcode_class(word);
  const Opcode opcode            = decode_opcode(opcode_class, info);

  // Op name
  switch (opcode) {
      // clang-format off
    case Opcode::AND:     result += snprintf(result, sizeof(result), "and"); break;
    case Opcode::EOR:     result += snprintf(result, sizeof(result), "eor"); break;
    case Opcode::SUB:     result += snprintf(result, sizeof(result), "sub"); break; 
    case Opcode::RSB:     result += snprintf(result, sizeof(result), "rsb"); break;
    case Opcode::ADD:     result += snprintf(result, sizeof(result), "add"); break;
    case Opcode::ADC:     result += snprintf(result, sizeof(result), "adc"); break;
    case Opcode::SBC:     result += snprintf(result, sizeof(result), "sbc"); break;
    case Opcode::RSC:     result += snprintf(result, sizeof(result), "rsc"); break;
    case Opcode::TST:     result += snprintf(result, sizeof(result), "tst"); break;
    case Opcode::TEQ:     result += snprintf(result, sizeof(result), "teq"); break;
    case Opcode::CMP:     result += snprintf(result, sizeof(result), "cmp"); break;
    case Opcode::CMN:     result += snprintf(result, sizeof(result), "cmn"); break;
    case Opcode::ORR:     result += snprintf(result, sizeof(result), "orr"); break;
    case Opcode::MOV:     result += snprintf(result, sizeof(result), "mov"); break;
    case Opcode::BIC:     result += snprintf(result, sizeof(result), "bic"); break;
    case Opcode::MVN:     result += snprintf(result, sizeof(result), "mvn"); break;
    case Opcode::MUL:     result += snprintf(result, sizeof(result), "mul"); break;
    case Opcode::SWI:     result += snprintf(result, sizeof(result), "swi"); break;

    case Opcode::B:       result += snprintf(result, sizeof(result), "b"); break;
    case Opcode::BL:      result += snprintf(result, sizeof(result), "bl"); break;
    case Opcode::LDR:     result += snprintf(result, sizeof(result), "ldr"); break;
    case Opcode::LDRB:    result += snprintf(result, sizeof(result), "ldrb"); break;
    case Opcode::LDRT:    result += snprintf(result, sizeof(result), "ldrt"); break;
    case Opcode::LDRBT:   result += snprintf(result, sizeof(result), "ldrbt"); break;
    case Opcode::STR:     result += snprintf(result, sizeof(result), "str"); break;
    case Opcode::STRB:    result += snprintf(result, sizeof(result), "strb"); break;
    case Opcode::STRT:    result += snprintf(result, sizeof(result), "strt"); break;
    case Opcode::STRBT:   result += snprintf(result, sizeof(result), "strbt"); break;
    case Opcode::Unknown: result += snprintf(result, sizeof(result), "???"); break;
    default:                                              break;
      // clang-format on
  }

  // condition code suffix
  const u32 cond_bits = info.word >> 28;
  switch (cond_bits) {
      // clang-format off
    case 0b0000: result += snprintf(result, sizeof(result), "eq"); break; // EQ : Z set
    case 0b0001: result += snprintf(result, sizeof(result), "ne"); break; // NE : Z clear
    case 0b0010: result += snprintf(result, sizeof(result), "cs"); break; // CS : C set
    case 0b0011: result += snprintf(result, sizeof(result), "cc"); break; // CC : C clear
    case 0b0100: result += snprintf(result, sizeof(result), "mi"); break; // MI : N set
    case 0b0101: result += snprintf(result, sizeof(result), "pl"); break; // PL : N clear
    case 0b0110: result += snprintf(result, sizeof(result), "vs"); break; // VS : V set
    case 0b0111: result += snprintf(result, sizeof(result), "vc"); break; // VC : V clear
    case 0b1000: result += snprintf(result, sizeof(result), "hi"); break; // HI : C set and Z clear
    case 0b1001: result += snprintf(result, sizeof(result), "ls"); break; // LS : C clear or Z set
    case 0b1010: result += snprintf(result, sizeof(result), "ge"); break; // GE : N == V
    case 0b1011: result += snprintf(result, sizeof(result), "lt"); break; // LT : N != V
    case 0b1100: result += snprintf(result, sizeof(result), "gt"); break; // GT : Z clear and N == V
    case 0b1101: result += snprintf(result, sizeof(result), "le"); break; // LE : Z set or N != V
    case 0b1110:                                                   break; // AL : Always
    case 0b1111: result += snprintf(result, sizeof(result), "nv"); break; // NV : Never
      // clang-format on
  }

  // Some instructions write to the status register
  if (opcode_class == OpcodeClass::DataProcessing ||
      opcode_class == OpcodeClass::Multiply) {
    if (info.word & (1 << 20)) {
      result += snprintf(result, sizeof(result), "s");
    }
  }

  result += snprintf(result, sizeof(result), " ");

  // arguments
  switch (opcode) {
    // clang-format off
    case Opcode::B:
    case Opcode::BL:
      {
        OpcodeBranch op { .raw = word };
        i32 disp = extend_sign<24u>(op.offset << 2);
        const u32 target = (u32)((i32)info.address + disp + 4);
        result += snprintf(result, sizeof(result), "0x%08x", target);
      }
      break;

    case Opcode::LDR:
    case Opcode::STR:
      {
        OpcodeSingleDataTransfer op { .raw = word };
        if (!op.I) {
          u32 offset = extend_sign<12>(op.offset);
          // result += sprintf(result, "r%d, [r%d, #%d]", op.rd, op.rn, op.offset);
        } else {
          // target = info.address + op.offset;
        }
        
      }
      break;
    default:
      break;
    // clang-format on
  }

  #endif

  return buff;
}

} // namespace guest::arm7di
