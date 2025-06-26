#include "shared/bitmanip.h"
#include "guest/r3000/decoder.h"

namespace guest::r3000 {

/* Helper to simplify building flag lists in decode routine */
static bitflags<Decoder::Flag>
operator|(const Decoder::Flag a, const Decoder::Flag b)
{
  return bitflags<Decoder::Flag>(a) | b;
}

Decoder::Decoder(R3000 *const cpu) : m_cpu(cpu)
{
  return;
}

Decoder::~Decoder()
{
  return;
}

Decoder::Info
Decoder::decode(const u32 address)
{
  const u32 fetch = m_cpu->fetch_instruction(address);
  const Instruction instruction(fetch);
  switch (instruction.op) {
    case 0b000000: {
      assert(instruction.is_r_type() && "r3000: decode logic is broken");
      switch (instruction.function) {
        case 0b000000: /* SLL */
          return Decoder::Info { .flags = Flag::SourceT };
          break;

        case 0b000010: /* SRL */
          return Decoder::Info { .flags = Flag::SourceT };
          break;

        case 0b000011: /* SRA */
          return Decoder::Info { .flags = Flag::SourceT };
          break;

        case 0b000100: /* SLLV */
          return Decoder::Info { .flags = Flag::SourceS | Flag::SourceT };
          break;

        case 0b000110: /* SRLV */
          return Decoder::Info { .flags = Flag::SourceS | Flag::SourceT };
          break;

        case 0b000111: /* SRAV */
          return Decoder::Info { .flags = Flag::SourceS | Flag::SourceT };
          break;

        case 0b001000: /* JR */
          return Decoder::Info { .flags =
                                   Flag::Branch | Flag::HasDelaySlot | Flag::SourceS };
          break;

        case 0b001001: /* JALR */
          return Decoder::Info { .flags =
                                   Flag::Branch | Flag::HasDelaySlot | Flag::SourceS };
          break;

        case 0b001100: /* SYSCALL */
          return Decoder::Info { .flags = Flag::Branch | Flag::Exception };
          break;

        case 0b001101: /* BREAK */
          return Decoder::Info { .flags = Flag::Branch | Flag::Exception };
          break;

        case 0b010000: /* MFHI */
          return Decoder::Info { .flags = bitflags<Flag>() };
          break;

        case 0b010001: /* MTHI */
          return Decoder::Info { .flags = Flag::SourceS };
          break;

        case 0b010010: /* MFLO */
          return Decoder::Info { .flags = bitflags<Flag>() };
          break;

        case 0b010011: /* MTLO */
          return Decoder::Info { .flags = Flag::SourceS };
          break;

        case 0b011000: /* MULT */
          return Decoder::Info { .flags = Flag::SourceS | Flag::SourceT };
          break;

        case 0b011001: /* MULTU */
          return Decoder::Info { .flags = Flag::SourceS | Flag::SourceT };
          break;

        case 0b011010: /* DIV */
          return Decoder::Info { .flags = Flag::SourceS | Flag::SourceT };
          break;

        case 0b011011: /* DIVU */
          return Decoder::Info { .flags = Flag::SourceS | Flag::SourceT };
          break;

        case 0b100000: /* ADD */
          return Decoder::Info { .flags =
                                   Flag::Exception | Flag::SourceS | Flag::SourceT };
          break;

        case 0b100001: /* ADDU */
          return Decoder::Info { .flags =
                                   Flag::Exception | Flag::SourceS | Flag::SourceT };
          break;

        case 0b100010: /* SUB */
          return Decoder::Info { .flags =
                                   Flag::Exception | Flag::SourceS | Flag::SourceT };
          break;

        case 0b100011: /* SUBU */
          return Decoder::Info { .flags = Flag::SourceS | Flag::SourceT };
          break;

        case 0b100100: /* AND */
          return Decoder::Info { .flags = Flag::SourceS | Flag::SourceT };
          break;

        case 0b100101: /* OR */
          return Decoder::Info { .flags = Flag::SourceS | Flag::SourceT };
          break;

        case 0b100110: /* XOR */
          return Decoder::Info { .flags = Flag::SourceS | Flag::SourceT };
          break;

        case 0b100111: /* NOR */
          return Decoder::Info { .flags = Flag::SourceS | Flag::SourceT };
          break;

        case 0b101010: /* SLT */
          return Decoder::Info { .flags = Flag::SourceS | Flag::SourceT };
          break;

        case 0b101011: /* SLTU */
          return Decoder::Info { .flags = Flag::SourceS | Flag::SourceT };
          break;

        default:
          printf("Unimplemented instruction is 0x%08x\n", fetch);
          assert(false && "Unimplemented instruction in decoder");
          break;
      }
      break;
    }

    case 0b000001: /* Bxx */
      return Decoder::Info { .flags = Flag::Branch | Flag::Conditional |
                                      Flag::HasDelaySlot | Flag::SourceS };
      break;

    case 0b000010: /* J */
      return Decoder::Info { .flags = Flag::Branch | Flag::HasDelaySlot };
      break;

    case 0b000011: /* JAL */
      return Decoder::Info { .flags = Flag::Branch | Flag::HasDelaySlot };
      break;

    case 0b000100: /* BEQ */
      return Decoder::Info { .flags = Flag::Branch | Flag::HasDelaySlot |
                                      Flag::Conditional | Flag::SourceS | Flag::SourceT };
      break;

    case 0b000101: /* BNE */
      return Decoder::Info { .flags = Flag::Branch | Flag::HasDelaySlot |
                                      Flag::Conditional | Flag::SourceS | Flag::SourceT };
      break;

    case 0b000110: /* BLEZ */
      return Decoder::Info { .flags = Flag::Branch | Flag::HasDelaySlot |
                                      Flag::Conditional | Flag::SourceS };
      break;

    case 0b000111: /* BGTZ */
      return Decoder::Info { .flags = Flag::Branch | Flag::HasDelaySlot |
                                      Flag::Conditional | Flag::SourceS };
      break;

    case 0b001000: /* ADDI */
      return Decoder::Info { .flags = Flag::Exception | Flag::SourceS };
      break;

    case 0b001001: /* ADDIU */
      return Decoder::Info { .flags = Flag::SourceS };
      break;

    case 0b001010: /* SLTI */
      return Decoder::Info { .flags = Flag::SourceS };
      break;

    case 0b001011: /* SLTIU */
      return Decoder::Info { .flags = Flag::SourceS };
      break;

    case 0b001100: /* ANDI */
      return Decoder::Info { .flags = Flag::SourceS };
      break;

    case 0b001101: /* ORI */
      return Decoder::Info { .flags = Flag::SourceS };
      break;

    case 0b001110: /* XORI */
      return Decoder::Info { .flags = Flag::SourceS };
      break;

    case 0b001111: /* LUI */
      return Decoder::Info { .flags = bitflags<Flag>() };
      break;

    case 0b010000:
      [[fallthrough]];
    case 0b010001:
      [[fallthrough]];
    case 0b010010:
      [[fallthrough]];
    case 0b010011: /* COP, CFC, CTC, MFC, MTC, illegal */
      /* TODO SourceT only used for op_mtc */
      return Decoder::Info { .flags = Flag::Exception | Flag::SourceT };
      break;

    case 0b100000: /* LB */
      return Decoder::Info { .flags = Flag::MemoryLoad | Flag::SourceS };
      break;

    case 0b100001: /* LH */
      return Decoder::Info { .flags =
                               Flag::MemoryLoad | Flag::Exception | Flag::SourceS };
      break;

    case 0b100010: /* LWL */
      return Decoder::Info { .flags = Flag::MemoryLoad | Flag::SourceS | Flag::SourceT |
                                      Flag::NoForwardDelay };
      break;

    case 0b100011: /* LW */
      return Decoder::Info { .flags =
                               Flag::MemoryLoad | Flag::Exception | Flag::SourceS };
      break;

    case 0b100100: /* LBU */
      return Decoder::Info { .flags = Flag::MemoryLoad | Flag::SourceS };
      break;

    case 0b100101: /* LHU */
      return Decoder::Info { .flags =
                               Flag::MemoryLoad | Flag::Exception | Flag::SourceS };
      break;

    case 0b100110: /* LWR */
      return Decoder::Info { .flags = Flag::MemoryLoad | Flag::SourceS | Flag::SourceT |
                                      Flag::NoForwardDelay };
      break;

    case 0b101000: /* SB */
      return Decoder::Info { .flags = Flag::MemoryStore | Flag::SourceS | Flag::SourceT };
      break;

    case 0b101001: /* SH */
      return Decoder::Info { .flags = Flag::MemoryStore | Flag::Exception |
                                      Flag::SourceS | Flag::SourceT };
      break;

    case 0b101010: /* SWL */
      return Decoder::Info { .flags = Flag::MemoryStore | Flag::MemoryLoad |
                                      Flag::SourceS | Flag::SourceT };
      break;

    case 0b101011: /* SW */
      return Decoder::Info { .flags = Flag::MemoryStore | Flag::Exception |
                                      Flag::SourceS | Flag::SourceT };
      break;

    case 0b101110: /* SWR */
      return Decoder::Info { .flags = Flag::MemoryStore | Flag::MemoryLoad |
                                      Flag::SourceS | Flag::SourceT };
      break;

    case 0b101111: /* SUBIU */
      return Decoder::Info { .flags = Flag::SourceS };
      break;

    case 0b110000: /* LWC0 */
      assert(false && "Unimplemented instruction in decoder");
      break;

    case 0b110001: /* LWC1 */
      assert(false && "Unimplemented instruction in decoder");
      break;

    case 0b110010: /* LWC2 */
      return Decoder::Info { .flags = Flag::MemoryLoad | Flag::SourceS | Flag::SourceT };
      break;

    case 0b110011: /* LWC3 */
      assert(false && "Unimplemented instruction in decoder");
      break;

    case 0b111000: /* SWC0 */
      assert(false && "Unimplemented instruction in decoder");
      break;

    case 0b111001: /* SWC1 */
      assert(false && "Unimplemented instruction in decoder");
      break;

    case 0b111010: /* SWC2 */
      return Decoder::Info { .flags = Flag::MemoryStore | Flag::SourceS | Flag::SourceT };
      break;

    case 0b111011: /* SWC3 */
      assert(false && "Unimplemented instruction in decoder");
      break;

    default:
      printf("Unimplemented instruction in decoder 0x%08x\n", fetch);
      assert(false && "Unimplemented instruction in decoder");
      break;
  }

  throw std::runtime_error("Unhandled instruction");
}

bool
Instruction::is_i_type() const
{
  return (op == 1) || (op >= 4 && op <= 15) || (op >= 32);
}

bool
Instruction::is_j_type() const
{
  return op >= 2 && op <= 3;
}

bool
Instruction::is_r_type() const
{
  return op == 0;
}

u32
Instruction::imm_se() const
{
  return extend_sign<16>(imm);
}

}
