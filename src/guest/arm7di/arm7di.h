#pragma once

#include "fox/jit/cache.h"
#include "fox/guest.h"
#include "fox/memtable.h"
#include "shared/types.h"
#include "guest/arm7di/arm7di_ir.h"
#include "guest/arm7di/arm7di_shared.h"

namespace guest::arm7di {

/* TODO clean this up */

/*!
 * @class apu::Arm7DI
 * @brief Implementation of an Arm7DI core, as implemented within the AICA
 *        audio chip.
 */
class Arm7DI : public fox::Guest {
public:
  /*!
   * @struct apu::Arm7DI::Registers
   * @brief Representation of the basic register state within the processor
   *        core.
   *
   * The normally addressable registers are R0 - R15. R15 is used as the
   * program counter register (PC). In all operating modes, R0-R7 and R15 are
   * shared. For the remaining registers, some operating modes have a dedicated
   * version while others are shared.
   *
   * User32:
   *   - Default mode; uses normal register bank
   * FIQ32:
   *   - R8-R14 are banked and renamed Rx_fiq
   * Supervisor32:
   *   - R13 and R14 are banked and renamed Rx_svc
   * Abort32:
   *   - R13 and R14 are banked and renamed Rx_abt
   * IRQ32:
   *   - R13 and R14 are banked and renamed Rx_irq
   * Undefined32:
   *   - R13 and R14 are banked and renamed Rx_und
   *
   * The program staus register CPSR is banked in all modes (has a copy
   * that is unique).
   *
   * For simplicity in the implementation, we manually swap the banked
   * registers in/out of the active register set as necessary.
   */
  struct Registers {
    /* Active register set */
    u32 R[16];
    CPSR_bits CPSR;
    CPSR_bits SPSR;

    /* General register set for each of the execution modes. Many of these
     * entries will not be used (masked off when not banked). */
    u32 R_user[16];
    u32 R_fiq[16];
    u32 R_svc[16];
    u32 R_abt[16];
    u32 R_irq[16];
    u32 R_und[16];

    /* Program status register banks */
    CPSR_bits SPSR_fiq;
    CPSR_bits SPSR_svc;
    CPSR_bits SPSR_abt;
    CPSR_bits SPSR_irq;
    CPSR_bits SPSR_und;

    u32 pending_interrupts;
  };

  Arm7DI(fox::MemoryTable *mem);
  ~Arm7DI();

  /*!
   * @brief Retrieve a read-only reference to the internal CPU state for
   *        inspection.
   */
  Registers &registers();

  /*!
   * @brief Perform a hard reset of the CPU core.
   */
  void reset();

  /*!
   * @brief Simulate a single instruction on the processor core.
   */
  void step();

  Arm7DIAssembler &get_assembler();

  /*!
   * @brief Switch between processor modes, handling register banking as
   *        necessary. Publicly accessible only to be called from free functions
   */
  void mode_switch(ProcessorMode current_mode, ProcessorMode new_mode);

  /*!
   * @brief Read a register from the user bank regardless of the current mode.
   */
  u32 read_register_user(u32 index) const;

  /*!
   * @brief Write a register to the user bank regardless of the current mode.
   */
  void write_register_user(u32 index, u32 value);

  /*!
   * @brief Raise a FIQ interrupt on the processor.
   */
  void raise_fiq();

  void clear_fiq();

  u32 debug_fetch_instruction(u32 address);

  fox::jit::Cache *get_jit_cache() { return &m_jit_cache; }

  class BasicBlock;

  void set_fixed_pc_fetch_offset(u32 offset) { m_fixed_pc_offset = offset; }

protected:
  /*!
   * @brief Raise an exception on the processor.
   */
  void raise_exception(Exception exception);

  /*!
   * @brief Shared memory table with SH4, which includes this core's system
   *        RAM (referred to as Wave memory from SH4).
   */
  fox::MemoryTable *const m_mem;

  /*!
   * @brief Internal representation of the processor state.
   */
  Registers m_registers;

  /*!
   * @brief Assembler for the Arm7DI core.
   */
  Arm7DIAssembler m_assembler;

  /* Loads and stores are always 1/2/4/8 bytes. */
  fox::Value guest_register_read(unsigned index, size_t bytes) final;
  void guest_register_write(unsigned index, size_t bytes, fox::Value value) final;

  // Intentional !
  // The below are not implemented because the specifics of how a store/load
  // interacts with memory is system dependent. A child class will specialize these
  // for a particular system's bus access.

  // virtual fox::Value guest_load(u32 address, size_t bytes) final;
  // virtual void guest_store(u32 address, size_t bytes, fox::Value value) final;

  fox::jit::Cache m_jit_cache;

  u32 m_fixed_pc_offset = 0x0080'0000;
};

}
