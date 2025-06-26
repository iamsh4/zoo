#include "guest/r3000/r3000_disas.h"
#include "shared/bitmanip.h"

namespace guest ::r3000 {

std::pair<std::string, std::string>
Disassembler::disassemble(const u32 pc, const Instruction ins)
{
  char buffer[256];
  char description[256];

  // TODO : Replace this once all are implemented
  snprintf(buffer,
           sizeof(buffer),
           "??? (op=%u, function=%u, raw=0x%08x)",
           ins.op,
           ins.function,
           ins.raw);

  snprintf(description,
           sizeof(description),
           "??? (op=%u, function=%u, raw=0x%08x)",
           ins.op,
           ins.function,
           ins.raw);

  switch (ins.op) {
    case 0b000000: {
      assert(ins.is_r_type() && "r3000: decode logic is broken");
      switch (ins.function) {
        case 0b000000:
          if (ins.raw == 0) {
            snprintf(buffer, sizeof(buffer), "nop");
            snprintf(description, sizeof(description), "Do nothing.");
          } else {
            snprintf(
              buffer, sizeof(buffer), "sll %s, %s, %u", r(ins.rd), r(ins.rt), ins.shamt);
          }
          break;
        case 0b000010:
          // op_srl(ins);
          break;
        case 0b000011:
          // op_sra(ins);
          break;
        case 0b000100:
          // op_sllv(ins);
          break;
        case 0b000110: {
          snprintf(
            buffer, sizeof(buffer), "srlv %s, %s, %s", r(ins.rd), r(ins.rt), r(ins.rs));
          break;
        }
        case 0b000111:
          // op_srav(ins);
          break;
        case 0b001000:
          snprintf(buffer, sizeof(buffer), "jr %s", r(ins.rs));
          snprintf(description, sizeof(description), "Jump to address in %s", r(ins.rs));
          break;
        case 0b001001:
          // op_jalr(ins);
          break;
        case 0b001100:
          // op_syscall(ins);
          break;
        case 0b001101:
          // op_break(ins);
          break;
        case 0b010000:
          // snprintf(buffer, sizeof(buffer), "mfhi %s", r(ins.rd));
          // snprintf(description, sizeof(description), "%s <- HI", r(ins.rd));
          break;
        case 0b010001:
          // op_mthi(ins);
          break;
        case 0b010010:
          // op_mflo(ins);
          break;
        case 0b010011:
          // op_mtlo(ins);
          break;
        case 0b011000:
          // op_mult(ins);
          break;
        case 0b011001:
          // op_multu(ins);
          break;
        case 0b011010:
          // op_div(ins);
          break;
        case 0b011011:
          // op_divu(ins);
          break;
        case 0b100000:
          snprintf(
            buffer, sizeof(buffer), "add %s, %s, %s", r(ins.rd), r(ins.rs), r(ins.rt));
          snprintf(description,
                   sizeof(description),
                   "%s <- %s + %s (possibly raises overflow exception)",
                   r(ins.rd),
                   r(ins.rs),
                   r(ins.rt));
          break;
        case 0b100001:
          snprintf(
            buffer, sizeof(buffer), "addu %s, %s, %s", r(ins.rd), r(ins.rs), r(ins.rt));
          break;
        case 0b100010:
          // op_sub(ins);
          break;
        case 0b100011:
          // op_subu(ins);
          break;
        case 0b100100: {
          snprintf(
            buffer, sizeof(buffer), "and %s, %s, %s", r(ins.rd), r(ins.rs), r(ins.rt));
          break;
        }
        case 0b100101:
          snprintf(
            buffer, sizeof(buffer), "or %s, %s, %s", r(ins.rd), r(ins.rs), r(ins.rt));
          if (ins.rs == 0 && ins.rt == 0) {
            // This is a common encoding for `mov rd, 0`
            snprintf(description, sizeof(description), "%s <- 0", r(ins.rd));
          } else {
            snprintf(description,
                     sizeof(description),
                     "%s <- %s | %s",
                     r(ins.rd),
                     r(ins.rs),
                     r(ins.rt));
          }
          break;
        case 0b100110:
          // op_xor(ins);
          break;
        case 0b100111:
          // op_nor(ins);
          break;
        case 0b101010:
          snprintf(
            buffer, sizeof(buffer), "slt %s, %s, %s", r(ins.rd), r(ins.rs), r(ins.rt));
          break;
        case 0b101011:
          // op_sltu(ins);
          break;
        default:
          // op_illegal(ins);
          break;
      }
      break;
    }
    case 0b000001:
      // op_bxx(ins);
      break;
    case 0b000010:
      snprintf(buffer, sizeof(buffer), "j ->0x_%07x", (ins.target << 2) & 0x0FFFFFFF);
      snprintf(description,
               sizeof(description),
               "Delay slot executes, then jump to 0x?%07x.",
               (ins.target << 2) & 0x0FFFFFFF);
      // op_j(ins);
      break;
    case 0b000011:
      snprintf(buffer, sizeof(buffer), "jal ->0x%08x", (ins.target << 2) & 0x0FFFFFFF);
      snprintf(description,
               sizeof(description),
               "Jump to 0x?%07x. Store address after delay slot to ra.",
               (ins.target << 2) & 0x0FFFFFFF);
      break;
    case 0b000100: {
      const i32 offset = extend_sign<16>(ins.imm) << 2u;
      const u32 target = pc + offset + 4;
      snprintf(
        buffer, sizeof(buffer), "beq %s, %s, ->0x%08x", r(ins.rs), r(ins.rt), target);
      break;
    }
    case 0b000101: {
      const i32 offset = extend_sign<16>(ins.imm) << 2u;
      const u32 target = pc + offset + 4;
      snprintf(
        buffer, sizeof(buffer), "bne %s, %s, ->0x%08x", r(ins.rs), r(ins.rt), target);
      snprintf(description,
               sizeof(description),
               "If (%s != %s) then execute delayed branch to 0x%08x",
               r(ins.rs),
               r(ins.rt),
               target);
      break;
    }
    case 0b000110: {
      const i32 offset = extend_sign<16>(ins.imm) << 2u;
      const u32 target = pc + offset + 4;
      snprintf(buffer, sizeof(buffer), "blez %s, ->0x%08x", r(ins.rs), target);
      break;
    }
    case 0b000111: {
      const i32 offset = extend_sign<16>(ins.imm) << 2u;
      const u32 target = pc + offset + 4;
      snprintf(buffer, sizeof(buffer), "bgtz %s, ->0x%08x", r(ins.rs), target);
      break;
    }
    case 0b001000:
      snprintf(buffer,
               sizeof(buffer),
               "addi %s, %s, 0x%x",
               r(ins.rt),
               r(ins.rs),
               reinterpret<i32>(ins.imm));
      snprintf(description,
               sizeof(description),
               "%s <- %s + 0x%x (exception on overflow)",
               r(ins.rt),
               r(ins.rs),
               ins.imm);
      break;
    case 0b001001:
      snprintf(buffer,
               sizeof(buffer),
               "addiu %s, %s, 0x%x",
               r(ins.rt),
               r(ins.rs),
               reinterpret<i32>(ins.imm));
      snprintf(description,
               sizeof(description),
               "%s <- %s + 0x%x",
               r(ins.rt),
               r(ins.rs),
               ins.imm);
      // op_addiu(ins);
      break;
    case 0b001010:
      snprintf(
        buffer, sizeof(buffer), "slti %s, %s, 0x%x", r(ins.rt), r(ins.rs), ins.imm);
      break;
    case 0b001011:
      // op_sltiu(ins);
      break;
    case 0b001100:
      snprintf(buffer, sizeof(buffer), "andi %s, %s, 0x%x", r(ins.rt), r(ins.rs), ins.imm);
      break;
    case 0b001101:
      snprintf(buffer, sizeof(buffer), "ori %s, %s, 0x%x", r(ins.rt), r(ins.rs), ins.imm);
      snprintf(description,
               sizeof(description),
               "%s <- %s | 0x%xu",
               r(ins.rt),
               r(ins.rs),
               ins.imm);
      break;
    case 0b001110:
      // op_xori(ins);
      break;
    case 0b001111:
      snprintf(buffer, sizeof(buffer), "lui %s, 0x%x", r(ins.rt), ins.imm);
      snprintf(description,
               sizeof(description),
               "Load upper 16bits of %s with 0x%x",
               r(ins.rt),
               ins.imm);
      break;
    case 0b010000: // fall through
    case 0b010001: // fall through
    case 0b010010: // fall through
    case 0b010011: {
      const u32 z = extract_bits(ins.raw, 27, 26);
      const u32 cop_func = ins.rs;

      if (cop_func == 0b100) {
        snprintf(buffer, sizeof(buffer), "mtc%u %s, r%x", z, r(ins.rt), ins.rd);
        snprintf(
          description, sizeof(description), "%s -> cop%u.r%u", r(ins.rt), z, ins.rd);
      }
      if (cop_func == 0b110) {
        snprintf(buffer, sizeof(buffer), "ctc%u %s, r%x", z, r(ins.rt), ins.rd);
        snprintf(description,
                 sizeof(description),
                 "%s -> cop%u.ctrl%u (aka cop%u.r%u)",
                 r(ins.rt),
                 z,
                 ins.rd,
                 z,
                 ins.rd + 32);
      }

      // op_cop_ins(ins);
      break;
    }
    case 0b100000:
      snprintf(
        buffer, sizeof(buffer), "lb %s, 0x%x(%s)", r(ins.rt), ins.imm_se(), r(ins.rs));
      snprintf(description,
               sizeof(description),
               "Load %s <- sign extended i8 @(%s + 0x%x).",
               r(ins.rt),
               r(ins.rs),
               ins.imm_se());
      break;
    case 0b100001:
      snprintf(
        buffer, sizeof(buffer), "lh %s, 0x%x(%s)", r(ins.rt), ins.imm_se(), r(ins.rs));
      break;
    case 0b100010:
      snprintf(
        buffer, sizeof(buffer), "lwl %s, 0x%x(%s)", r(ins.rt), ins.imm_se(), r(ins.rs));
      break;
    case 0b100011:
      snprintf(
        buffer, sizeof(buffer), "lw %s, 0x%x(%s)", r(ins.rt), ins.imm_se(), r(ins.rs));
      snprintf(description,
               sizeof(description),
               "Load %s <- @(%s + 0x%x).",
               r(ins.rt),
               r(ins.rs),
               ins.imm_se());
      break;
    case 0b100100: {
      snprintf(
        buffer, sizeof(buffer), "lbu %s, 0x%x(%s)", r(ins.rt), ins.imm_se(), r(ins.rs));
      break;
    }
    case 0b100101:
      snprintf(
        buffer, sizeof(buffer), "lhu %s, 0x%x(%s)", r(ins.rt), ins.imm_se(), r(ins.rs));
      break;
    case 0b100110:
      snprintf(
        buffer, sizeof(buffer), "lwr %s, 0x%x(%s)", r(ins.rt), ins.imm_se(), r(ins.rs));
      break;
    case 0b101000: {
      snprintf(
        buffer, sizeof(buffer), "sb %s, 0x%x(%s)", r(ins.rt), ins.imm_se(), r(ins.rs));
      break;
    }
    case 0b101001:
      snprintf(
        buffer, sizeof(buffer), "sh %s, 0x%x(%s)", r(ins.rt), ins.imm_se(), r(ins.rs));
      break;
    case 0b101010:
      snprintf(
        buffer, sizeof(buffer), "swl %s, 0x%x(%s)", r(ins.rt), ins.imm_se(), r(ins.rs));
      break;
    case 0b101011:
      snprintf(buffer,
               sizeof(buffer),
               "sw %s, 0x%x(%s)",
               r(ins.rt),
               extend_sign<16>(ins.imm),
               r(ins.rs));
      snprintf(description,
               sizeof(description),
               "Store %s -> @(%s + 0x%x).",
               r(ins.rt),
               r(ins.rs),
               extend_sign<16>(ins.imm));
      break;
    case 0b101110:
      snprintf(
        buffer, sizeof(buffer), "swr %s, 0x%x(%s)", r(ins.rt), ins.imm_se(), r(ins.rs));
      break;
    case 0b110000:
      // op_lwc0(ins);
      break;
    case 0b110001:
      // op_lwc1(ins);
      break;
    case 0b110010:
      // op_lwc2(ins);
      break;
    case 0b110011:
      // op_lwc3(ins);
      break;
    case 0b111000:
      // op_swc0(ins);
      break;
    case 0b111001:
      // op_swc1(ins);
      break;
    case 0b111010:
      // op_swc2(ins);
      break;
    case 0b111011:
      // op_swc3(ins);
      break;
    default:
      snprintf(buffer, sizeof(buffer), "...");
      break;
  }

  return std::make_pair(buffer, description);
}

const char *
Disassembler::r(const unsigned index)
{
  return R3000::get_register_name(index, true);
}

} // namespace guest:r3000
