#include <exception>
#include <fmt/core.h>

#include "guest/r3000/r3000.h"
#include "guest/r3000/r3000_disas.h"
#include "guest/r3000/r3000_ir.h"
#include "guest/r3000/decoder.h"
#include "shared/bitmanip.h"
#include "shared/profiling.h"

namespace guest::r3000 {

/* Local aliases / helpers */
namespace {

using Operand = fox::ir::Operand;

fox::ir::Operand
const_u32(const u32 value)
{
  return Operand::constant<u32>(value);
}

fox::ir::Operand
const_u16(const u16 value)
{
  return Operand::constant<u16>(value);
}

fox::ir::Operand
const_bool(const bool value)
{
  return fox::ir::Operand::constant<bool>(value);
}

}

void
Assembler::set_coprocessor_assembler(u32 cop_num, Coprocessor *cop)
{
  m_cop_handlers[cop_num] = cop;
}

fox::ir::ExecutionUnit &&
Assembler::assemble(R3000 *const cpu, const u32 pc, const u32 limit)
{
  ProfileZone;
  Decoder decoder(cpu);

  /* TODO Remove the cpu parameter and move fetching / block generation to
   *      a separate routine. */

  /* Re-initialize internal state */
  m_pc = pc;
  m_branch_executed = false;
  m_branch_delayed = false;
  m_writeback_value = Operand();

  /* Cache the current copy of the delayed branch virtual register. */
  const Operand delayed_branch = read_reg(Registers::BRANCH_DELAY_ADDRESS);

  /* Fetch and decode basic instruction details. */
  const u32 instruction_word = cpu->fetch_instruction(m_pc);
  const Instruction instruction(instruction_word);
  const Decoder::Info info = decoder.decode(m_pc);

  /* Load register inputs now, before retiring delayed writebacks from a prior
   * instruction. */
  if (!(info.flags & Decoder::Flag::NoForwardDelay)) {
    m_Rs = (info.flags & Decoder::Flag::SourceS) ? read_reg(instruction.rs) : Operand();
    m_Rt = (info.flags & Decoder::Flag::SourceT) ? read_reg(instruction.rt) : Operand();
  }

  /* Flush any delayed register writes. This must happen after loading inputs
   * for the following (current) instruction but before the results are
   * written. */
  /* TODO Once branching is supported in IR, do this in IR. */
  flush();
  invalidate();
  call([](fox::Guest *const guest) {
    R3000 *const cpu = reinterpret_cast<R3000 *>(guest);
    const u32 index = cpu->m_regs[Registers::DELAYED_WRITEBACK_REG_INDEX];
    if (index != INVALID_WRITEBACK_INDEX) {
      cpu->m_regs[index] = cpu->m_regs[Registers::DELAYED_WRITEBACK_REG_VALUE];
      cpu->m_regs[Registers::DELAYED_WRITEBACK_REG_INDEX] = INVALID_WRITEBACK_INDEX;
      cpu->m_regs[Registers::DELAYED_WRITEBACK_REG_VALUE] = 0;
    }
  });

  if (info.flags & Decoder::Flag::NoForwardDelay) {
    m_Rs = (info.flags & Decoder::Flag::SourceS) ? read_reg(instruction.rs) : Operand();
    m_Rt = (info.flags & Decoder::Flag::SourceT) ? read_reg(instruction.rt) : Operand();
  }

  /* Decode instruction and generate its IR. */
  // const auto &[disassembly, description] = Disassembler().disassemble(m_pc,
  // instruction); const std::string disassembly = ""; const std::string description = "";
  // printf(" pc=0x%08x ... %-30s | %s ", m_pc, disassembly.c_str(), description.c_str());

  decode_instruction(instruction);

  if (m_branch_executed) {
    if (m_branch_delayed) {
      /* XXX What is the behavior of a branch inside a delay slot? For now,
       *     just assert it doesn't happen. This won't work for blocks. */
      // assert();
      if (cpu->m_regs[Registers::BRANCH_DELAY_ADDRESS] != INVALID_BRANCH_DELAY_ADDRESS) {
        throw std::runtime_error("branch in the branch");
      }

      /* The instruction branches but has a delay slot. PC must be moved forward
       * 4 bytes to execute the delay slot unconditionally. The branched PC will
       * be written if necessary by the next step. */
      write_reg(Registers::PC, const_u32(m_pc + 4u));
    } else {
      /* XXX What is the behavior of a branch inside a delay slot? For now,
       *     just assert it doesn't happen. This won't work for blocks. */
      assert(cpu->m_regs[Registers::BRANCH_DELAY_ADDRESS] ==
             INVALID_BRANCH_DELAY_ADDRESS);

      /* The instruction branches and has no delay slot. PC was already updated,
       * so this is a no-op. */
    }
  } else {
    /* No branch was executed. Move PC forward or handle delayed branch. */
    const Operand no_branch =
      cmp_eq(delayed_branch, const_u32(INVALID_BRANCH_DELAY_ADDRESS));
    write_reg(Registers::PC, select(no_branch, delayed_branch, const_u32(m_pc + 4u)));
    write_reg(Registers::BRANCH_DELAY_ADDRESS, const_u32(INVALID_BRANCH_DELAY_ADDRESS));
  }

  if (m_writeback_value.is_valid()) {
    write_reg(Registers::DELAYED_WRITEBACK_REG_INDEX, const_u32(m_writeback_index));
    write_reg(Registers::DELAYED_WRITEBACK_REG_VALUE, m_writeback_value);
  }

  flush();

  /* TODO Return actual cycle count. */
  exit(const_bool(true), Operand::constant<u64>(1));

  return export_unit();
}

void
Assembler::flush()
{
  for (unsigned i = 0; i < Registers::NUM_REGS; ++i) {
    if (!m_registers[i].dirty) {
      continue;
    }

    assert(m_registers[i].valid);

    const Operand index = const_u16(i);
    writegr(index, m_registers[i].value);
    m_registers[i].dirty = false;
  }
}

void
Assembler::flush(const unsigned index)
{
  if (!m_registers[index].dirty) {
    return;
  }

  assert(index < Registers::NUM_REGS);
  assert(m_registers[index].valid);

  writegr(const_u16(index), m_registers[index].value);
  m_registers[index].dirty = false;
}

void
Assembler::invalidate()
{
  for (unsigned i = 0; i < Registers::NUM_REGS; ++i) {
    assert(!m_registers[i].dirty);
    m_registers[i].value = Operand();
    m_registers[i].valid = false;
  }
}

void
Assembler::invalidate(const unsigned index, const bool allow_dirty)
{
  assert(!m_registers[index].dirty || allow_dirty);
  m_registers[index].value = Operand();
  m_registers[index].valid = false;
  m_registers[index].dirty = false;
}

unsigned
get_coprocessor_index(const Instruction ins)
{
  return extract_bits(ins.raw, 27, 26);
}

void
Assembler::decode_instruction(const Instruction ins)
{
  /* clang-format off */
  switch (ins.op) {
    case 0b000000: {
      assert(ins.is_r_type() && "r3000: decode logic is broken");
      switch (ins.function) {
        case 0b000000: op_sll(ins);     break;
        case 0b000010: op_srl(ins);     break;
        case 0b000011: op_sra(ins);     break;
        case 0b000100: op_sllv(ins);    break;
        case 0b000110: op_srlv(ins);    break;
        case 0b000111: op_srav(ins);    break;
        case 0b001000: op_jr(ins);      break;
        case 0b001001: op_jalr(ins);    break;
        case 0b001100: op_syscall(ins); break;
        case 0b001101: op_break(ins);   break;
        case 0b010000: op_mfhi(ins);    break;
        case 0b010001: op_mthi(ins);    break;
        case 0b010010: op_mflo(ins);    break;
        case 0b010011: op_mtlo(ins);    break;
        case 0b011000: op_mult(ins);    break;
        case 0b011001: op_multu(ins);   break;
        case 0b011010: op_div(ins);     break;
        case 0b011011: op_divu(ins);    break;
        case 0b100000: op_add(ins);     break;
        case 0b100001: op_addu(ins);    break;
        case 0b100010: op_sub(ins);     break;
        case 0b100011: op_subu(ins);    break;
        case 0b100100: op_and(ins);     break;
        case 0b100101: op_or(ins);      break;
        case 0b100110: op_xor(ins);     break;
        case 0b100111: op_nor(ins);     break;
        case 0b101010: op_slt(ins);     break;
        case 0b101011: op_sltu(ins);    break;
        default:       op_illegal(ins); break;
      }
      break;
    }
    case 0b000001: op_bxx(ins);     break;
    case 0b000010: op_j(ins);       break;
    case 0b000011: op_jal(ins);     break;
    case 0b000100: op_beq(ins);     break;
    case 0b000101: op_bne(ins);     break;
    case 0b000110: op_blez(ins);    break;
    case 0b000111: op_bgtz(ins);    break;
    case 0b001000: op_addi(ins);    break;
    case 0b001001: op_addiu(ins);   break;
    case 0b001010: op_slti(ins);    break;
    case 0b001011: op_sltiu(ins);   break;
    case 0b001100: op_andi(ins);    break;
    case 0b001101: op_ori(ins);     break;
    case 0b001110: op_xori(ins);    break;
    case 0b001111: op_lui(ins);     break;
    case 0b010000: [[fallthrough]];
    case 0b010001: [[fallthrough]];
    case 0b010010: [[fallthrough]];
    case 0b010011: op_cop_ins(ins); break;
    case 0b100000: op_lb(ins);      break;
    case 0b100001: op_lh(ins);      break;
    case 0b100010: op_lwl(ins);     break;
    case 0b100011: op_lw(ins);      break;
    case 0b100100: op_lbu(ins);     break;
    case 0b100101: op_lhu(ins);     break;
    case 0b100110: op_lwr(ins);     break;
    case 0b101000: op_sb(ins);      break;
    case 0b101001: op_sh(ins);      break;
    case 0b101010: op_swl(ins);     break;
    case 0b101011: op_sw(ins);      break;
    case 0b101110: op_swr(ins);     break;
    case 0b101111: op_subiu(ins);   break;
    case 0b110000: op_lwc0(ins);    break;
    case 0b110001: op_lwc1(ins);    break;
    case 0b110010: op_lwc2(ins);    break;
    case 0b110011: op_lwc3(ins);    break;
    case 0b111000: op_swc0(ins);    break;
    case 0b111001: op_swc1(ins);    break;
    case 0b111010: op_swc2(ins);    break;
    case 0b111011: op_swc3(ins);    break;
    default:       op_illegal(ins); break;
  }
  /* clang-format on */
}

static void
__unimplemented(const Instruction ins)
{
  const u32 cop_index = get_coprocessor_index(ins);

  printf("r3000: Unimplemented opcode, op=%u function=%u raw=0x%08x rs=%u cop=%u\n",
         ins.op,
         ins.function,
         ins.raw,
         ins.rs,
         cop_index);
  throw std::runtime_error("r3000: Unimplemented opcode");
}

void
Assembler::op_add(const Instruction ins)
{
  Operand sum, overflow;
  add_with_overflow(m_Rs, m_Rt, &sum, &overflow);
  exception_on_overflow(overflow);

  // "The destination register rt is not modified when an integer overflow exception
  // occurs."
  write_reg(ins.rd, sum);
}

void
Assembler::op_addi(const Instruction ins)
{
  Operand sum, overflow;
  add_with_overflow(m_Rs, const_u32(ins.imm_se()), &sum, &overflow);
  exception_on_overflow(overflow);

  // "The destination register rt is not modified when an integer overflow exception
  // occurs."
  write_reg(ins.rt, sum);
}

void
Assembler::op_addiu(const Instruction ins)
{
  write_reg(ins.rt, add(m_Rs, const_u32(ins.imm_se())));
}

void
Assembler::op_addu(const Instruction ins)
{
  write_reg(ins.rd, add(m_Rs, m_Rt));
}

void
Assembler::op_and(const Instruction ins)
{
  write_reg(ins.rd, _and(m_Rs, m_Rt));
}

void
Assembler::op_andi(const Instruction ins)
{
  write_reg(ins.rt, _and(const_u32(ins.imm), m_Rs));
}

void
Assembler::op_beq(const Instruction ins)
{
  /* Note: Jump is relative to delay slot address, so add 4 to PC first. */
  const u32 target = m_pc + 4 + (ins.imm_se() << 2u);
  jmp_delay(const_u32(target), cmp_eq(m_Rs, m_Rt));
}

void
Assembler::op_bgtz(const Instruction ins)
{
  /* Note: Jump is relative to delay slot address, so add 4 to PC first. */
  const u32 target = m_pc + 4 + (ins.imm_se() << 2u);
  jmp_delay(const_u32(target), cmp_gt(m_Rs, const_u32(0)));
}

void
Assembler::op_blez(const Instruction ins)
{
  /* Note: Jump is relative to delay slot address, so add 4 to PC first. */
  const u32 target = m_pc + 4 + (ins.imm_se() << 2u);
  jmp_delay(const_u32(target), cmp_lte(m_Rs, const_u32(0)));
}

void
Assembler::op_bne(const Instruction ins)
{
  /* Note: Jump is relative to delay slot address, so add 4 to PC first. */
  const u32 target = m_pc + 4 + (ins.imm_se() << 2u);
  jmp_delay(const_u32(target), _not(cmp_eq(m_Rs, m_Rt)));
}

void
Assembler::op_break(const Instruction ins)
{
  exception(Exceptions::Breakpoint);
}

void
Assembler::op_bxx(const Instruction ins)
{
  /* This encompasses four similar instructions: BGEZ, BLTZ, BGEZAL, BLTZAL. */

  /* XXX Bits 17, 18, 19 must be 0 or this is an invalid opcode. */
  const bool is_bgez = (ins.raw >> 16) & 1;
  const bool is_link = (ins.raw >> 20) & 1;
  const Operand zero = const_u32(0);

  /* Note: The link register is updated even if the test fails. */
  if (is_link) {
    write_reg(Registers::RA, const_u32(m_pc + 8));
  }

  /* Note: Jump is relative to delay slot address, so add 4 to PC first. */
  const u32 target = m_pc + 4 + (ins.imm_se() << 2u);
  const Operand condition = is_bgez ? cmp_gte(m_Rs, zero) : cmp_lt(m_Rs, zero);
  jmp_delay(const_u32(target), condition);
}

void
Assembler::op_cfc(const Instruction ins)
{
  // __unimplemented(ins);
  const u32 cop_index = get_coprocessor_index(ins);
  throw_if_coprocessor_not_present(cop_index);

  Operand value;
  if (cop_index == 0) {
    throw std::runtime_error("r3000: invalid");
  } else {
    const u32 reg_index =
      Registers::COP0_CTRL + Registers::NUM_REGS_PER_COP * cop_index + ins.rd;
    const Operand value = read_reg(reg_index);

    call(
      fox::ir::Type::Integer32,
      [](fox::Guest *guest, fox::Value reg_index, fox::Value read_value) {
        // const u16 cop_index =
        //   (reg_index.u16_value - Registers::COP0_DATA) / Registers::NUM_REGS_PER_COP;
        // const u16 cop_reg =
        //   (reg_index.u16_value - Registers::COP0_DATA) % Registers::NUM_REGS_PER_COP;
        // printf("CFC: cop%u.r%02u -> 0x%08x\n", cop_index, cop_reg, read_value.u32_value);
        return fox::Value { .u32_value = 0 };
      },
      const_u32(reg_index),
      value);

    write_reg(ins.rt, value);
  }
}

void
Assembler::op_cop(const Instruction ins)
{
  const u32 cop_index = get_coprocessor_index(ins);
  throw_if_coprocessor_not_present(cop_index);

  if (cop_index == 2) {
    u32 cofun = extract_bits(ins.raw, 24, 0);

    flush();
    m_cop_handlers[cop_index]->handle_cop_ir(cofun);
    return;
  }

  // {
  //   Operand sr = read_reg(Registers::SR);
  //   const Operand mode = _and(sr, const_u32(0x3f));
  //   sr = _and(sr, const_u32(~0xfu));
  //   sr = _or(sr, shiftr(mode, const_u32(2)));
  //   write_reg(Registers::SR, sr);
  // }

  if (cop_index == 0) {
    // RFE
    if (extract_bits(ins.raw, 21, 6) == 0 && ins.function == 16) {
      const Operand sr = read_reg(Registers::SR);
      const Operand left = _and(sr, const_u32(~0xfu));
      const Operand right = _and(shiftr(sr, const_u32(2)), const_u32(0xfu));
      const Operand new_sr = _or(left, right);
      write_reg(Registers::SR, new_sr);
    } else {
      __unimplemented(ins);
    }
  }
}

void
Assembler::op_cop_ins(const Instruction ins)
{
  // const u32 cop_type_bits = 0xf000'0000;
  // const u32 cop_num_bits = 0x0c00'0000;
  const u32 cop_co_bit = 0x0200'0000;

  // COP is a little bit funky. just one bit of 'rs' section needs to be set
  if ((cop_co_bit & ins.raw) == cop_co_bit) {
    op_cop(ins);
    return;
  }

  switch (ins.rs) {
    case 0b00010:
      op_cfc(ins);
      break;
    case 0b00110:
      op_ctc(ins);
      break;
    case 0b00000:
      op_mfc(ins);
      break;
    case 0b00100:
      op_mtc(ins);
      break;
    default:
      op_illegal(ins);
      break;
  }
}

void
Assembler::op_ctc(const Instruction ins)
{
  const u32 cop_index = get_coprocessor_index(ins);
  throw_if_coprocessor_not_present(cop_index);

  const u32 cop_ctrl_base =
    Registers::COP0_CTRL + Registers::NUM_REGS_PER_COP * cop_index;
  const u32 reg_index = cop_ctrl_base + ins.rd;

  call(
    fox::ir::Type::Integer32,
    [](fox::Guest *guest, fox::Value reg_index, fox::Value write_value) {
      // const u16 cop_index =
      //   (reg_index.u16_value - Registers::COP0_DATA) / Registers::NUM_REGS_PER_COP;
      // const u16 cop_reg =
      //   (reg_index.u16_value - Registers::COP0_DATA) % Registers::NUM_REGS_PER_COP;
      // printf("CTC: cop%u.r%02u <- 0x%08x\n", cop_index, cop_reg, write_value.u32_value);
      return fox::Value { .u32_value = 0 };
    },
    const_u32(reg_index),
    m_Rt);

  write_reg(reg_index, m_Rt);
}

void
Assembler::op_div(const Instruction ins)
{
  invalidate(Registers::LO, true);
  invalidate(Registers::HI, true);
  call(
    fox::ir::Type::Integer32,
    [](fox::Guest *guest, fox::Value rs, fox::Value rt) {
      R3000 *const r3000 = reinterpret_cast<R3000 *>(guest);

      // numerator > =         0, denom=0           => LO = 0xFFFF'FFFF, HI = numerator
      // numerator <           0, denom=0           => LO =           1, HI = numerator
      // numerator = 0x8000'0000, denom=0xFFFF'FFFF => LO = 0x8000'0000, HI = 0

      if (rt.u32_value == 0) {
        r3000->m_regs[Registers::HI] = rs.u32_value;
        r3000->m_regs[Registers::LO] = rs.i32_value >= 0 ? 0xFFFF'FFFF : 1;
      } else if (rs.u32_value == 0x8000'0000 && rt.i32_value == -1) {
        r3000->m_regs[Registers::HI] = 0;
        r3000->m_regs[Registers::LO] = 0x8000'0000;
      } else {
        const i32 remainder = rs.i32_value % rt.i32_value;
        const i32 quotient = rs.i32_value / rt.i32_value;
        r3000->m_regs[Registers::HI] = remainder;
        r3000->m_regs[Registers::LO] = quotient;
      }

      return fox::Value { .u32_value = 0 };
    },
    m_Rs,
    m_Rt);
}

void
Assembler::op_divu(const Instruction ins)
{
  invalidate(Registers::LO, true);
  invalidate(Registers::HI, true);
  call(
    fox::ir::Type::Integer32,
    [](fox::Guest *guest, fox::Value rs, fox::Value rt) {
      R3000 *const r3000 = reinterpret_cast<R3000 *>(guest);

      const u32 n = rs.u32_value;
      const u32 d = rt.u32_value;

      if (d == 0) {
        r3000->m_regs[Registers::HI] = n;
        r3000->m_regs[Registers::LO] = 0xFFFF'FFFF;
      } else {
        r3000->m_regs[Registers::HI] = n % d;
        r3000->m_regs[Registers::LO] = n / d;
      }

      return fox::Value { .u32_value = 0 };
    },
    m_Rs,
    m_Rt);
}

void
Assembler::op_j(const Instruction ins)
{
  const u32 new_pc = (m_pc & 0xF000'0000) | (ins.target << 2);
  jmp_delay(const_u32(new_pc));
}

void
Assembler::op_jal(const Instruction ins)
{
  /* Note: Return address is the instruction following the delay slot. */
  write_reg(Registers::RA, const_u32(m_pc + 8u));
  op_j(ins);
}

void
Assembler::op_jalr(const Instruction ins)
{
  /* Note: Return address is the instruction following the delay slot. */
  write_reg(ins.rd, const_u32(m_pc + 8u));
  jmp_delay(m_Rs);
}

void
Assembler::op_jr(const Instruction ins)
{
  /* TODO Generate address exception if value of m_Rs is not 16-bit aligned at
   *      the time branch is taken. (?? 32-bit) */
  jmp_delay(m_Rs);
}

void
Assembler::op_lb(const Instruction ins)
{
  const Operand load_address = add(m_Rs, const_u32(ins.imm_se()));
  const Operand loaded_value = load(fox::ir::Type::Integer8, load_address);
  const Operand extended = extend32(loaded_value);
  write_reg_delayed(ins.rt, extended);
}

void
Assembler::op_lbu(const Instruction ins)
{
  const Operand load_address = add(m_Rs, const_u32(ins.imm_se()));
  const Operand loaded_value = load(fox::ir::Type::Integer8, load_address);
  const Operand extended = bitcast(fox::ir::Type::Integer32, loaded_value);
  write_reg_delayed(ins.rt, extended);
}

void
Assembler::op_lh(const Instruction ins)
{
  const Operand load_address = add(m_Rs, const_u32(ins.imm_se()));
  exception_on_unaligned_access<2, Exceptions::AddressErrorLoad>(load_address);

  const Operand loaded_value = load(fox::ir::Type::Integer16, load_address);
  write_reg_delayed(ins.rt, extend32(loaded_value));
}

void
Assembler::op_lhu(const Instruction ins)
{
  const Operand load_address = add(m_Rs, const_u32(ins.imm_se()));
  exception_on_unaligned_access<2, Exceptions::AddressErrorLoad>(load_address);

  const Operand loaded_value = load(fox::ir::Type::Integer16, load_address);
  const Operand extended = bitcast(fox::ir::Type::Integer32, loaded_value);
  write_reg_delayed(ins.rt, extended);
}

void
Assembler::op_lui(const Instruction ins)
{
  write_reg(ins.rt, const_u32(ins.imm << 16));
}

void
Assembler::op_lw(const Instruction ins)
{
  // XXX : Handle exception cases
  Operand address = add(m_Rs, const_u32(ins.imm_se()));

  // For now, do a call in order to ensure cache isolation is handled properly
  flush(Registers::SR);
  const Operand load_value = load(fox::ir::Type::Integer32, address);

  // TODO : Verify read-after-write delay for this operations
  // sw address, 777
  // lw address
  // ^^^ What value do you actually get back?
  write_reg_delayed(ins.rt, load_value);
}

void
Assembler::op_lwc0(const Instruction ins)
{
  __unimplemented(ins);
}

void
Assembler::op_lwc1(const Instruction ins)
{
  __unimplemented(ins);
}

void
Assembler::op_lwc2(const Instruction ins)
{
  // For now, do a call in order to ensure cache isolation is handled properly
  flush(Registers::SR);

  const Operand address = add(m_Rs, const_u32(ins.imm_se()));
  const Operand load_value = load(fox::ir::Type::Integer32, address);
  write_reg_delayed(Registers::COP2_DATA + ins.rt, load_value);
}

void
Assembler::op_lwc3(const Instruction ins)
{
  __unimplemented(ins);
}

void
Assembler::op_lwl(const Instruction ins)
{
  const Operand addr = add(m_Rs, const_u32(ins.imm_se()));

  // 0   4bcd   (mem << 24) | (reg & 0x00ffffff)
  // 1   34cd   (mem << 16) | (reg & 0x0000ffff)
  // 2   234d   (mem <<  8) | (reg & 0x000000ff)
  // 3   1234   (mem      ) | (reg & 0x00000000)

  flush(Registers::SR);
  write_reg(ins.rt,
            call(
              fox::ir::Type::Integer32,
              [](fox::Guest *guest, fox::Value addr, fox::Value reg) {
                R3000 *const r3000 = reinterpret_cast<R3000 *>(guest);

                const u32 aligned_addr = addr.u32_value & ~3;
                const u32 shift = addr.u32_value & 3;
                const u32 mem = r3000->guest_load(aligned_addr, 4).u32_value;

                u32 f = 0;
                switch (shift) {
                  case 0:
                    f = (reg.u32_value & 0x00ff'ffff) | (mem << 24);
                    break;
                  case 1:
                    f = (reg.u32_value & 0x0000'ffff) | (mem << 16);
                    break;
                  case 2:
                    f = (reg.u32_value & 0x0000'00ff) | (mem << 8);
                    break;
                  case 3:
                    f = (reg.u32_value & 0x0000'0000) | (mem);
                    break;
                  default:
                    assert(false);
                }

                return fox::Value { .u32_value = f };
              },
              addr,
              m_Rt));

#if 0
  const Operand addr_aligned = _and(addr, const_u32(0xffff'fffc));

  // Read the aligned address
  flush(Registers::SR);
  const Operand aligned_load = load(fox::ir::Type::Integer32, addr_aligned);
  const Operand misalignment = _and(addr, const_u32(0b11));

    // 0   4bcd   (mem << 24) | (reg & 0x00ffffff)
    // 1   34cd   (mem << 16) | (reg & 0x0000ffff)
    // 2   234d   (mem <<  8) | (reg & 0x000000ff)
    // 3   1234   (mem      ) | (reg & 0x00000000)

  // misalignmentt=0 0x00ff'ffff
  // misalignmentt=1 0x0000'ffff
  // misalignmentt=2 0x0000'00ff
  // misalignmentt=3 0x0000'0000
  // 0xffff'ffff >> ((misalignment - 1) * 8)
  const Operand mask =
    shiftr(const_u32(0xffff'ffff), umul(add(misalignment, const_u32(1)), const_u32(8)));
  const Operand left_final = _and(mask, m_Rt);

  const Operand right_final =
    shiftl(aligned_load, umul(sub(const_u32(3), misalignment), const_u32(8)));

  const Operand combined = _or(left_final, right_final);
  write_reg_delayed(ins.rt, combined);

  // XXX : probably(?) works if misalignment is 0
  // XXX CRITICAL : some care needed if LWR is hidden in the next instruction since they
  // actually should combine
#endif
}

void
Assembler::op_lwr(const Instruction ins)
{
  const Operand addr = add(m_Rs, const_u32(ins.imm_se()));

  // 0   1234   (mem      ) | (reg & 0x00000000)
  // 1   a123   (mem >>  8) | (reg & 0xff000000)
  // 2   ab12   (mem >> 16) | (reg & 0xffff0000)
  // 3   abc1   (mem >> 24) | (reg & 0xffffff00)

  flush(Registers::SR);
  write_reg(ins.rt,
            call(
              fox::ir::Type::Integer32,
              [](fox::Guest *guest, fox::Value addr, fox::Value reg) {
                R3000 *const r3000 = reinterpret_cast<R3000 *>(guest);

                const u32 aligned_addr = addr.u32_value & ~3;
                const u32 shift = addr.u32_value & 3;
                const u32 mem = r3000->guest_load(aligned_addr, 4).u32_value;

                u32 f = 0;
                switch (shift) {
                  case 0:
                    f = (reg.u32_value & 0x0000'0000) | (mem);
                    break;
                  case 1:
                    f = (reg.u32_value & 0xff00'0000) | (mem >> 8);
                    break;
                  case 2:
                    f = (reg.u32_value & 0xffff'0000) | (mem >> 16);
                    break;
                  case 3:
                    f = (reg.u32_value & 0xffff'ff00) | (mem >> 24);
                    break;
                  default:
                    assert(false);
                }

                return fox::Value { .u32_value = f };
              },
              addr,
              m_Rt));

#if 0
  const Operand addr_aligned = _and(addr, const_u32(0xffff'fffc));

  // Read the aligned address
  flush(Registers::SR);
  const Operand aligned_load = load(fox::ir::Type::Integer32, addr_aligned);
  const Operand misalignment = _and(addr, const_u32(0b11));

  // misalignmentt=0 0x0000'0000
  // misalignmentt=1 0xff00'0000
  // misalignmentt=2 0xffff'0000
  // misalignmentt=3 0xffff'ff00
  // 0xffff'ffff << ((4 - misalignment) * 8)
  const Operand left_mask =
    shiftl(const_u32(0xffff'ffff), umul(sub(const_u32(4), misalignment), const_u32(8)));

  const Operand left_final = _and(left_mask, m_Rt);
  const Operand right_final = shiftr(aligned_load, umul(misalignment, const_u32(8)));

  const Operand combined = _or(left_final, right_final);
  write_reg_delayed(ins.rt, combined);

  // XXX CRITICAL : See LWL concerns
#endif
}

void
Assembler::op_mfc(const Instruction ins)
{
  const u32 cop_index = get_coprocessor_index(ins);
  throw_if_coprocessor_not_present(cop_index);

  const u32 cop_reg_index =
    Registers::COP0_DATA + Registers::NUM_REGS_PER_COP * cop_index + ins.rd;
  const Operand cop_reg_value = read_reg(cop_reg_index);

  call(
    fox::ir::Type::Integer32,
    [](fox::Guest *guest, fox::Value reg_index, fox::Value read_value) {
      // const u16 cop_index =
      //   (reg_index.u16_value - Registers::COP0_DATA) / Registers::NUM_REGS_PER_COP;
      // const u16 cop_reg =
      //   (reg_index.u16_value - Registers::COP0_DATA) % Registers::NUM_REGS_PER_COP;
      // printf("MFC: cop%u.r%02u -> 0x%08x\n", cop_index, cop_reg, read_value.u32_value);
      return fox::Value { .u32_value = 0 };
    },
    const_u32(cop_reg_index),
    cop_reg_value);

  write_reg_delayed(ins.rt, cop_reg_value);
}

void
Assembler::op_mfhi(const Instruction ins)
{
  write_reg(ins.rd, read_reg(Registers::HI));
}

void
Assembler::op_mflo(const Instruction ins)
{
  write_reg(ins.rd, read_reg(Registers::LO));
}

void
Assembler::op_mtc(const Instruction ins)
{
  const u32 cop_index = get_coprocessor_index(ins);
  throw_if_coprocessor_not_present(cop_index);
  assert(cop_index == 0 || cop_index == 2);

  const u32 cop_reg_base = Registers::COP0_DATA + Registers::NUM_REGS_PER_COP * cop_index;
  const u32 cop_reg_index = cop_reg_base + ins.rd;

  call(
    fox::ir::Type::Integer32,
    [](fox::Guest *guest, fox::Value reg_index, fox::Value write_value) {
      // const u16 cop_index =
      //   (reg_index.u16_value - Registers::COP0_DATA) / Registers::NUM_REGS_PER_COP;
      // const u16 cop_reg =
      //   (reg_index.u16_value - Registers::COP0_DATA) % Registers::NUM_REGS_PER_COP;
      // printf("MTC: cop%u.r%02u <- 0x%08x\n", cop_index, cop_reg, write_value.u32_value);
      return fox::Value { .u32_value = 0 };
    },
    const_u32(cop_reg_index),
    m_Rt);

  write_reg(cop_reg_index, m_Rt);
}

void
Assembler::op_mthi(const Instruction ins)
{
  write_reg(Registers::HI, m_Rs);
}

void
Assembler::op_mtlo(const Instruction ins)
{
  write_reg(Registers::LO, m_Rs);
}

void
Assembler::op_mult(const Instruction ins)
{
  const Operand a = extend64(m_Rs);
  const Operand b = extend64(m_Rt);

  const Operand v = mul(a, b);

  const Operand hi = bitcast(fox::ir::Type::Integer32, shiftr(v, const_u32(32)));
  const Operand lo = bitcast(fox::ir::Type::Integer32, v);

  write_reg(Registers::HI, hi);
  write_reg(Registers::LO, lo);
}

void
Assembler::op_multu(const Instruction ins)
{
  const Operand a = bitcast(fox::ir::Type::Integer64, m_Rs);
  const Operand b = bitcast(fox::ir::Type::Integer64, m_Rt);

  const Operand v = umul(a, b);

  const Operand hi = bitcast(fox::ir::Type::Integer32, shiftr(v, const_u32(32)));
  const Operand lo = bitcast(fox::ir::Type::Integer32, v);

  write_reg(Registers::HI, hi);
  write_reg(Registers::LO, lo);
}

void
Assembler::op_nor(const Instruction ins)
{
  write_reg(ins.rd, _not(_or(m_Rs, m_Rt)));
}

void
Assembler::op_or(const Instruction ins)
{
  write_reg(ins.rd, _or(m_Rs, m_Rt));
}

void
Assembler::op_ori(const Instruction ins)
{
  write_reg(ins.rt, _or(m_Rs, const_u32(ins.imm)));
}

void
Assembler::op_sb(const Instruction ins)
{
  const Operand val = bitcast(fox::ir::Type::Integer8, m_Rt);
  const Operand address = add(m_Rs, const_u32(ins.imm_se()));
  flush(Registers::SR);
  store(address, val);
}

void
Assembler::op_sh(const Instruction ins)
{
  const Operand lower = bitcast(fox::ir::Type::Integer16, m_Rt);
  const Operand address = add(m_Rs, const_u32(ins.imm_se()));
  flush(Registers::SR);
  store(address, lower);
}

void
Assembler::op_sll(const Instruction ins)
{
  write_reg(ins.rd, shiftl(m_Rt, const_u32(ins.shamt)));
}

void
Assembler::op_sllv(const Instruction ins)
{
  // MIPS defines only the bottom 5 bits as valid. We don't handle that here, but the IR
  // only looks at the bottom 5 bits, so this is handled.
  write_reg(ins.rd, shiftl(m_Rt, m_Rs));
}

void
Assembler::op_slt(const Instruction ins)
{
  write_reg(ins.rd, select(cmp_lt(m_Rs, m_Rt), const_u32(0), const_u32(1)));
}

void
Assembler::op_slti(const Instruction ins)
{
  const Operand imm = const_u32(ins.imm_se());
  write_reg(ins.rt, select(cmp_lt(m_Rs, imm), const_u32(0), const_u32(1)));
}

void
Assembler::op_sltiu(const Instruction ins)
{
  const Operand imm = const_u32(ins.imm_se());
  write_reg(ins.rt, select(cmp_ult(m_Rs, imm), const_u32(0), const_u32(1)));
}

void
Assembler::op_sltu(const Instruction ins)
{
  write_reg(ins.rd, select(cmp_ult(m_Rs, m_Rt), const_u32(0), const_u32(1)));
}

void
Assembler::op_sra(const Instruction ins)
{
  write_reg(ins.rd, ashiftr(m_Rt, const_u32(ins.shamt)));
}

void
Assembler::op_srav(const Instruction ins)
{
  write_reg(ins.rd, ashiftr(m_Rt, m_Rs));
}

void
Assembler::op_srl(const Instruction ins)
{
  write_reg(ins.rd, shiftr(m_Rt, const_u32(ins.shamt)));
}

void
Assembler::op_srlv(const Instruction ins)
{
  write_reg(ins.rd, shiftr(m_Rt, m_Rs));
}

void
Assembler::op_sub(const Instruction ins)
{
  // like subu, but signed, and causes exception on signed overflow
  // For simplicity and reuse, we negate the second argument and use our existing add
  // logic.
  const Operand arg2 = add(_not(m_Rt), const_u32(1));

  Operand sum, overflow;
  add_with_overflow(m_Rs, arg2, &sum, &overflow);
  exception_on_overflow(overflow);

  // "The destination register rt is not modified when an integer overflow exception
  // occurs."
  write_reg(ins.rd, sum);
}

void
Assembler::op_subiu(const Instruction ins)
{
  write_reg(ins.rt, sub(m_Rs, const_u32(ins.imm_se())));
}

void
Assembler::op_subu(const Instruction ins)
{
  write_reg(ins.rd, sub(m_Rs, m_Rt));
}

void
Assembler::op_sw(const Instruction ins)
{
  // XXX : Handle exception cases
  Operand address = add(m_Rs, const_u32(ins.imm_se()));

  // For now, do a call in order to ensure cache isolation is handled properly
  // store(vAddr, m_Rt);
  flush(Registers::SR);
  store(address, m_Rt);
}

void
Assembler::op_swc0(const Instruction ins)
{
  __unimplemented(ins);
}

void
Assembler::op_swc1(const Instruction ins)
{
  __unimplemented(ins);
}

void
Assembler::op_swc2(const Instruction ins)
{
  // For now, do a call in order to ensure cache isolation is handled properly
  flush(Registers::SR);

  const Operand data = read_reg(Registers::COP2_DATA + ins.rt);
  const Operand address = add(m_Rs, const_u32(ins.imm_se()));
  store(address, data);
}

void
Assembler::op_swc3(const Instruction ins)
{
  __unimplemented(ins);
}

void
Assembler::op_swl(const Instruction ins)
{
  const Operand addr = add(m_Rs, const_u32(ins.imm_se()));

  flush(Registers::SR);
  call(
    fox::ir::Type::Integer32,
    [](fox::Guest *guest, fox::Value addr, fox::Value rt) {
      R3000 *const r3000 = reinterpret_cast<R3000 *>(guest);

      const u32 aligned_addr = addr.u32_value & ~3;
      const u32 shift = addr.u32_value & 3;
      const u32 mem = r3000->guest_load(aligned_addr, 4).u32_value;

      u32 f = 0;
      switch (shift) {
        case 0:
          f = (rt.u32_value >> 24) | (mem & 0xffff'ff00);
          break;
        case 1:
          f = (rt.u32_value >> 16) | (mem & 0xffff'0000);
          break;
        case 2:
          f = (rt.u32_value >> 8) | (mem & 0xff00'0000);
          break;
        case 3:
          f = (rt.u32_value >> 0) | (mem & 0x0000'0000);
          break;
        default:
          assert(false);
      }

      r3000->guest_store(aligned_addr, 4, fox::Value { .u32_value = f });

      return fox::Value { .u32_value = 0 };
    },
    addr,
    m_Rt);

#if 0
  const Operand aligned_addr = _and(addr, const_u32(0xffff'fffc));
  const Operand cur_mem = load(fox::ir::Type::Integer32, aligned_addr);

  const Operand misalignment = _and(addr, const_u32(0b11));

  // 0x1ffcdb -> misalignment=3

  /*
    Mem = 1234.  Reg = abcd
    0   123a   (reg >> 24) | (mem & 0xffffff00)
    1   12ab   (reg >> 16) | (mem & 0xffff0000)
    2   1abc   (reg >>  8) | (mem & 0xff000000)
    3   abcd   (reg      ) | (mem & 0x00000000)
  */

  // 0xffff'ffff << ((misalignment + 1) * 8)
  const Operand left_mask =
    shiftl(const_u32(0xffff'ffff), umul(add(const_u32(1), misalignment), const_u32(8)));
  const Operand final_left = _and(cur_mem, left_mask);

  const Operand final_right =
    shiftr(m_Rt, umul(sub(const_u32(3), misalignment), const_u32(8)));

  store(aligned_addr, _or(final_left, final_right));
#endif
}

void
Assembler::op_swr(const Instruction ins)
{
  const Operand addr = add(m_Rs, const_u32(ins.imm_se()));

  flush(Registers::SR);
  call(
    fox::ir::Type::Integer32,
    [](fox::Guest *guest, fox::Value addr, fox::Value rt) {
      R3000 *const r3000 = reinterpret_cast<R3000 *>(guest);

      // 0   abcd   (reg      ) | (mem & 0x00000000)
      // 1   bcd4   (reg <<  8) | (mem & 0x000000ff)
      // 2   cd34   (reg << 16) | (mem & 0x0000ffff)
      // 3   d234   (reg << 24) | (mem & 0x00ffffff)

      const u32 aligned_addr = addr.u32_value & ~3;
      const u32 shift = addr.u32_value & 3;
      const u32 mem = r3000->guest_load(aligned_addr, 4).u32_value;

      u32 f = 0;
      switch (shift) {
        case 0:
          f = (rt.u32_value << 0) | (mem & 0x0000'0000);
          break;
        case 1:
          f = (rt.u32_value << 8) | (mem & 0x0000'00ff);
          break;
        case 2:
          f = (rt.u32_value << 16) | (mem & 0x0000'ffff);
          break;
        case 3:
          f = (rt.u32_value << 24) | (mem & 0x00ff'ffff);
          break;
        default:
          assert(false);
      }

      r3000->guest_store(aligned_addr, 4, fox::Value { .u32_value = f });

      return fox::Value { .u32_value = 0 };
    },
    addr,
    m_Rt);

#if 0
  const Operand aligned_addr = _and(addr, const_u32(0xffff'fffc));
  const Operand cur_mem = load(fox::ir::Type::Integer32, aligned_addr);

  const Operand misalignment = _and(addr, const_u32(0b11));

  /*
  Mem = 1234.  Reg = abcd
  0   abcd   (reg      ) | (mem & 0x00000000)
  1   bcd4   (reg <<  8) | (mem & 0x000000ff)
  2   cd34   (reg << 16) | (mem & 0x0000ffff)
  3   d234   (reg << 24) | (mem & 0x00ffffff)
  */

  // 0xffff'ffff >> ((4 - misalignment) * 8)
  const Operand mask =
    shiftr(const_u32(0xffff'ffff), umul(sub(const_u32(4), misalignment), const_u32(8)));

  const Operand final_right = _and(cur_mem, mask);
  const Operand final_left = shiftl(m_Rt, umul(misalignment, const_u32(8)));

  store(aligned_addr, _or(final_left, final_right));
#endif
}

void
Assembler::op_syscall(const Instruction ins)
{
  // XXX : Timing at exit from syscall
  exception(Exceptions::Syscall);
}

void
Assembler::op_xor(const Instruction ins)
{
  write_reg(ins.rd, _xor(m_Rs, m_Rt));
}

void
Assembler::op_xori(const Instruction ins)
{
  write_reg(ins.rt, _xor(m_Rs, const_u32(ins.imm)));
}

void
Assembler::op_illegal(const Instruction ins)
{
  throw std::runtime_error("r3000: Illegal opcode");
}

void
Assembler::throw_if_coprocessor_not_present(const unsigned z)
{
  // NOTE: Assuming PS1
  const bool is_cop_present = z == 0 || z == 2;

  // XXX : This should raise an exception within the guest.

  if (!is_cop_present) {
    throw std::runtime_error(
      "r3000: Instruction refers to coprocessor that doesn't exist");
  }
}

void
Assembler::write_reg(const u16 index, const fox::ir::Operand value)
{
  assert(value.type() == fox::ir::Type::Integer32);

  if (index == Registers::R0) {
    /* Writes to the 0 register are ignored */
    return;
  }

  m_registers[index].value = value;
  m_registers[index].valid = true;
  m_registers[index].dirty = true;
}

void
Assembler::write_reg_delayed(const u16 index, const fox::ir::Operand value)
{
  if (index == Registers::R0) {
    /* Writes to the 0 register are ignored */
    return;
  }

  m_writeback_value = value;
  m_writeback_index = index;
}

Operand
Assembler::read_reg(const u16 index)
{
  /* R0 is always zero */
  if (index == Registers::R0) {
    return const_u32(0);
  }

  if (m_registers[index].valid) {
    return m_registers[index].value;
  }

  const Operand ssr_index = Operand::constant<u16>(index);
  m_registers[index].value = readgr(fox::ir::Type::Integer32, ssr_index);
  m_registers[index].valid = true;
  return m_registers[index].value;
}

void
Assembler::jmp_delay(const Operand new_pc, const Operand condition)
{
  m_branch_executed = true;
  m_branch_delayed = true;

  if (condition.is_constant()) {
    if (!condition.value().bool_value) {
      /* Branch never taken. */
      return;
    }

    write_reg(Registers::BRANCH_DELAY_ADDRESS, new_pc);
    return;
  }

  write_reg(Registers::BRANCH_DELAY_ADDRESS,
            select(condition, const_u32(INVALID_BRANCH_DELAY_ADDRESS), new_pc));
}

void
Assembler::jmp_nodelay(const Operand new_pc, const Operand condition)
{
  m_branch_executed = true;
  m_branch_delayed = false;

  if (condition.is_constant()) {
    if (!condition.value().bool_value) {
      return;
    }

    write_reg(Registers::PC, new_pc);
    return;
  }

  write_reg(Registers::PC, select(condition, const_u32(m_pc + 4u), new_pc));
}

void
Assembler::add_with_overflow(const Operand a,
                             const Operand b,
                             Operand *const out_sum,
                             Operand *const out_did_overflow)
{
  *out_sum = add(a, b);

  // Signed overflow has happened when one of the following things happens
  // 1) (a positive + b positive == c negative)
  // 2) (a negative + b negative == c positive)
  // So, if both a and b have the same sign but the sign of the result is different, then
  // there was an overflow.

  const Operand sign_mask = const_u32(0x8000'0000);
  const Operand sign_a = _and(sign_mask, a);
  const Operand sign_b = _and(sign_mask, b);
  const Operand sign_sum = _and(sign_mask, *out_sum);

  const Operand sign_same_ab = cmp_eq(sign_a, sign_b);
  const Operand sign_result_different = _not(cmp_eq(sign_a, sign_sum));

  *out_did_overflow = _and(sign_same_ab, sign_result_different);
}

void
Assembler::exception(const Exceptions::Exception exceptionCause)
{
  /* SR.BEV chooses RAM vs ROM address for exception handler. */
  const Operand handler_condition =
    test(read_reg(Registers::SR), const_u32(1 << Registers::SR_Bits::BEV_bit));
  const Operand handler =
    select(handler_condition, const_u32(0x8000'0080), const_u32(0xbfc0'0180));

  /* The IEx / KUx bits of SR are used as a 3-depth stack of CPU state. Software
   * must handle stack overflows manually. */
  const u32 mode_mask = u32(0x3f);

  Operand sr = read_reg(Registers::SR);
  const Operand mode = _and(sr, const_u32(mode_mask));
  sr = _and(sr, const_u32(~mode_mask));
  sr = _or(sr, _and(shiftl(mode, const_u32(2)), const_u32(mode_mask)));
  write_reg(Registers::SR, sr);

  const Operand delayed_branch = read_reg(Registers::BRANCH_DELAY_ADDRESS);
  const Operand not_in_delay_slot =
    cmp_eq(delayed_branch, const_u32(INVALID_BRANCH_DELAY_ADDRESS));

  Operand cause = read_reg(Registers::CAUSE);
  cause = _and(cause, const_u32(~0x7c));
  cause = _or(cause, const_u32(exceptionCause << 2));
  const Operand bd_bit = const_u32(1 << Registers::CAUSE_Bits::BD_bit);
  cause = select(not_in_delay_slot, cause, _or(cause, bd_bit));
  write_reg(Registers::CAUSE, cause);

  // If in a delay slot, we need to subtract 4 from PC before assigning to EPC
  const Operand epc = select(not_in_delay_slot, const_u32(m_pc - 4u), const_u32(m_pc));
  write_reg(Registers::EPC, epc);

  jmp_nodelay(handler);
}

void
Assembler::exception_on_overflow(const Operand condition)
{
  // XXX : We don't handle this yet, but this will let us continue until someday an
  // overflow really should cause an exception

  // Section 2.77
  call(
    fox::ir::Type::Integer32,
    [](fox::Guest *, const fox::Value condition) {
      if (condition.bool_value) {
        printf("r3000: Overflow should cause exception. Not yet modeled.\n");
        // throw std::runtime_error(
        //   "r3000: Overflow should cause exception. Not yet modeled.");
      }
      return fox::Value { .u32_value = 0 };
    },
    condition);
}

template<u32 bytes, Exceptions::Exception exception_type>
void
Assembler::exception_on_unaligned_access(const Operand address)
{
  // Section 2.78
  call(
    fox::ir::Type::Integer32,
    [](fox::Guest *, const fox::Value address) {
      if (address.u32_value % bytes != 0) {
        printf("Unaligned access generates exception %u\n", unsigned(exception_type));
        // throw std::runtime_error(exception_str);
      }
      return fox::Value { .u32_value = 0 };
    },
    address);
}

}
