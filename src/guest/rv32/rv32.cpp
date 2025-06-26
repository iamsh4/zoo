
#include "rv32.h"
#include "rv32_jit.h"
#include "fox/ir_operand.h"
#include "fox/bytecode/compiler.h"

namespace guest::rv32 {

RV32::RV32(fox::MemoryTable *mem) : m_mem(mem), m_jit_cache(mem)
{
  m_registers[Registers::REG_X_START] = 0; // x0 == 0
}

RV32::~RV32() {}

void
RV32::reset()
{
  m_registers[Registers::REG_PC] = m_reset_address;
}

void
RV32::set_reset_address(u32 address)
{
  m_reset_address = address;
}

u32 *
RV32::registers()
{
  return m_registers;
}

template<typename T>
T
RV32::mem_read(u32 address)
{
  return m_mem->read<T>(address);
}

template u8 RV32::mem_read<u8>(u32 address);
template u16 RV32::mem_read<u16>(u32 address);
template u32 RV32::mem_read<u32>(u32 address);

template<typename T>
void
RV32::mem_write(u32 address, T value)
{
  m_mem->write<T>(address, value);
}

fox::Value
RV32::guest_register_read(unsigned index, size_t bytes)
{
  assert(index < Registers::__NUM_REGISTERS);
  assert(bytes == 4);

  return fox::Value { .u32_value = m_registers[index] };
}

void
RV32::guest_register_write(unsigned index, size_t bytes, fox::Value value)
{
  assert(index < Registers::__NUM_REGISTERS);
  assert(bytes == 4);

  if (index == 0) {
    return;
  }

  // printf("Writing x%d <- 0x%08x\n", index, value.u32_value);

  m_registers[index] = value.u32_value;
}

fox::Value
RV32::guest_load(u32 address, size_t bytes)
{
  if (address & (bytes - 1)) {
    throw "Unaligned load";
  }

  if (bytes == 1)
    return fox::Value { .u8_value = mem_read<u8>(address) };
  else if (bytes == 2)
    return fox::Value { .u16_value = mem_read<u16>(address) };
  else if (bytes == 4)
    return fox::Value { .u32_value = mem_read<u32>(address) };
  else
    assert(false);
  throw std::runtime_error("Unhandled RV32 guest_load");
}

void
RV32::guest_store(u32 address, size_t bytes, fox::Value value)
{
  if (address >= 0x40000000 && address < 0x80000000) {
    // Cached region cannot be written to.
    // TODO: Remove once cache invalidation, which currently depends on this
    //       has moved to using a specific instruction.
    return;
  }

  if (address & (bytes - 1)) {
    throw "Unaligned store";
  }

  if (bytes == 1)
    mem_write<u8>(address, value.u8_value);
  else if (bytes == 2)
    mem_write<u16>(address, value.u16_value);
  else if (bytes == 4)
    mem_write<u32>(address, value.u32_value);
  else
    assert(false);
}

u32
RV32::step()
{
#if 1
  assert(m_registers[Registers::REG_PC] % 4 == 0);

  // Execute instruction
  u64 cycles;
  try {
    // Is interrupt pending? If so, enter handler
    // check_enter_irq(); // XXX

    // Decode next instruction
    const u32 pc = m_registers[Registers::REG_PC];
    fox::jit::CacheEntry *entry = m_jit_cache.lookup(pc);
    if (!entry) {
      const u32 next_unit_start = m_jit_cache.trailing_unit(pc);
      fox::ir::ExecutionUnit eu = m_assembler.assemble(this, pc, next_unit_start);
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
    throw;
  }

  return cycles;

#else
  assert(m_registers[Registers::REG_PC] % 4 == 0);
  const u32 instruction_word = mem_read<u32>(m_registers[Registers::REG_PC]);
  const Encoding encoded { .raw = instruction_word,
                           .pc = m_registers[Registers::REG_PC] };

  // printf("------\n");
  // printf("PC: 0x%08x -> 0x%08x\n", encoded.pc, encoded.raw);
  // for(u32 i=0; i<16; ++i){
  //   printf("x%u: 0x%08x ", i, m_registers[i]);
  // }
  // printf("\n");

  bool did_decode = false;
  for (auto &isa : m_instruction_sets) {
    const Decoding decoding = isa->decode(encoded);

    if (decoding.instruction == Instruction::__NOT_DECODED__) {
      continue;
    }

    // printf("decoded 0x%08x -> ins %u\n", encoded.pc, decoding.instruction);

    did_decode = true;

    isa->assemble(&m_assembler, decoding);
    m_assembler.exit(fox::ir::Operand::constant<bool>(true),
                     fox::ir::Operand::constant<u64>(0));

    fox::bytecode::Compiler bytecode_compiler;
    auto x = m_assembler.assemble(this, encoded.pc, 1);
    // x.debug_print();
    auto bytecode = bytecode_compiler.compile(std::move(x));
    bytecode->execute(this, (void *)m_mem->root(), (void *)m_registers);

    const bool is_branch = decoding.flags & u32(Decoding::Flag::ConditionalJump) ||
                           decoding.flags & u32(Decoding::Flag::UnconditionalJump);
    if (!is_branch) {
      m_registers[Registers::REG_PC] += 4;
    }

    break;
  }

  // assert(did_decode && "Failed to decode rv32i");
  if (!did_decode) {
    throw "Failed to decode rv32i";
  }

  const u32 CYCLES_PER_INSTRUCTION = 1;
  return CYCLES_PER_INSTRUCTION;
#endif
}
}
