#include <unistd.h>
#include <algorithm>
#include <fmt/core.h>

#include "guest/r3000/r3000.h"
#include "guest/r3000/r3000_ir.h"
#include "guest/r3000/r3000_jit.h"

#include "fox/ir/optimizer.h"

#include "shared/bitmanip.h"
#include "shared/error.h"
#include "shared/profiling.h"

namespace guest::r3000 {

static_assert(Registers::NUM_REGS == (32 + 3 +     /* General-purpose registers */
                                      32 * 2 * 4 + /* 32*2*4 COP registers */
                                      4            /* 4 virtual pipeline registers */
                                      ));

constexpr const char *register_names[] = {
  "R0",      "R1",      "R2",      "R3",      "R4",      "R5",      "R6",      "R7",
  "R8",      "R9",      "R10",     "R11",     "R12",     "R13",     "R14",     "R15",
  "R16",     "R17",     "R18",     "R19",     "R20",     "R21",     "R22",     "R23",
  "R24",     "R25",     "R26",     "R27",     "R28",     "R29",     "R30",     "R31",
  "HI",      "LO",      "PC",

  "cop0r0",  "cop0r1",  "cop0r2",  "cop0r3",  "cop0r4",  "cop0r5",  "cop0r6",  "cop0r7",
  "cop0r8",  "cop0r9",  "cop0r10", "cop0r11", "cop0r12", "cop0r13", "cop0r14", "cop0r15",
  "cop0r16", "cop0r17", "cop0r18", "cop0r19", "cop0r20", "cop0r21", "cop0r22", "cop0r23",
  "cop0r24", "cop0r25", "cop0r26", "cop0r27", "cop0r28", "cop0r29", "cop0r30", "cop0r31",
  "cop0r32", "cop0r33", "cop0r34", "cop0r35", "cop0r36", "cop0r37", "cop0r38", "cop0r39",
  "cop0r40", "cop0r41", "cop0r42", "cop0r43", "cop0r44", "cop0r45", "cop0r46", "cop0r47",
  "cop0r48", "cop0r49", "cop0r50", "cop0r51", "cop0r52", "cop0r53", "cop0r54", "cop0r55",
  "cop0r56", "cop0r57", "cop0r58", "cop0r59", "cop0r60", "cop0r61", "cop0r62", "cop0r63",

  "cop1r0",  "cop1r1",  "cop1r2",  "cop1r3",  "cop1r4",  "cop1r5",  "cop1r6",  "cop1r7",
  "cop1r8",  "cop1r9",  "cop1r10", "cop1r11", "cop1r12", "cop1r13", "cop1r14", "cop1r15",
  "cop1r16", "cop1r17", "cop1r18", "cop1r19", "cop1r20", "cop1r21", "cop1r22", "cop1r23",
  "cop1r24", "cop1r25", "cop1r26", "cop1r27", "cop1r28", "cop1r29", "cop1r30", "cop1r31",
  "cop1r32", "cop1r33", "cop1r34", "cop1r35", "cop1r36", "cop1r37", "cop1r38", "cop1r39",
  "cop1r40", "cop1r41", "cop1r42", "cop1r43", "cop1r44", "cop1r45", "cop1r46", "cop1r47",
  "cop1r48", "cop1r49", "cop1r50", "cop1r51", "cop1r52", "cop1r53", "cop1r54", "cop1r55",
  "cop1r56", "cop1r57", "cop1r58", "cop1r59", "cop1r60", "cop1r61", "cop1r62", "cop1r63",

  "cop2r0",  "cop2r1",  "cop2r2",  "cop2r3",  "cop2r4",  "cop2r5",  "cop2r6",  "cop2r7",
  "cop2r8",  "cop2r9",  "cop2r10", "cop2r11", "cop2r12", "cop2r13", "cop2r14", "cop2r15",
  "cop2r16", "cop2r17", "cop2r18", "cop2r19", "cop2r20", "cop2r21", "cop2r22", "cop2r23",
  "cop2r24", "cop2r25", "cop2r26", "cop2r27", "cop2r28", "cop2r29", "cop2r30", "cop2r31",
  "cop2r32", "cop2r33", "cop2r34", "cop2r35", "cop2r36", "cop2r37", "cop2r38", "cop2r39",
  "cop2r40", "cop2r41", "cop2r42", "cop2r43", "cop2r44", "cop2r45", "cop2r46", "cop2r47",
  "cop2r48", "cop2r49", "cop2r50", "cop2r51", "cop2r52", "cop2r53", "cop2r54", "cop2r55",
  "cop2r56", "cop2r57", "cop2r58", "cop2r59", "cop2r60", "cop2r61", "cop2r62", "cop2r63",

  "cop3r0",  "cop3r1",  "cop3r2",  "cop3r3",  "cop3r4",  "cop3r5",  "cop3r6",  "cop3r7",
  "cop3r8",  "cop3r9",  "cop3r10", "cop3r11", "cop3r12", "cop3r13", "cop3r14", "cop3r15",
  "cop3r16", "cop3r17", "cop3r18", "cop3r19", "cop3r20", "cop3r21", "cop3r22", "cop3r23",
  "cop3r24", "cop3r25", "cop3r26", "cop3r27", "cop3r28", "cop3r29", "cop3r30", "cop3r31",
  "cop3r32", "cop3r33", "cop3r34", "cop3r35", "cop3r36", "cop3r37", "cop3r38", "cop3r39",
  "cop3r40", "cop3r41", "cop3r42", "cop3r43", "cop3r44", "cop3r45", "cop3r46", "cop3r47",
  "cop3r48", "cop3r49", "cop3r50", "cop3r51", "cop3r52", "cop3r53", "cop3r54", "cop3r55",
  "cop3r56", "cop3r57", "cop3r58", "cop3r59", "cop3r60", "cop3r61", "cop3r62", "cop3r63",
};

R3000::R3000(fox::MemoryTable *memory_table)
  : m_mem(memory_table),
    m_jit_cache(memory_table)
{
  reset();
}

void
R3000::reset()
{
  memset(m_regs, 0, sizeof(u32) * Registers::NUM_REGS);
  m_regs[Registers::PC] = 0xBFC00000;
  m_regs[Registers::BRANCH_DELAY_ADDRESS] = INVALID_BRANCH_DELAY_ADDRESS;
  m_regs[Registers::DELAYED_WRITEBACK_REG_INDEX] = INVALID_WRITEBACK_INDEX;
}

bool
R3000::has_breakpoint(u32 address)
{
  return std::find(exec_breakpoints.begin(), exec_breakpoints.end(), address) !=
         exec_breakpoints.end();
}

void
R3000::add_mem_write_watch(u32 address)
{
  write_breakpoints.insert(address);
}

void
R3000::remove_mem_write_watch(u32 address)
{
  write_breakpoints.erase(address);
}

void
R3000::write_watch_list(std::vector<u32> *out)
{
  out->clear();
  for (u32 el : write_breakpoints) {
    out->push_back(el);
  }
}

u32
R3000::step_instruction()
{
  // Execute instruction
  u64 cycles;
  try {
    // Is interrupt pending? If so, enter handler
    check_enter_irq();

    // Decode next instruction
    const u32 pc = m_regs[Registers::PC];
    fox::jit::CacheEntry *entry = m_jit_cache.lookup(pc);
    if (!entry) {
      /* Cache will maintain a reference on it until we call garbage_collect(). */

      // TODO : Create assembler once, avoid recreating this handler function
      // TODO : This is tied to PS1, but should be easy to make generic
      // TODO : This connection between assembler and coprocessor is awful. Clean up.
      Assembler assembler;
      assembler.set_coprocessor_assembler(2, m_coprocessors[2]);
      m_coprocessors[2]->set_assembler(&assembler);

      fox::ir::ExecutionUnit eu = assembler.assemble(this, pc, 1);
      fox::ref<fox::jit::CacheEntry> ref_entry = new BasicBlock(pc, 4, std::move(eu));
      m_jit_cache.insert(ref_entry.get());
      entry = ref_entry.get();
    }

    BasicBlock *bb = (BasicBlock *)entry;
    cycles = bb->execute(this, 1000);
    m_jit_cache.garbage_collect();
  } catch (std::exception &e) {
    printf("Exception during step_instruction execution...\n");
    printf(" - %s\n", e.what());

    for (u32 i = 0; i < 40; ++i) {
      printf("REG[%02u]=0x%08x ", i, m_regs[i]);
      if ((i % 8) == 7) {
        printf("\n");
      }
    }

    throw;
  }

  // XXX : Handle the case where this instruction generated an exception

  return cycles;
}

u32
R3000::step_block()
{
  assert(false && "Not yet implemented");
#if 0
  Assembler assembler;
  auto block = assembler.assemble(*m_mem, m_regs[Registers::PC], 1u);

  block = fox::ir::optimize::ConstantPropagation().execute(std::move(block));
  block = fox::ir::optimize::DeadCodeElimination().execute(std::move(block));
  printf("Block AFTER OPTIMIZATION\n");
  block.debug_print();

  const bool run_native = false;
  std::unique_ptr<fox::jit::Routine> routine;
  if (run_native) {

    fox::codegen::arm64::Compiler compiler;
    compiler.set_register_address_cb([](unsigned index) { return index; });

    auto native_routine = compiler.compile(std::move(block));
    native_routine->prepare(true);

    printf("arm64 disassembly\n");
    native_routine->debug_print();
    printf("\n");

    routine.reset(native_routine.release());

  } else {
    fox::bytecode::Compiler compiler;
    routine = compiler.compile(std::move(block));
  }
  routine->execute(this, (void *)m_mem->root(), (void *)m_regs);

#endif
  return 0;
}

const char *
R3000::get_register_name(unsigned index, bool use_register_mnemonics)
{
  if (use_register_mnemonics) {
    static const char *names[] = { "r0", "at", "v0", "v1", "a0", "a1", "a2", "a3",
                                   "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
                                   "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
                                   "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra" };

    return names[index];
  } else {
    static const char *names[] = {
      "r0",  "r1",  "r2",  "r3",  "r4",  "r5",  "r6",  "r7",  "r8",  "r9",  "r10",
      "r11", "r12", "r13", "r14", "r15", "r16", "r17", "r18", "r19", "r20", "r21",
      "r22", "r23", "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31",
    };
    return names[index];
  }
}

void
R3000::dump()
{
  for (u32 i = 0; i < Registers::COP0_DATA; ++i) {
    if (i < 32)
      printf("%3s=%08x ", get_register_name(i, true), m_regs[i]);
    else
      printf("%3s=%08x ", register_names[i], m_regs[i]);
    if (i % 8 == 7)
      printf("\n");
  }
  printf("\n");
}

fox::Value
R3000::guest_register_read(unsigned index, size_t bytes)
{
  assert(index < Registers::NUM_REGS);
  assert(bytes == 4);

  return fox::Value { .u32_value = m_regs[index] };
}

void
R3000::guest_register_write(unsigned index, size_t bytes, fox::Value value)
{
  assert(index < Registers::NUM_REGS);
  assert(bytes == 4);
  assert(index != Registers::R0 && "R3000: IR should never write to R0");

  m_regs[index] = value.u32_value;
}

fox::Value
R3000::guest_load(u32 address, size_t bytes)
{
  if (bytes == 1)
    return fox::Value { .u8_value = mem_read<u8>(address) };
  else if (bytes == 2)
    return fox::Value { .u16_value = mem_read<u16>(address) };
  else if (bytes == 4)
    return fox::Value { .u32_value = mem_read<u32>(address) };
  else
    _check(false, "Invalid guest_load size");
}

void
R3000::guest_store(u32 address, size_t bytes, fox::Value value)
{
  if (bytes == 1)
    mem_write<u8>(address, value.u8_value);
  else if (bytes == 2)
    mem_write<u16>(address, value.u16_value);
  else if (bytes == 4)
    mem_write<u32>(address, value.u32_value);
  else
    _check(false, "Invalid guest_write size");
}

std::pair<R3000::AddressType, u32>
R3000::mem_region(u32 virtual_address, bool is_kernel_mode) const
{
  if (!is_kernel_mode) {
    // We have more work to do to support this.
    throw std::runtime_error("mem_region check in non-kernel mode.");
  }

  // KUSEG : 0x0000'0000 - 0x7fff'ffff | Cached | MMU
  // KSEG0 : 0x8000'0000 - 0x9fff'ffff | Cached | ...
  // KSEG1 : 0xa000'0000 - 0xbfff'ffff | ...    | ...
  // KSEG2 : 0xc000'0000 - 0xffff'ffff | Cached | MMU (Kernel mode-only)

  const u32 segment_512mb = virtual_address >> 29;
  const u32 mask[8] = {
    // KUSEG - 2GiB
    0xffff'ffff,
    0xffff'ffff,
    0xffff'ffff,
    0xffff'ffff,
    // KSEG0 - 512MiB
    0x7fff'ffff,
    // KSEG1 - 512MiB
    0x1fff'ffff,
    // KSEG2 - 1GiB
    0xffff'ffff,
    0xffff'ffff,
  };

  u32 masked = mask[segment_512mb] & virtual_address;

  const u32 SIZE_2MB = 2 * 1024 * 1024;
  const u32 SIZE_8MB = 8 * 1024 * 1024;
  if (masked < SIZE_8MB) {
    masked &= SIZE_2MB - 1;
  }

  if (masked < 0xfffe'0000) {
    return { AddressType::Physical, masked };
  } else {
    return { AddressType::Register, masked };
  }
}

u32
R3000::fetch_instruction(u32 address)
{
  // XXX : Actually pass in kernel mode
  const auto region = mem_region(address, true);

  if (region.first == AddressType::Physical) {
    return m_mem->read<u32>(region.second);
  } else {
    throw std::runtime_error(
      fmt::format("Invalid memory region for fetch, virtual address 0x{:08x}", address));
  }
}

template<typename T>
T
R3000::mem_read(u32 address)
{
  ProfileZone;
  address &= ~(sizeof(T) - 1);

  // XXX : Actually pass in kernel mode
  const auto region = mem_region(address, true);

  if (region.first == AddressType::Physical) {
    // TODO : Unknown if things like CPU MMIO Registers are affected by this SR bit.
    if (m_regs[Registers::SR] & 0x10000) {
      printf("Ignoring load while cache is isolated\n");
      // XXX : Isolated cache read/write go to dcache
      throw std::runtime_error("unhandled load while cache is isolated");
    } else {
      const T val = m_mem->read<T>(region.second);
      // printf("mem_read<%lu>(0x%08x) -> 0x%x\n", sizeof(T), region.second, val);
      return val;
    }
  } else {
    throw std::runtime_error(
      fmt::format("Invalid memory region for read, virtual address 0x{:08x}", address));
  }
}

void
R3000::breakpoint_add(u32 address)
{
  if (!has_breakpoint(address)) {
    exec_breakpoints.push_back(address);
  }
}

void
R3000::breakpoint_remove(u32 address)
{
  if (auto it = std::remove(exec_breakpoints.begin(), exec_breakpoints.end(), address);
      it != exec_breakpoints.end()) {
    exec_breakpoints.erase(it, exec_breakpoints.end());
  }
}

void
R3000::breakpoint_list(std::vector<u32> *results)
{
  if (results) {
    *results = exec_breakpoints;
  }
}

template<typename T>
void
R3000::mem_write(u32 address, T value)
{
  ProfileZone;
  address &= ~(sizeof(T) - 1);

  if (auto it = write_breakpoints.find(address); it != write_breakpoints.end()) {
    printf("r3000: Found write val=0x%x -> 0x%08x (pc=0x%08x)\n",
           value,
           *it,
           m_regs[Registers::PC]);
    m_halted = true;
  }

  // XXX : Actually pass in kernel mode
  const auto region = mem_region(address, true);

  if (region.first == AddressType::Physical) {
    // TODO : Unknown if things like CPU MMIO Registers are affected by this SR bit.
    if (m_regs[Registers::SR] & 0x10000) {
      // printf("Ignoring store while cache is isolated\n");
    } else {
      // printf("mem_write<%lu>(0x%08x) <- 0x%x\n", sizeof(T), region.second, value);
      m_mem->write<T>(region.second, value);
    }
  } else if (region.first == AddressType::Register) {
    // TODO
    printf("XXX : Write CACHE_CONTROL 0%08x < 0x%08x\n", address, value);
  } else {
    throw std::runtime_error(
      fmt::format("Invalid memory region for write, virtual address 0x{:08x}", address));
  }
}

Registers::SR_Bits &
R3000::SR()
{
  static_assert(sizeof(Registers::SR_Bits) == sizeof(m_regs[Registers::SR]));
  return *reinterpret_cast<Registers::SR_Bits *>(&m_regs[Registers::SR]);
}

void
R3000::set_external_irq(bool new_state)
{
  external_irq = new_state;
}

void
R3000::check_enter_irq()
{
  auto *cause = reinterpret_cast<Registers::CAUSE_Bits *>(&m_regs[Registers::CAUSE]);
  auto *sr = reinterpret_cast<Registers::SR_Bits *>(&m_regs[Registers::SR]);

  /////////////////////////////////////////////////////////////

  // Because the external IRQ line is not latched, we need to actually check it's current
  // value In our implementation, this is 'remembered' and an external system needs to set
  // the value of this line appropriately. This way, we'll see when it's low/high on each
  // check.
  const u32 cause_with_external_state = cause->raw | (u32(external_irq) << 10);
  const bool pending = (cause_with_external_state & sr->raw) & 0x700;

  // If interrupts are enabled and one is pending, we need to enter the handler.
  const bool should_enter_irq = sr->IEc && pending;
  if (!should_enter_irq) {
    return;
  }

  // Confirmed, we need to enter exception handler.
  printf("r3000: entering irq exception handler\n");

  // NOTE: This should match our `exception` IR function

  // Exception handler is determined by the BEV bit.
  const u32 handler = sr->BEV ? 0xbfc0'0180 : 0x8000'0080;

  // 'push' the interrupt enable/mode bits to the left by two. These form a kind of 3-deep
  // 'stack'. Software must handle the case of >3 deep exceptions.
  sr->raw = (sr->raw & ~0x3fu) | ((sr->raw << 2u) & 0x3fu);

  cause->raw &= ~0x7cu;
  cause->raw |= u32(Exceptions::Interrupt) << 2;

  if (m_regs[Registers::BRANCH_DELAY_ADDRESS] != INVALID_BRANCH_DELAY_ADDRESS) {
    cause->BD = 1;
    m_regs[Registers::EPC] = m_regs[Registers::PC] - 4;
    m_regs[Registers::BRANCH_DELAY_ADDRESS] = INVALID_BRANCH_DELAY_ADDRESS;
  } else {
    cause->BD = 0;
    m_regs[Registers::EPC] = m_regs[Registers::PC];
  }

  printf("irq exception handler @ 0x%08x\n", handler);
  m_regs[Registers::PC] = handler;
}

void
R3000::set_coprocessor(u32 index, Coprocessor *coprocessor)
{
  m_coprocessors[index] = coprocessor;
}

} // namespace guest::cpu
