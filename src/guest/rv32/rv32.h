#pragma once

#include "fox/guest.h"
#include "fox/memtable.h"
#include "fox/jit/cache.h"
#include "shared/types.h"
#include "./rv32_ir.h"

namespace guest::rv32 {

// https://github.com/riscv/riscv-isa-manual/releases/download/20240411/unpriv-isa-asciidoc.pdf

class RV32 : public fox::Guest {
public:
  class BasicBlock;

  RV32(fox::MemoryTable *mem);
  ~RV32();

  /*!
   * @brief Retrieve a read-only reference to the internal CPU state for
   *        inspection.
   */
  u32 *registers();

  /*!
   * @brief Perform a hard reset of the CPU core.
   */
  void reset();

  /*!
   * @brief Simulate a single instruction on the processor core.
   */
  u32 step();

  RV32Assembler &get_assembler();

  void set_reset_address(u32 address);

  template<class T>
  void add_instruction_set()
  {
    m_instruction_sets.push_back(std::make_unique<T>());
  }

  u32 PC() const
  {
    return m_registers[Registers::REG_PC];
  }

private:
  /*!
   * @brief Shared memory table with SH4, which includes this core's system
   *        RAM (referred to as Wave memory from SH4).
   */
  fox::MemoryTable *const m_mem;

  u32 m_registers[::guest::rv32::Registers::__NUM_REGISTERS];

  fox::jit::Cache m_jit_cache;

  struct CSR {
    // TODO
  };
  CSR m_csrs;

  /*!
   * @brief Read from a memory location from the viewpoint of the ARM
   *        core.
   */
  template<typename T>
  T mem_read(u32 address);

  /*!
   * @brief Write to a memory location from the viewpoint of the ARM
   *        core.
   */
  template<typename T>
  void mem_write(u32 address, T value);

  // ZZZ
  RV32Assembler m_assembler;
  friend class RV32Assembler;
  
  void assemble_block(u32 address);

  std::vector<std::unique_ptr<RV32InstructionSet>> m_instruction_sets;

  /* Loads and stores are always 1/2/4/8 bytes. */
  fox::Value guest_register_read(unsigned index, size_t bytes) final;
  void guest_register_write(unsigned index, size_t bytes, fox::Value value) final;
  fox::Value guest_load(u32 address, size_t bytes) final;
  void guest_store(u32 address, size_t bytes, fox::Value value) final;

  u32 m_reset_address = 0;
};

}
