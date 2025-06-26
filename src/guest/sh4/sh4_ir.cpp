// vim: expandtab:ts=2:sw=2

#include <map>
#include <cassert>

#include "sh4.h"
#include "sh4_ir.h"

using Type = fox::ir::Type;
using Operand = fox::ir::Operand;

namespace cpu {

SH4Assembler::SH4Assembler()
{
  return;
}

SH4Assembler::~SH4Assembler()
{
  return;
}

fox::ir::ExecutionUnit &&
SH4Assembler::assemble(const uint32_t cpu_flags,
                       const InstructionDetail *const instructions,
                       const size_t count)
{
  bool pc_dirty = false;
  m_source = instructions;
  m_cpu_flags = cpu_flags;
  m_source_index = 0;
  m_cpu_cycles = 0;

  while (m_source_index < count) {
    const InstructionDetail &instruction = m_source[m_source_index];
    const u16 opcode_id = instruction.id;
    const Opcode &opcode = (SH4::opcode_table)[opcode_id];
    const auto ir = opcode.ir;

    assert(opcode_id != 0);

    /* Branches with delay slots are translated in one piece, with the
     * translation method using translate_delay_slot() to insert the delay slot
     * IR at the correct point. */
    bool can_translate = !!ir && !(opcode.flags & DISABLE_JIT);
    if (can_translate && (opcode.flags & DELAY_SLOT)) {
      assert((m_source_index + 1) < count);
      m_delay_slot_processed = false;

      /* For branches with delay slots, both the branch and its delay slot must
       * have IR translations. Otherwise both must use upcalls. */
      const InstructionDetail &delay_instruction = m_source[m_source_index + 1u];
      const Opcode &slot = (SH4::opcode_table)[delay_instruction.id];
      can_translate = can_translate && !!slot.ir && !(slot.flags & DISABLE_JIT);
      if (can_translate) {
        m_cpu_cycles += opcode.cycles + slot.cycles;
        (this->*ir)(instruction.raw, instruction.address, m_cpu_flags);
        assert(m_delay_slot_processed);
        m_source_index += 2;
        pc_dirty = false;
        continue;
      } else {
        can_translate = false;
      }
    }

    if (can_translate) {
      m_cpu_cycles += opcode.cycles;
      (this->*ir)(instruction.raw, instruction.address, m_cpu_flags);
      if (!(opcode.flags & BRANCH)) {
        pc_dirty = true;
      } else {
        pc_dirty = false;
      }
      m_source_index += 1;
      continue;
    }

    /* PC is not updated by IR translations, but the interpreter copy needs to
     * be up-to-date before executing upcalls. */
    if (pc_dirty) {
      write_PC(Operand::constant<u32>(instruction.address));
      pc_dirty = false;
    }

    flush();
    invalidate();

    /* Perform upcall to interpreter for instructions without IR translations
     * or where the translation isn't valid. For branches, insert a conditional
     * exit after the branch retires. */
    if (opcode.flags & BRANCH) {
      Operand result = interpret_upcall(instruction);
      if (opcode.flags & DELAY_SLOT) {
        interpret_upcall(m_source[m_source_index + 1u]);
        m_source_index += 1;
      }

      Operand needs_exit;
      if (opcode.flags & CONDITIONAL) {
        needs_exit = test(result, result);
      } else {
        needs_exit = Operand::constant<bool>(true);
      }
      exit(needs_exit);
    } else {
      /* Not a branch */
      interpret_upcall(instruction);
    }
    m_source_index += 1;
  }

  /* Flush final PC address if we haven't already taken a branch. */
  if (pc_dirty) {
    write_PC(Operand::constant<u32>(m_source[count - 1u].address + sizeof(u16)));
    pc_dirty = false;
  }

  /* XXX Possibly redundant, only add it if necessary. */
  const Operand always = Operand::constant<bool>(true);
  exit(always);

  /* Validate execution unit. */
  for (unsigned i = 0; i < _RegisterCount; ++i) {
    assert(!m_registers[i].dirty);
  }

  /* Clear local state for assembly of next unit. */
  for (unsigned i = 0; i < _RegisterCount; ++i) {
    m_registers[i].value = Operand();
    m_registers[i].valid = false;
    m_registers[i].dirty = false;
  }

  m_source = nullptr;
  return export_unit();
}

Operand
SH4Assembler::read_PC()
{
  return read_i32(PC);
}

void
SH4Assembler::write_PC(const Operand value)
{
  write_i32(PC, value);
}

void
SH4Assembler::exit(const Operand decision)
{
  flush();

  /* Record cycles up to this point in case the exit is taken. */
  const Operand ssr_index = Operand::constant<u16>(CycleCount);
  const Operand new_cycles =
    add(readgr(Type::Integer32, ssr_index), Operand::constant<u32>(m_cpu_cycles));
  writegr(ssr_index, new_cycles);
  m_cpu_cycles = 0;

  fox::ir::Assembler::exit(decision, Operand::constant<u64>(0));
}

void
SH4Assembler::flush()
{
  for (unsigned i = 0; i < _RegisterCount; ++i) {
    if (!m_registers[i].dirty) {
      continue;
    }

    assert(m_registers[i].valid);

    const Operand index = Operand::constant<u16>(i);
    writegr(index, m_registers[i].value);
    m_registers[i].dirty = false;
  }
}

void
SH4Assembler::flush(const unsigned index)
{
  if (!m_registers[index].dirty) {
    return;
  }

  assert(m_registers[index].valid);

  writegr(Operand::constant<u16>(index), m_registers[index].value);
  m_registers[index].dirty = false;
}

void
SH4Assembler::invalidate()
{
  for (unsigned i = 0; i < _RegisterCount; ++i) {
    assert(!m_registers[i].dirty);
    m_registers[i].value = Operand();
    m_registers[i].valid = false;
  }
}

void
SH4Assembler::invalidate(unsigned index, bool allow_dirty)
{
  assert(!m_registers[index].dirty || allow_dirty);
  m_registers[index].value = Operand();
  m_registers[index].valid = false;
  m_registers[index].dirty = false;
}

void
SH4Assembler::interpret_upcall()
{
  if (m_in_delay_slot) {
    const InstructionDetail &instruction = m_source[m_source_index + 1u];
    interpret_upcall(instruction, false);
  } else {
    const InstructionDetail &instruction = m_source[m_source_index];
    interpret_upcall(instruction, false);
  }
}

void
SH4Assembler::gpr_maybe_swap(const Operand do_swap)
{
  assert(do_swap.type() == Type::Bool);

  if (do_swap.is_constant() && !do_swap.value().bool_value) {
    return;
  }

  /* Flush all registers that may be affected by a bank swap. */
  for (unsigned i = 0; i < 8; ++i) {
    flush(R0 + i);
    invalidate(R0 + i);
    flush(R0alt + i);
    invalidate(R0alt + i);
  }

  call(Type::Integer64, &SH4::gpr_maybe_swap, do_swap);
}

void
SH4Assembler::fpu_maybe_swap(const Operand do_swap)
{
  assert(do_swap.type() == Type::Bool);

  if (do_swap.is_constant() && !do_swap.value().bool_value) {
    return;
  }

  /* If the swap is unconditional, register references can simply be swapped
   * between the base and alt versions. If it is conditional then registers
   * need to be flushed and (if necessary) reloaded later. */
  if (do_swap.is_constant() && do_swap.value().bool_value) {
    for (unsigned i = 0; i < 16; ++i) {
      std::swap(m_registers[SP0 + i], m_registers[SP0alt + i]);
    }
  } else {
    for (unsigned i = 0; i < 16; ++i) {
      flush(SP0 + i);
      invalidate(SP0 + i);
      flush(SP0alt + i);
      invalidate(SP0alt + i);
    }
  }

  call(Type::Integer64, &SH4::fpu_maybe_swap, do_swap);
}

void
SH4Assembler::translate_delay_slot()
{
  const InstructionDetail &instruction = m_source[m_source_index + 1u];
  const u16 opcode_id = instruction.id;
  const Opcode &opcode = (SH4::opcode_table)[opcode_id];
  const auto ir = opcode.ir;
  assert(!(opcode.flags & ILLEGAL_IN_DELAY_SLOT));

  assert(!m_delay_slot_processed && !m_in_delay_slot);
  m_in_delay_slot = true;
  (this->*ir)(instruction.raw, instruction.address, m_cpu_flags);
  m_delay_slot_processed = true;
  m_in_delay_slot = false;
}

Operand
SH4Assembler::interpret_upcall(const InstructionDetail instruction, const bool add_cycles)
{
  if (add_cycles) {
    m_cpu_cycles += (SH4::opcode_table)[instruction.id].cycles;
  }

  flush(SR);
  flush(SSR);

  const Operand opcode =
    Operand::constant<u64>((u64(instruction.id) << 32) | instruction.raw);
  const Operand pc = Operand::constant<u64>(instruction.address);
  return call(Type::Integer64, &SH4::interpreter_upcall, opcode, pc);
}

}
