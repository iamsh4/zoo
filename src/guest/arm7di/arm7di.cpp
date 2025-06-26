#include <thread>
#include <cstring>
#include <cassert>

#include "fox/bytecode/compiler.h"
#include "shared/bitmanip.h"
#include "guest/arm7di/arm7di.h"
#include "arm7di_jit.h"

// - Construct EBBs instead of single instruction
// - Handle PC during assembly instead of during execution (how are branches not broken
// today?)
// - Disassembly support
// - Return cycle counts at exit of each EBB

using namespace std::chrono_literals;

bool
arm7di_debug_enabled()
{
  static const bool cached_value = []() {
    const char *val = getenv("ARM7DI_DEBUG");
    return val && atoi(val) > 0;
  }();
  return cached_value;
}

namespace guest::arm7di {

const u32 CPSR_I = (1 << 7);
const u32 CPSR_F = (1 << 6);

using Operand = fox::ir::Operand;

Arm7DI::Arm7DI(fox::MemoryTable *const mem) : m_mem(mem), m_assembler(), m_jit_cache(mem)
{
  reset();
}

Arm7DI::~Arm7DI()
{
  return;
}

Arm7DI::Registers &
Arm7DI::registers()
{
  return m_registers;
}

void
Arm7DI::reset()
{
  memset(&m_registers, 0, sizeof(Registers));
  m_registers.CPSR.M                    = Mode_SVC;
  m_registers.CPSR.I                    = 1;
  m_registers.CPSR.F                    = 1;
  m_registers.R[Arm7DIRegisterIndex_PC] = 0x00000000;
  mode_switch(ProcessorMode::Mode_USR, ProcessorMode::Mode_SVC);
}

u32
Arm7DI::debug_fetch_instruction(u32 address)
{
  return guest_load(address, 4).u32_value;
}

void
Arm7DI::raise_exception(Exception exception)
{
  if (exception >= Exception_NUM_EXCEPTIONS) {
    throw std::runtime_error("Invalid exception index");
  }

  // printf("RAISING EXCEPTION %u\n", exception);

  // De-assert our internal IRQ/FIQ-pending while we enter the handler
  if (exception == Exception_FIQ) {
    m_registers.pending_interrupts &= ~CPSR_F;
  }

  const ProcessorMode current_mode = static_cast<ProcessorMode>(m_registers.CPSR.M);
  const ProcessorMode new_mode     = kExceptionModes[exception];

  // Swith to new mode, save old CPSR -> SPSR
  const u32 old_cpsr = m_registers.CPSR.raw;
  mode_switch(current_mode, new_mode);
  m_registers.SPSR.raw = old_cpsr;

  // Save PC to LR
  m_registers.R[Arm7DIRegisterIndex_LR] = m_registers.R[Arm7DIRegisterIndex_PC] + 4;
  // Keep condition bits, change mode bits, disable IRQs regardless of exception cause
  // m_registers.CPSR.M = new_mode;
  // m_registers.CPSR.I = 1;
  m_registers.CPSR.raw = (m_registers.CPSR.raw & ~0x1f) | new_mode | CPSR_I;

  // We're entering FIQ handler, need to disable FIQ.
  if (new_mode == Mode_FIQ) {
    m_registers.CPSR.F = 1;
  }
  m_registers.R[Arm7DIRegisterIndex_PC] = kExceptionHandlers[exception];
}

void
Arm7DI::raise_fiq()
{
  m_registers.pending_interrupts |= CPSR_F;
}

void
Arm7DI::clear_fiq()
{
  // m_registers.pending_interrupts &= ~CPSR_F;
}

void
Arm7DI::step()
{
  // Check for IRQ/FIQ pending interrupts
  if (m_registers.pending_interrupts) {
    const bool fiq_enabled = !m_registers.CPSR.F;
    const bool irq_enabled = !m_registers.CPSR.I;

    if ((m_registers.pending_interrupts & CPSR_F) && fiq_enabled) {
      raise_exception(Exception_FIQ);
    } else if ((m_registers.pending_interrupts & CPSR_I) && irq_enabled) {
      raise_exception(Exception_IRQ);
    }
  }

  // Fetch next instruction
  const u32 pc    = m_registers.R[Arm7DIRegisterIndex_PC];
  const u32 fetch = guest_load(pc, 4).u32_value;

#if 1
  if (arm7di_debug_enabled()) {
    // Print R0-R15 CPSR SPSR
    printf("farm R ");
    for (u32 i = 0; i < 16; ++i)
      printf("%08x ", m_registers.R[i]);
    printf("CPSR %08x SPSR %08x ins %08x\n",
           m_registers.CPSR.raw,
           m_registers.SPSR.raw,
           fetch);
  }
#endif

  // TODO : explanation
  const u32 jit_cache_pc = pc + m_fixed_pc_offset;

  // We need to garbage collect before lookup since the SH4 side may have invalidated
  // some entries.
  m_jit_cache.garbage_collect();
  fox::jit::CacheEntry *entry = m_jit_cache.lookup(jit_cache_pc);
  if (!entry) {
    Arm7DIInstructionInfo instruction { .address = pc, .word = fetch };

    // Decode
    m_assembler.generate_ir(instruction);

    fox::ir::ExecutionUnit eu = m_assembler.assemble();
    fox::ref<fox::jit::CacheEntry> ref_entry =
      new BasicBlock(jit_cache_pc, 4, std::move(eu));
    m_jit_cache.insert(ref_entry.get());
    entry = ref_entry.get();
  }

  BasicBlock *bb   = (BasicBlock *)entry;
  bb->execute(this, 1000);
}

Arm7DIAssembler &
Arm7DI::get_assembler()
{
  return m_assembler;
}

u32
Arm7DI::read_register_user(u32 index) const
{
  if (index >= Arm7DIRegisterIndex_NUM_REGISTERS) {
    throw std::runtime_error("Invalid register index");
  }

  const u32 mode_bits = m_registers.CPSR.raw & 0x1f;

  bool is_banked = false;
  is_banked |= mode_bits == 0x11 && index >= 8 && index <= 14;
  is_banked |= mode_bits != 0x10 && index >= 13 && index <= 14;

  if (mode_bits == 0x10 || !is_banked) {
    // Already in user mode, or not a banked register
    return m_registers.R[index];
  } else {
    // We weren't in user mode, and we're writing to a banked register
    return m_registers.R_user[index];
  }
}

void
Arm7DI::write_register_user(u32 index, u32 value)
{
  if (index >= Arm7DIRegisterIndex_NUM_REGISTERS) {
    throw std::runtime_error("Invalid register index");
  }

  const u32 mode_bits = m_registers.CPSR.raw & 0x1f;

  bool is_banked = false;
  is_banked |= mode_bits == 0x11 && index >= 8 && index <= 14;
  is_banked |= mode_bits != 0x10 && index >= 13 && index <= 14;

  if (mode_bits == 0x10 || !is_banked) {
    // Already in user mode, or not a banked register
    m_registers.R[index] = value;
  } else {
    // We weren't in user mode, and we're writing to a banked register
    m_registers.R_user[index] = value;
  }
}

void
Arm7DI::mode_switch(const ProcessorMode current_mode, const ProcessorMode new_mode)
{
  // printf("Switching from %u to %u\n", current_mode, new_mode);

  switch (current_mode) {
    case Mode_USR:
      /* No-op */
      break;

    case Mode_FIQ:
      std::swap(m_registers.R[8], m_registers.R_fiq[8]);
      std::swap(m_registers.R[9], m_registers.R_fiq[9]);
      std::swap(m_registers.R[10], m_registers.R_fiq[10]);
      std::swap(m_registers.R[11], m_registers.R_fiq[11]);
      std::swap(m_registers.R[12], m_registers.R_fiq[12]);
      std::swap(m_registers.R[13], m_registers.R_fiq[13]);
      std::swap(m_registers.R[14], m_registers.R_fiq[14]);
      m_registers.SPSR_fiq = m_registers.SPSR;
      break;

    case Mode_SVC:
      std::swap(m_registers.R[13], m_registers.R_svc[13]);
      std::swap(m_registers.R[14], m_registers.R_svc[14]);
      m_registers.SPSR_svc = m_registers.SPSR;
      break;

    case Mode_ABT:
      std::swap(m_registers.R[13], m_registers.R_abt[13]);
      std::swap(m_registers.R[14], m_registers.R_abt[14]);
      m_registers.SPSR_abt = m_registers.SPSR;
      break;

    case Mode_IRQ:
      std::swap(m_registers.R[13], m_registers.R_irq[13]);
      std::swap(m_registers.R[14], m_registers.R_irq[14]);
      m_registers.SPSR_irq = m_registers.SPSR;
      break;

    case Mode_UND:
      std::swap(m_registers.R[13], m_registers.R_und[13]);
      std::swap(m_registers.R[14], m_registers.R_und[14]);
      m_registers.SPSR_und = m_registers.SPSR;
      break;
  }

  /* Switch from user mode to target mode */
  switch (new_mode) {
    case Mode_USR:
      /* No-op */
      break;

    case Mode_FIQ:
      std::swap(m_registers.R[8], m_registers.R_fiq[8]);
      std::swap(m_registers.R[9], m_registers.R_fiq[9]);
      std::swap(m_registers.R[10], m_registers.R_fiq[10]);
      std::swap(m_registers.R[11], m_registers.R_fiq[11]);
      std::swap(m_registers.R[12], m_registers.R_fiq[12]);
      std::swap(m_registers.R[13], m_registers.R_fiq[13]);
      std::swap(m_registers.R[14], m_registers.R_fiq[14]);
      m_registers.SPSR = m_registers.SPSR_fiq;
      break;

    case Mode_SVC:
      std::swap(m_registers.R[13], m_registers.R_svc[13]);
      std::swap(m_registers.R[14], m_registers.R_svc[14]);
      m_registers.SPSR = m_registers.SPSR_svc;
      break;

    case Mode_ABT:
      std::swap(m_registers.R[13], m_registers.R_abt[13]);
      std::swap(m_registers.R[14], m_registers.R_abt[14]);
      m_registers.SPSR = m_registers.SPSR_abt;
      break;

    case Mode_IRQ:
      std::swap(m_registers.R[13], m_registers.R_irq[13]);
      std::swap(m_registers.R[14], m_registers.R_irq[14]);
      m_registers.SPSR = m_registers.SPSR_irq;
      break;

    case Mode_UND:
      std::swap(m_registers.R[13], m_registers.R_und[13]);
      std::swap(m_registers.R[14], m_registers.R_und[14]);
      m_registers.SPSR = m_registers.SPSR_und;
      break;
  }
}

fox::Value
Arm7DI::guest_register_read(unsigned index, size_t bytes)
{
  assert(index < Arm7DIRegisterIndex_NUM_REGISTERS);
  assert(bytes == 4);

  if (index <= Arm7DIRegisterIndex_R15)
    return fox::Value { .u32_value = m_registers.R[index] };
  else if (index == Arm7DIRegisterIndex_CPSR)
    return fox::Value { .u32_value = m_registers.CPSR.raw };
  else if (index == Arm7DIRegisterIndex_SPSR)
    return fox::Value { .u32_value = m_registers.SPSR.raw };
  else
    assert(false);

  throw std::runtime_error("Unhandled register read");
}

void
Arm7DI::guest_register_write(unsigned index, size_t bytes, fox::Value value)
{
  assert(index < Arm7DIRegisterIndex_NUM_REGISTERS);
  assert(bytes == 4);

  if (index <= Arm7DIRegisterIndex_R15)
    m_registers.R[index] = value.u32_value;
  else if (index == Arm7DIRegisterIndex_CPSR)
    m_registers.CPSR.raw = value.u32_value;
  else if (index == Arm7DIRegisterIndex_SPSR)
    m_registers.SPSR.raw = value.u32_value;
  else {
    assert(false);
    throw std::runtime_error("Unhandled register write");
  }
}

}
