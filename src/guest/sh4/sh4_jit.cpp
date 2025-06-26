// vim: expandtab:ts=2:sw=2

#ifdef _WIN64
#include <Windows.h>
#else
#define _XOPEN_SOURCE
#include <ucontext.h>
#endif
#include <cassert>
#include <cstring>
#include <csignal>
#include <fmt/core.h>

#include "fox/ir/optimizer.h"
#include "fox/amd64/amd64_compiler.h"
#include "fox/amd64/amd64_assembler.h"
#include "fox/arm64/arm64_compiler.h"
#include "fox/arm64/arm64_assembler.h"
#include "fox/bytecode/compiler.h"
#include "shared/bitmanip.h"
#include "shared/profiling.h"
#include "core/console.h"
#include "sh4_debug.h"
#include "sh4_jit.h"

namespace cpu {

thread_local static SH4 *s_fault_cpu = nullptr;
thread_local static SH4::BasicBlock *s_fault_block = nullptr;

SH4::BasicBlock::BasicBlock(const u32 address,
                            const u32 size,
                            BasicBlockOpcodes &&instructions,
                            const u32 guard_flags,
                            const u32 jit_flags,
                            const StopReason reason)
  : CacheEntry(address, address & ADDRESS_MASK, size),
    m_stop_reason(reason),
    m_guard_flags(guard_flags),
    m_instructions(std::move(instructions)),
    m_flags(jit_flags)
{
  return;
}

SH4::BasicBlock::~BasicBlock()
{
  return;
}

fox::ir::ExecutionUnit
optimize(fox::ir::ExecutionUnit &input)
{
  fox::ir::ExecutionUnit eu = input.copy();
  eu = fox::ir::optimize::ConstantPropagation().execute(eu);
  eu = fox::ir::optimize::DeadCodeElimination().execute(eu);
  return eu;
}

bool
SH4::BasicBlock::compile()
{
  ProfileZone;

  assert(!is_compiled() || (m_flags & DIRTY));
  m_compiled_flags = m_target_flags;

  SH4Assembler assembler;
  fox::ir::ExecutionUnit ebb =
    assembler.assemble(m_compiled_flags, instructions().data(), instructions().size());
  m_unit.reset(new fox::ir::ExecutionUnit(std::move(ebb)));

  auto eu = optimize(*m_unit);
  try {
    fox::bytecode::Compiler bytecode_compiler;
    m_bytecode = bytecode_compiler.compile(eu.copy());
  } catch (...) {
    return false;
  }

#ifndef __arm64__
  if (1) {
    using namespace fox::codegen::amd64;

    m_compiled_flags = m_stats.last_flags;

    const auto register_address_cb = [](const unsigned index) {
      static_assert(sizeof(regs) + sizeof(FPU) + 4 == SH4Assembler::_RegisterCount * 4);
      assert(index < SH4Assembler::_RegisterCount);

      const Register<QWORD> opaque(Compiler::gpr_guest_registers);
      return RegMemAny(Address<ANY>(opaque, index * sizeof(u32)));
    };

    const auto mem_load_emitter = [](Assembler *const target,
                                     const unsigned read_size,
                                     const GeneralRegister address_register,
                                     const GeneralRegister out) {
      const IndexedAddress<ANY> address(Compiler::gpr_guest_memory, address_register, 1u);
      switch (read_size) {
        case 1:
          target->mov(Register<BYTE>(out), IndexedAddress<BYTE>(address));
          break;
        case 2:
          target->mov(Register<WORD>(out), IndexedAddress<WORD>(address));
          break;
        case 4:
          target->mov(Register<DWORD>(out), IndexedAddress<DWORD>(address));
          break;
        case 8:
          target->mov(Register<QWORD>(out), IndexedAddress<QWORD>(address));
          break;
        default:
          assert(false);
      }
    };

    Compiler amd64_compiler;
    amd64_compiler.set_register_address_cb(register_address_cb);
    if (!(m_flags & DISABLE_FASTMEM)) {
      amd64_compiler.set_memory_load_emitter(mem_load_emitter);
    } else {
      amd64_compiler.set_memory_load_emitter(nullptr);
    }
    m_native = amd64_compiler.compile(std::move(eu));
    m_native->prepare(true);
  }
#endif

#ifdef __arm64__
  {
    using namespace fox::codegen::arm64;

    m_compiled_flags = m_stats.last_flags;

    const auto register_address_cb = [](unsigned index) {
      assert((index * sizeof(u32)) < (sizeof(regs) + sizeof(FPU)));

      /* Note: For double precision registers, this should probably be
       *       multiplied by two, and then in the codegen divide properly, but
       *       this all works out as is. */

      return index;
    };

    Compiler arm64_compiler;
    arm64_compiler.set_register_address_cb(register_address_cb);
    arm64_compiler.set_use_fastmem(!(m_flags & DISABLE_FASTMEM));

    try {
      m_native = arm64_compiler.compile(std::move(eu));
    } catch (std::invalid_argument e) {
      m_native = nullptr;
    }
  }
#endif

#if 0
  m_native->debug_print();

  printf("---\n");
  m_bytecode.debug_print();

  printf("============== %lu bytecode / %lu native @ %p ==============\n",
        m_bytecode.bytes,
        m_native ? m_native->size() : 0lu,
        m_native ? m_native->data() : nullptr);
  printf("\n");
#endif

  mark_clean();
  return true;
}

void
SH4::BasicBlock::execute(SH4 *const cpu)
{
  assert(cpu->regs.PC == instructions().data()[0].address);

  if (s_fault_cpu == nullptr) {
#ifndef _WIN64
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = jit_handle_fault;

    if (sigaction(SIGSEGV, &sa, NULL) < 0) {
      throw std::system_error(std::error_code(errno, std::generic_category()),
                              "sigaction");
    }

    if (sigaction(SIGBUS, &sa, NULL) < 0) {
      throw std::system_error(std::error_code(errno, std::generic_category()),
                              "sigaction");
    }
#else
#error "Windows fault handler not impelmented"
    AddVectoredExceptionHandler(1, &jit_handle_fault);
#endif

    s_fault_cpu = cpu;
  }

  ++m_stats.count_executed;

  /* Check CPU state that can affect block execution. */
  const u32 cpu_flags = calculate_guard_flags(cpu);
  if ((cpu_flags & m_guard_flags) == (m_stats.last_flags & m_guard_flags)) {
    m_stats.last_flags = cpu_flags;
    ++m_stats.last_flags_count;
  } else {
    m_stats.last_flags = cpu_flags;
    m_stats.last_flags_count = 1lu;
  }

  if (!is_compiled()) {
    /* TODO Tune the hueristics here. */
    if (m_stats.count_interpreted > 10u) {
      m_target_flags = cpu_flags;
      cpu->m_jit_cache->queue_compile_unit(this);
    }

    execute_interpreter(cpu);
    return;
  }

  if (m_flags & DIRTY) {
    /* TODO Potentially delay recompilation? */
    m_target_flags = cpu_flags;
    cpu->m_jit_cache->queue_compile_unit(this);

    execute_interpreter(cpu);
    return;
  }

  if ((m_compiled_flags & m_guard_flags) != (cpu_flags & m_guard_flags)) {
    ++m_stats.guard_failed;

    /* TODO Tune the hueristics here. */
    if (m_stats.last_flags_count > 100u) {
      mark_dirty();
      m_target_flags = cpu_flags;
      cpu->m_jit_cache->queue_compile_unit(this);
    }

    execute_interpreter(cpu);
    return;
  }

  bool use_native =
    !!m_native && cpu->get_execution_mode() == SH4::ExecutionMode::Native;
  if (use_native && !m_native->ready()) {
    if (m_stats.count_not_remapped > 20u) {
      m_native->prepare(true);
    } else {
      use_native = m_native->prepare(false);
      m_stats.count_not_remapped += (use_native ? 0u : 1u);
    }
  }

  if (use_native) {
    execute_native(cpu);
    return;
  }

  execute_bytecode(cpu);
}

u32
SH4::BasicBlock::calculate_guard_flags(SH4 *const target)
{
  u32 cpu_flags = 0lu;
  cpu_flags |= (target->FPU.FPSCR.SZ) ? FPSCR_SZ : 0u;
  cpu_flags |= (target->FPU.FPSCR.PR) ? FPSCR_PR : 0u;
  return cpu_flags;
}

u32
SH4::BasicBlock::execute_native(SH4 *const guest)
{
  s_fault_block = this;
  const u32 cycles =
    m_native->execute(guest, (void *)guest->m_phys_mem->root(), &guest->regs);
  s_fault_block = nullptr;

  ++m_stats.count_compiled;
  ++m_stats.count_executed;
  return cycles;
}

u32
SH4::BasicBlock::execute_bytecode(SH4 *const guest)
{
  const u32 cycles =
    m_bytecode->execute(guest, (void *)guest->m_phys_mem->root(), &guest->regs);
  ++m_stats.count_executed;
  return cycles;
}

u32
SH4::BasicBlock::execute_interpreter(SH4 *const guest)
{
  u32 cycles = 0u;
  for (const auto &entry : m_instructions) {
    const Opcode &opcode = opcode_table[entry.id];

    guest->m_executed_branch = false;
    assert(guest->regs.PC == entry.address);
    (guest->*opcode.execute)(entry.raw);
    cycles += opcode.cycles;

    if (!guest->m_executed_branch) {
      if (guest->in_delay_slot()) {
        /* Finished delay slot of branch, exited EBB. */
        guest->regs.PC = guest->m_branch_target;
        guest->m_branch_target = 0xFFFFFFFFu;
        break;
      }
    } else if (!guest->in_delay_slot()) {
      /* No-delay branch, exited EBB. */
      break;
    }

    guest->regs.PC += 2u;
  }

  ++m_stats.count_executed;
  ++m_stats.count_interpreted;

  return cycles;
}

SH4::BasicBlock *
SH4::jit_create_unit(u32 address)
{
  ProfileZone;
  // printf("jit_create_unit START 0x%08x\n", address);

  const u32 start_address = address;
  const u32 next_unit_start = m_jit_cache->trailing_unit(address);
  BasicBlock::StopReason stop_reason = BasicBlock::StopReason::SizeLimit;
  u32 guard_flags = 0;
  u32 jit_flags = 0;
  BasicBlockOpcodes block_opcodes;
  while (address < next_unit_start && block_opcodes.size() < 2048u) {
    const u16 fetch = idata_read(address);
    const u16 opcode_id = decode_table[fetch];
    const Opcode &opcode = opcode_table[opcode_id];
    u64 flags = opcode.flags;

    // printf("    - 0x%08x : fetch 0x%04x opcode_id %u\n", address, fetch, opcode_id);

    /* Invalid opcodes may be behind impossible conditions - just stop when
     * we see one. */
    if (opcode_id == 0) {
      stop_reason = BasicBlock::StopReason::InvalidOpcode;
      break;
    }

    /* If it's a branch with a delay slot, ensure it's included in the EBB. */
    if (opcode.flags & DELAY_SLOT) {
      const u16 slot_fetch = idata_read(address + 2u);
      const u16 slot_opcode_id = decode_table[slot_fetch];

      /* TODO Should we check it's a valid instruction for a delay slot? */

      block_opcodes.push_back(InstructionDetail { address, fetch, opcode_id });
      block_opcodes.push_back(
        InstructionDetail { address + 2u, slot_fetch, slot_opcode_id });
      flags |= opcode_table[slot_opcode_id].flags;
      address += 4u;
    } else {
      block_opcodes.push_back(InstructionDetail { address, fetch, opcode_id });
      address += 2u;
    }

    /* Check if the instruction will be affected by CPU flags that the
     * block compilation may be specialized for. */
    /* Track instruction attributes that can alter how the unit is compiled by
     * the JIT backend. */
    guard_flags |= (flags & FPU_SZ) ? BasicBlock::FPSCR_SZ : 0;
    guard_flags |= (flags & FPU_PR) ? BasicBlock::FPSCR_PR : 0;

    /* Non-conditional branches are guaranteed to be the end of the EBB, unless
     * we attempt to optimize the branch away. */
    if ((opcode.flags & BRANCH) && !(opcode.flags & CONDITIONAL)) {
      stop_reason = BasicBlock::StopReason::Branch;
      break;
    }

    /* Barrier instructions change CPU mode in a way that makes JIT difficult,
     * such as swapping banks or changing FPU size mode. */
    if (flags & BARRIER) {
      stop_reason = BasicBlock::StopReason::Barrier;
      break;
    }
  }

  if (address == next_unit_start) {
    stop_reason = BasicBlock::StopReason::StartOfBlock;
  }

  if (address == start_address) {
    throw std::runtime_error("Tried to generate empty basic block");
  }

  // TODO : Check if this is an SDK symbol
  // printf("jit_create_unit END\n");
  BasicBlock *const block = new BasicBlock(start_address,
                                           address - start_address,
                                           std::move(block_opcodes),
                                           guard_flags,
                                           jit_flags,
                                           stop_reason);

  return block;
}

#if defined(__arm64__)
void
SH4::jit_handle_fault(int signo, siginfo_t *info, void *ucontext_opaque)
{
  /*
   * _STRUCT_ARM_EXCEPTION_STATE64   __es ->
   * {
   *   __uint64_t far;         // Virtual Fault Address
   *   __uint32_t esr;         // Exception syndrome
   *   __uint32_t exception;   // number of arm exception taken
   * }

   * _STRUCT_ARM_THREAD_STATE64      __ss ->
   * {
   *   __uint64_t __x[29]; // General purpose registers x0-x28
   *   __uint64_t __fp;    // Frame pointer x29
   *   __uint64_t __lr;    // Link register x30
   *   __uint64_t __sp;    // Stack pointer x31
   *   __uint64_t __pc;    // Program counter
   *   __uint32_t __cpsr;  // Current program status register
   *   __uint32_t __pad;   // Same size for 32-bit or 64-bit clients
   * };

   * _STRUCT_ARM_NEON_STATE64        __ns ->
   * {
   *   __uint128_t __v[32];
   *   __uint32_t  __fpsr;
   *   __uint32_t  __fpcr;
   * };
   */

  ucontext_t *const uc = (ucontext_t *)ucontext_opaque;
  u64 target_address = (u64)info->si_addr; // == uc->uc_mcontext->__es.__far;
  fox::MemoryTable *const mem = s_fault_cpu->m_phys_mem;

  if (target_address < u64(mem->root()) ||
      target_address >= u64(mem->root() + 0x100000000u)) {
    /* Treat as a real segfault. */
    printf("Real segfault, %p\n", info->si_addr);

    /* TODO Output a stack trace and/or dump of the JIT routine that was
     *      running */
    abort();
  }
  target_address -= u64(mem->root());

  /* Use primary, not fast path, next time. */
  s_fault_block->add_flag(BasicBlock::DISABLE_FASTMEM);

  const u32 instruction = *(u32 *)(uc->uc_mcontext->__ss.__pc);

  /* Loads ALWAYS uses LDR{B,H} <Wt>, [<Xmem_base>, <Wguest_address>]
   * Load8/16/32 all have the same instruction mask, just different bits for
   * size. */
  const u32 load_instruction_mask = 0b11111111111000001111110000000000;

  const u32 load32_instruction_bits = 0b10111000011000000100100000000000;
  const u32 load16_instruction_bits = 0b01111000011000000100100000000000;
  const u32 load8_instruction_bits = 0b00111000011000000100100000000000;

  const u32 output_host_register_index = instruction & 0b11111;
  if ((instruction & load_instruction_mask) == load32_instruction_bits) {
    const u32 load_value = s_fault_cpu->mem_read<u32>(target_address);
    uc->uc_mcontext->__ss.__x[output_host_register_index] = 0xFFFFFFFF & load_value;
  } else if ((instruction & load_instruction_mask) == load16_instruction_bits) {
    const u16 load_value = s_fault_cpu->mem_read<u16>(target_address);
    uc->uc_mcontext->__ss.__x[output_host_register_index] = 0xFFFF & load_value;
  } else if ((instruction & load_instruction_mask) == load8_instruction_bits) {
    const u8 load_value = s_fault_cpu->mem_read<u8>(target_address);
    uc->uc_mcontext->__ss.__x[output_host_register_index] = 0xFF & load_value;
  } else {
    printf("Instruction 0x%08X\n", instruction);
    assert(false && "Unsupported JIT load instruction");
  }

  /* Proceed past the faulting instruction */
  const size_t instruction_size = 4;
  uc->uc_mcontext->__ss.__pc += instruction_size;
}
#endif

#if defined(__x86_64__)
void
SH4::jit_handle_fault(int signo, siginfo_t *info, void *ucontext_opaque)
{
  if (s_fault_block == NULL) {
    /* Treat as a real segfault. */
    signal(SIGSEGV, SIG_DFL);
    return;
  }

  ucontext_t *const uc = (ucontext_t *)ucontext_opaque;
  const u8 *const pc = (u8 *)uc->uc_mcontext.gregs[REG_RIP];
  fox::MemoryTable *const mem = s_fault_cpu->m_phys_mem;

  u64 target_address = u64(info->si_addr);
  if (target_address < u64(mem->root()) ||
      target_address >= u64(mem->root() + 0x100000000u)) {
    /* Treat as a real segfault. */
    printf("Real segfault, %p\n", info->si_addr);
    signal(SIGSEGV, SIG_DFL);
    return;
  }
  target_address -= u64(mem->root());

  /* Use primary, not fast path, next time. */
  s_fault_block->add_flag(BasicBlock::DISABLE_FASTMEM);

  /* Decode the faulting instruction to determine the output register. Only
   * normal mov instructions of 1, 2, 4, 8 bytes are supported. */
  u8 destination;
  size_t bytes = 0;
  unsigned pc_bytes = 0;
  bool is_valid = false;
  {
    const u8 *decode = pc;

    /* Optional size prefix */
    bool has_size_prefix = false;
    if (decode[0] == 0x66) {
      has_size_prefix = true;
      ++decode;
      ++pc_bytes;
    }

    /* Optional REX prefix */
    u8 rex = 0;
    if ((decode[0] & 0xf0) == 0x40) {
      rex = decode[0] & 0x0f;
      ++decode;
      ++pc_bytes;
    }

    u8 modrm = 0;
    if (decode[0] == 0x8A && !has_size_prefix) {
      /* 8 bit transfer */
      is_valid = true;
      modrm = decode[1];
      bytes = 1;
      pc_bytes += 2;
    } else if (decode[0] == 0x8B) {
      /* 16, 32, or 64 bit transfer */
      if (has_size_prefix) {
        is_valid = true;
        modrm = decode[1];
        bytes = 2;
        pc_bytes += 2;
      } else if (!(rex & 0x08)) {
        is_valid = true;
        modrm = decode[1];
        bytes = 4;
        pc_bytes += 2;
      } else {
        is_valid = true;
        modrm = decode[1];
        bytes = 8;
        pc_bytes += 2;
      }
    }

    /* Destination register is formed by 3 bits from ModRM and 1 bit from REX */
    destination = ((modrm >> 3) & 0x7);
    destination |= (rex << 1) & 0x8;

    /* Determine total encoding size for the instruction. */
    /* XXX Hardcoded for now! */
    pc_bytes += 1;
  }

  if (!is_valid) {
    printf("Segfault in basic block on unexpected instruction\n");
    signal(SIGSEGV, SIG_DFL);
    return;
  }

  /* Find destination register in the return context. */
  using fox::codegen::amd64::GeneralRegister;
  void *output = nullptr;
  switch (GeneralRegister(destination)) {
    case GeneralRegister::RAX:
      output = &uc->uc_mcontext.gregs[REG_RAX];
      break;
    case GeneralRegister::RCX:
      output = &uc->uc_mcontext.gregs[REG_RCX];
      break;
    case GeneralRegister::RDX:
      output = &uc->uc_mcontext.gregs[REG_RDX];
      break;
    case GeneralRegister::RBX:
      output = &uc->uc_mcontext.gregs[REG_RBX];
      break;
    case GeneralRegister::RSP:
      output = &uc->uc_mcontext.gregs[REG_RSP];
      break;
    case GeneralRegister::RBP:
      output = &uc->uc_mcontext.gregs[REG_RBP];
      break;
    case GeneralRegister::RSI:
      output = &uc->uc_mcontext.gregs[REG_RSI];
      break;
    case GeneralRegister::RDI:
      output = &uc->uc_mcontext.gregs[REG_RDI];
      break;
    case GeneralRegister::R8:
      output = &uc->uc_mcontext.gregs[REG_R8];
      break;
    case GeneralRegister::R9:
      output = &uc->uc_mcontext.gregs[REG_R9];
      break;
    case GeneralRegister::R10:
      output = &uc->uc_mcontext.gregs[REG_R10];
      break;
    case GeneralRegister::R11:
      output = &uc->uc_mcontext.gregs[REG_R11];
      break;
    case GeneralRegister::R12:
      output = &uc->uc_mcontext.gregs[REG_R12];
      break;
    case GeneralRegister::R13:
      output = &uc->uc_mcontext.gregs[REG_R13];
      break;
    case GeneralRegister::R14:
      output = &uc->uc_mcontext.gregs[REG_R14];
      break;
    case GeneralRegister::R15:
      output = &uc->uc_mcontext.gregs[REG_R15];
      break;
    default:
      assert(false);
  }

  /* Perform intended read operation. */
  switch (bytes) {
    case 1:
      *(u8 *)output = s_fault_cpu->mem_read<u8>(target_address);
      break;
    case 2:
      *(u16 *)output = s_fault_cpu->mem_read<u16>(target_address);
      break;
    case 4:
      *(u64 *)output = s_fault_cpu->mem_read<u32>(target_address);
      break;
    case 8:
      *(u64 *)output = s_fault_cpu->mem_read<u64>(target_address);
      break;
    default: /* Uh-oh */
      printf("Segfault in basic block on unexpected instruction\n");
      signal(SIGSEGV, SIG_DFL);
      return;
  }

  uc->uc_mcontext.gregs[REG_RIP] += pc_bytes;
}
#endif

#if 0 /* TODO: Restore support for win64 */
LONG CALLBACK
SH4::jit_handle_fault(PEXCEPTION_POINTERS ex_info)
{
  assert(s_fault_block != NULL);

  DWORD code = ex_info->ExceptionRecord->ExceptionCode;
  if (code != STATUS_ACCESS_VIOLATION && code != STATUS_ILLEGAL_INSTRUCTION) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  fox::MemoryTable *const mem = s_fault_cpu->m_phys_mem;
  assert(ex_info->ContextRecord->Rax >= u64(mem->root()));
  assert(ex_info->ContextRecord->Rax < u64(mem->root()) + 0x100000000u);

  const u8 *const pc = (u8 *) ex_info->ContextRecord->Rip;
  const u64 target_address = ex_info->ContextRecord->Rax - mem->root();

  /* Use primary, not fast path, next time. */
  s_fault_block->add_flag(BasicBlock::DISABLE_FASTMEM);

  switch (pc[0]) {
    case 0x8a: /* mov al, BYTE PTR [rax] */
               /* 0x8a 0x00 */
      ex_info->ContextRecord->Rax = s_fault_cpu->mem_read<u8>(target_address);
      ex_info->ContextRecord->Rip += 2;
      break;

    case 0x66: /* mov ax, WORD PTR [rax] */
               /* 0x66 0x8b 0x00 */
      ex_info->ContextRecord->Rax = s_fault_cpu->mem_read<u16>(target_address);
      ex_info->ContextRecord->Rip += 3;
      break;

    case 0x8b: /* mov eax, DWORD PTR [rax] */
               /* 0x8b 0x00 */
      ex_info->ContextRecord->Rax = s_fault_cpu->mem_read<u32>(target_address);
      ex_info->ContextRecord->Rip += 2;
      break;

    case 0x48: /* mov rax, QWORD PTR [rax] */
               /* 0x48 0x8b 0x00 */
      ex_info->ContextRecord->Rax = s_fault_cpu->mem_read<u64>(target_address);
      ex_info->ContextRecord->Rip += 3;
      break;

    default: /* Uh-oh */
      printf("Got a real segfault!\n");
      exit(0);
  }

  return EXCEPTION_CONTINUE_EXECUTION;
}
#endif

fox::Value
SH4::guest_register_read(unsigned index, size_t bytes)
{
  assert(index < SH4Assembler::_RegisterCount);

  if (index < SH4Assembler::SP0) {
    assert(bytes == 4);
    fox::Value value;
    memcpy(&value.u32_value, (u32 *)&regs + index, sizeof(u32));
    return value;
  }

  index -= SH4Assembler::SP0;

  /* The guest interface assumes SP0 / DP0 always refers to the current bank,
   * but our Registers struct stores the two banks directly as bank0/bank1. */
  if (bytes == 4) {
    fox::Value value;
    memcpy(&value.u32_value, (u32 *)&FPU + index, 4);
    return value;
  } else {
    /* Non-FP registers in FPU are only 32 bits */
    assert(bytes == 8 && index < 32);
    fox::Value value;
    memcpy(&value.u64_value, (u32 *)&FPU + index, 8);
    return value;
  }
}

void
SH4::guest_register_write(unsigned index, size_t bytes, const fox::Value value)
{
  assert(index < SH4Assembler::_RegisterCount);

  if (index < SH4Assembler::SP0) {
    memcpy((u32 *)&regs + index, &value.u32_value, sizeof(u32));
    return;
  }

  index -= SH4Assembler::SP0;

  /* The guest interface assumes SP0 / DP0 always refers to the current bank,
   * but our Registers struct stores the two banks directly as bank0/bank1. */
  if (bytes == 4) {
    memcpy((u32 *)&FPU + index, &value.u32_value, 4);
  } else {
    /* Non-FP registers in FPU are only 32 bits */
    assert(bytes == 8 && index < 32);
    memcpy((u32 *)&FPU + index, &value.u64_value, 8);
  }
}

fox::Value
SH4::guest_load(const u32 address, const size_t bytes)
{
  fox::Value result;
  switch (bytes) {
    case 1:
      result.u8_value = mem_read<u8>(address);
      break;
    case 2:
      result.u16_value = mem_read<u16>(address);
      break;
    case 4:
      result.u32_value = mem_read<u32>(address);
      break;
    case 8:
      result.u64_value = mem_read<u64>(address);
      break;
    default:
      assert(false);
  }

  return result;
}

void
SH4::guest_store(const u32 address, const size_t bytes, const fox::Value value)
{
  switch (bytes) {
    case 1:
      mem_write<u8>(address, value.u8_value);
      break;
    case 2:
      mem_write<u16>(address, value.u16_value);
      break;
    case 4:
      mem_write<u32>(address, value.u32_value);
      break;
    case 8:
      mem_write<u64>(address, value.u64_value);
      break;
    default:
      assert(false);
  }
}

fox::Value
SH4::interpreter_upcall(fox::Guest *const cpu_in,
                        const fox::Value opcode_in,
                        const fox::Value pc_in)
{
  SH4 *const cpu = static_cast<SH4 *>(cpu_in);
  const u32 opcode_id = (opcode_in.u64_value >> 32);
  const u32 opcode_raw = (opcode_in.u64_value & 0xFFFFu);
  const u32 PC = pc_in.u32_value;
  const Opcode &opcode = opcode_table[opcode_id];
  const bool delay_slot = cpu->in_delay_slot();

  cpu->m_executed_branch = false;

  // assert(regs.PC == PC); /* XXX Not reliable for certain upcalls. */
  (cpu->*opcode.execute)(opcode_raw);

  if (cpu->m_executed_branch) {
    assert(cpu->regs.PC == PC);
    if (cpu->m_branch_target != 0xFFFFFFFFu) {
      cpu->regs.PC = PC + sizeof(u16);
    }

    return fox::Value { .u64_value = 1lu };
  }

  if (delay_slot && cpu->m_branch_target != 0xFFFFFFFFu) {
    /* Previous instruction was a branch and we just ran the delay slot */
    cpu->regs.PC = cpu->m_branch_target;
    cpu->m_branch_target = 0xFFFFFFFFu;
  } else {
    cpu->regs.PC = PC + sizeof(u16);
  }

  return fox::Value { .u64_value = 0lu };
}

fox::Value
SH4::gpr_maybe_swap(fox::Guest *const cpu_in, const fox::Value do_swap)
{
  SH4 *const cpu = static_cast<SH4 *>(cpu_in);
  if (do_swap.bool_value) {
    cpu->gpr_swap_bank();
  }
  return fox::Value { .u64_value = 0 };
}

fox::Value
SH4::fpu_maybe_swap(fox::Guest *const cpu_in, const fox::Value do_swap)
{
  SH4 *const cpu = static_cast<SH4 *>(cpu_in);
  if (do_swap.bool_value) {
    cpu->FPU.swap_bank();
  }
  return fox::Value { .u64_value = 0 };
}

}
