// vim: expandtab:ts=2:sw=2

#pragma once

#ifdef _WIN64
#include <Windows.h>
#else
#include <signal.h>
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE
#endif
#include <ucontext.h>
#endif

#include <array>
#include <ostream>
#include <atomic>
#include <functional>
#include <vector>
#include <unordered_set>
#include <set>

#include "fox/memtable.h"
#include "fox/guest.h"
#include "fox/jit/cache.h"
#include "shared/log.h"
#include "shared/types.h"
#include "serialization/serializer.h"
#include "shared/scheduler.h"
#include "shared/cpu.h"
#include "shared/execution_graph.h"
#include "sh4_opcode.h"
#include "sh4_trace.h"
#include "sh4_mmio.h"

namespace gui {
class CPUMMIOWindow;
class CPUWindow;
}

class Console;

namespace cpu {

/*!
 * @brief Interrupt types that the SH4 CPU can accept, both internal and
 *        external.
 *
 * These are used in a bitmask ordered by the interrupt's priority, leading
 * to the haphazard ordering required here.
 */
enum Interrupt : int {
  /* Highest priority at the top */
  NMI = 0,
  IRQ0,
  IRQ1,
  IRQ2,
  IRQ3,
  IRQ4,
  IRQ5,
  IRQ6,
  IRQ7,
  IRQ8,
  IRQ9,
  IRQ10,
  IRQ11,
  IRQ12,
  IRQ13,
  IRQ14,
  DMTE0,
  DMTE1,
  DMTE2,
  DMTE3,
  DMAE,
  TUNI0,
  TUNI1,
  TUNI2,
  NUM_SH4_INTERRUPTS
};

/*!
 * @class SH4
 * @brief Guest implementation of the SH4a "SuperH" 32-bit RISC CPU
 *
 * Implemented without thread safety, so the CPU should only be accessed from
 * one thread at any time (setup thread or execution thread).
 *
 * For more information, see https://en.wikipedia.org/wiki/SuperH
 *
 * Word: 16b, Longword: 32b, SPFP: 32b, DPFP: 64b
 */
class SH4 : public fox::Guest, serialization::Serializer {
public:
  /*!
   * @brief Method of basic block execution.
   */
  enum ExecutionMode {
    Interpreter,
    Bytecode,
    Native
  };

  /*!
   * @brief Exception thrown when the CPU reaches a user defined software
   *        breakpoint.
   */
  class BreakpointException : public std::exception {
  public:
    BreakpointException() : std::exception()
    {
      return;
    }
  };

  /* Forward declaration - basic block for JIT. */
  class BasicBlock;

  /*!
   * @struct SH4::Registers
   * @brief State of all SH4 CPU registers
   *
   * There are four kinds of registers: General, System, Control, FP
   * Access to these depends on the current CPU mode, which can be user or
   * privileged. The first 8 general purpose registers are banked.
   */
  struct Registers {
    /*!
     * @brief General purpose registers R0 - R15, with two banks for R0 - R7.
     *
     * In privileged mode, R0-R7 are aliases for the the first seven GPRs
     * in the bank selected in the status register. The LDC/STC instructions
     * can be used to access the opposing bank.
     *
     * In user mode, R0-R7 are always used to reference bank 0.
     *
     * The array is layed out as:
     *  - [0  ..  7] -> [R0 ..  R7] (active bank)
     *  - [8  .. 15] -> [R8 .. R15]
     *  - [16 .. 23] -> [R0 ..  R7] (alternate bank)
     */
    u32 general_registers[24];

    /*!
     * @brief State registers, which is mostly privileged state
     */
    union Status {
      u32 raw;

      struct {
        u32 T : 1;       /**< True/False or Carry/Borrow */
        u32 S : 1;       /**< Saturation for MAC instruction */
        u32 _rsvd2 : 2;  /**< Reserved */
        u32 IMASK : 4;   /**< Interrupt Mask Level */
        u32 Q : 1;       /**< User by DIV* instructions */
        u32 M : 1;       /**< Used by DIV* instructions */
        u32 _rsvd1 : 5;  /**< Reserved */
        u32 FD : 1;      /**< FPU Disable */
        u32 _rsvd0 : 12; /**< Reserved */
        u32 BL : 1;      /**< Exception/Interrupt Block */
        u32 RB : 1;      /**< Register Bank Select */
        u32 MD : 1;      /**< Mode (Priviledged / User) */
        u32 _1 : 1;      /**< Reserved */
      };
    };

    /*!
     * @brief Status Register
     */
    Status SR;

    /*!
     * @brief Saved Status Register
     */
    Status SSR;

    /*!
     * @brief Program Counter
     */
    u32 PC;

    /*!
     * @brief Saved Program Counter
     */
    u32 SPC;

    /*!
     * @brief Global Base Register
     */
    u32 GBR;

    /*!
     * @brief Vector Base Register
     */
    u32 VBR;

    /*!
     * @brief Multipliy/Accumulate Registers
     */
    union {
      struct {
        u32 MACL;
        u32 MACH;
      };

      u64 MAC;
    };

    /*!
     * @brief Program Register
     */
    u32 PR;

    /*!
     * @brief Saved Program Register
     */
    u32 SPR;

    /*!
     * @brief Saved General Register 15
     */
    u32 SGR;

    /*!
     * @brief Debug Base Register (Priviledge Mode Only)
     */
    u32 DBR;

    /*!
     * @brief Set all register values to 0.
     */
    void clear();

    /*!
     * @brief Print current register state to stdout for debugging
     */
    void write(std::ostream &output) const;
  };

  /*!
   * @struct SH4::FPUState
   * @brief State of the FPU registers
   */
  struct FPUState {
    /*!
     * @brief FPU Status/Control Register
     */
    union FPUStatus {
      u32 raw;
      struct {
        u32 RM0 : 1;    /**< Rounding Mode Bit 2 */
        u32 RM1 : 1;    /**< Rounding Mode Bit 1 */
        u32 Flag : 5;   /**< FPU Exception Flags */
        u32 Enable : 5; /**< FPU Exception Enable Field */
        u32 Cause : 6;  /**< FPU Exception Cause Field */
        u32 DN : 1;     /**< Denormalization Mode */
        u32 PR : 1;     /**< Precision Mode */
        u32 SZ : 1;     /**< Transfer Size Mode */
        u32 FR : 1;     /**< Bank Select */
        u32 _1 : 10;    /**< Reserved */
      };
    };

    /*!
     * @brief One bank of floating point registers. Each pair of single
     *        precision registers can be used as a single double precision
     *        register.
     *
     * The active type (single / double) and bank are controlled by FPSCR.
     */
    union register_set {
      float sp[16];
      double dp[8];
    };

    register_set banks[2];

    /*!
     * @brief FPU status and configuration register
     */
    FPUStatus FPSCR;

    /*!
     * @brief FP communication register (Used for CPU <-> FPU transfers)
     */
    u32 FPUL;

    float &FR(unsigned i)
    {
      return banks[0].sp[i];
    }

    double &DR(unsigned i)
    {
      return banks[0].dp[i];
    }

    float &XF(unsigned i)
    {
      return banks[1].sp[i];
    }

    double &XD(unsigned i)
    {
      return banks[1].dp[i];
    }

    /*!
     * @brief Swap FPU register banks. This must be called any time the FR bit
     *        in FPSCR is changed (from 0 to 1 / 1 to 0).
     */
    void swap_bank()
    {
      for (unsigned i = 0; i < 16; ++i) {
        std::swap(banks[0].sp[i], banks[1].sp[i]);
      }
    }

    void write(std::ostream &output) const;
  };

  SH4(Console *console);
  ~SH4();

  void serialize(serialization::Snapshot &) override;
  void deserialize(const serialization::Snapshot &) override;

  /*!
   * @brief Set the current CPU emulation mode (e.g. bytecode JIT)
   */
  void set_execution_mode(const ExecutionMode new_mode)
  {
    m_execution_mode = new_mode;
  }

  /*!
   * @brief Return the current CPU emulation mode.
   */
  const ExecutionMode get_execution_mode() const
  {
    return m_execution_mode;
  }

  /*!
   * @brief Whether the currently executing instruction is a jump delay slot
   */
  bool in_delay_slot() const;

  /*!
   * @brief Execute a single instruction located at the current PC address.
   *        Returns the number of cycles executed.
   */
  u64 step();

  /*!
   * @brief Execute for a CPU quantum, until the end of the basic block.
   *
   * Returns number of cycles executed.
   */
  u64 step_block();

  /*!
   * @brief Read from current CPU register state.
   */
  const Registers &registers() const
  {
    return regs;
  }

  /*!
   * @brief Directly set current CPU register state.
   */
  void set_registers(Registers new_registers)
  {
    regs = new_registers;
  }

  /*!
   * @brief Read from current FPU register state.
   */
  const FPUState &fpu_registers() const
  {
    return FPU;
  }

  /*!
   * @brief Directly set current FPU register state.
   */
  void set_fpu_state(FPUState new_state)
  {
    FPU = new_state;
  }

  /*!
   * @brief Retrieve the memory object used by the CPU.
   */
  fox::MemoryTable *memory()
  {
    return m_phys_mem;
  }

  /*!
   * @brief Create a new CacheEntry starting at the provided address.
   */
  BasicBlock *jit_create_unit(u32 address);

  /*!
   * @brief Interpret code from a JIT cache entry, instead of fetching the
   *        instructions directly.
   */
  void jit_interpret(const BasicBlock *unit);

  void jit_native(BasicBlock *unit);

  fox::Value guest_register_read(unsigned index, size_t bytes) override final;
  void guest_register_write(unsigned index,
                            size_t bytes,
                            fox::Value value) override final;

  fox::Value guest_load(u32 address, size_t bytes) override final;
  void guest_store(u32 address, size_t bytes, fox::Value value) override final;

  /*!
   * @brief Perform a soft CPU reset
   */
  void reset();

  /*!
   * @brief Set the input level for an IRQ line to high (schedule it). The
   *        passed parameter n corresponds to IRQn on the CPU.
   */
  void latch_irq(u32 irq_line);

  /*!
   * @brief Set the input level for an IRQ line to low (cancel it). The
   *        passed parameter n corresponds to IRQn on the CPU.
   */
  void cancel_irq(u32 irq_line);

  /*!
   * @brief Execute a DMAC operation triggered by an external device
   * @return false if an error occurred during transfer
   */
  bool execute_dmac(unsigned channel, u32 external_target, u32 length);

  /* CPU Debugging Methods */

  /*!
   * @brief Enable or disable CPU debugging mode, which may disable some
   *        optimizations when enabled.
   */
  void debug_enable(bool enable = true);

  /*!
   * @brief Whether or not debug breakpoints are enabled
   */
  bool is_debug_enabled() const;

  /*!
   * @brief Add an instruction address to the list of CPU software breakpoints
   */
  void debug_breakpoint_add(u32 address);

  /*!
   * @brief Remove an instruction address to the list of CPU software
   *        breakpoints
   */
  void debug_breakpoint_remove(u32 address);

  /*!
   * @brief Retrieve the set of breakpoints enabled on the CPU
   */
  void debug_breakpoint_list(std::vector<u32> *result);

  void debug_watchpoint_add(u32 address, zoo::WatchpointOperation);
  void debug_watchpoint_remove(u32 address, zoo::WatchpointOperation);
  bool debug_watchpoint_check(u32 address, zoo::WatchpointOperation);

  /*!
   * @brief Enable or disable handling of interrupts. Only has effect when
   *        debugging is enabled.
   */
  void debug_mask_interrupts(bool masked);

  /*!
   * @brief Generate an ELF-format core file with the processor's current
   *        state.
   */
  void debug_save_core(const std::string &filename);

  /*!
   * @brief Return the Cache instance used for the CPU JIT.
   */
  fox::jit::Cache *get_jit_cache()
  {
    return m_jit_cache.get();
  }

public: /* Static entry points */
#ifndef _WIN64
  /*!
   * @brief Handle a memory fault during JIT execution (Linux / OSX)
   */
  static void jit_handle_fault(int signo, siginfo_t *info, void *ucontext_opaque);
#else
  /*!
   * @brief Handle a memory fault during JIT execution (Windows)
   */
  static LONG CALLBACK jit_handle_fault(PEXCEPTION_POINTERS ex_info);
#endif

  /*!
   * @brief Execute a single instruction using the interpreter as a fallback
   *        from a JIT block.
   */
  static fox::Value interpreter_upcall(fox::Guest *cpu, fox::Value opcode, fox::Value PC);

  /*!
   * @brief Handle a conditional GPR bank swap from a JIT block.
   */
  static fox::Value gpr_maybe_swap(fox::Guest *cpu, fox::Value do_swap);

  /*!
   * @brief Handle a conditional FPU bank swap from a JIT block.
   */
  static fox::Value fpu_maybe_swap(fox::Guest *cpu, fox::Value do_swap);

public: /* Instruction decoding */
  /*!
   * @brief Brute-force table of opcodes.
   */
  static const std::vector<Opcode> opcode_table;
  static const u8 decode_table[65536];

  u32 *pc_register_pointer()
  {
    return &(regs.PC);
  }

  void branch_to_pc(u32 address)
  {
    regs.PR = regs.PC;
    jmp_nodelay(address);
  }

protected:
  /*!
   * @brief Get a writable reference to a GPR
   */
  u32 &gpr(const unsigned index)
  {
    return regs.general_registers[index];
  }

  /*!
   * @brief Get a writable reference to a GPR in the alternate bank
   */
  u32 &gpr_alt(const unsigned index)
  {
    const unsigned is_lower_register = ((~index) & 0x8) >> 3u;
    const unsigned bank_shift = is_lower_register << 4u;

    return regs.general_registers[index | bank_shift];
  }

  /*!
   * @brief Retrieve the current value of a referenced GPR
   */
  template<typename T>
  T gpr(const unsigned index) const
  {
    return regs.general_registers[index];
  }

  /*!
   * @brief Retrieve the current value of a referenced GPR in the alternate
   *        bank.
   */
  template<typename T>
  T gpr_alt(const unsigned index) const
  {
    const unsigned is_lower_register = ((~index) & 0x8) >> 3u;
    const unsigned bank_shift = is_lower_register << 4u;

    return regs.general_registers[index | bank_shift];
  }

  /*!
   * @brief Swap GPR banks. This must be called any time the RB bit in SR is
   *        changed (from 0 to 1 / 1 to 0).
   */
  void gpr_swap_bank()
  {
    for (unsigned i = 0; i < 8; ++i) {
      std::swap(regs.general_registers[i], regs.general_registers[i + 16]);
    }
  }

  /*!
   * @brief Perform a branch with delay slot execution
   */
  void jmp_delay(u32 address);

  /*!
   * @brief Perform a branch without delay slot execution
   */
  void jmp_nodelay(u32 address);

  void handle_sampling_profiler_tick();

public: /* XXX Required for debug utilities */
  /*!
   * @brief Read a single 16-bit value from CPU memory through required
   *        translation for the purpose of instruction decoding.
   *
   * This should only be used internally by the CPU for instruction decoding,
   * as idata reads must go through the iTLB and generate different exceptions.
   */
  u16 idata_read(u32 address) const;

  /*!
   * @brief Read single value from CPU memory (through TLB etc.)
   *
   * XXX May need to not be const when/if we emulate TLB misses
   *
   * Can only be used for types CPU can operate on. Currently:
   *   - Signed and unsigned integers size 8, 16, and 32
   *   - Single and double precision floating point
   */
  template<typename T>
  T mem_read(u32 address);

  /*!
   * @brief Write single value to memory through CPU translation
   *
   * Can only be used for types CPU can operate on. Currently:
   *   - Signed and unsigned integers size 8, 16, and 32
   *   - Single and double precision floating point
   */
  template<typename T>
  void mem_write(u32 address, T value);

  /* Required for testing which decodes instructions */
  friend gui::CPUMMIOWindow;
  friend gui::CPUWindow;

private: /* Internal type definitions */
  /*!
   * @brief Type of address decoding from mem_region
   */
  enum class AddressType {
    Invalid,
    AccessViolation,
    Physical,
    Register,
    StoreQueue,
    OperandCache,
    Virtual,
  };

private: /* CPU Hardware State */
  /*!
   * @brief The physical memory bus attached to the CPU
   */
  fox::MemoryTable *const m_phys_mem;

  /*!
   * @brief Current state of all CPU registers. Ensure these are always next
   *        to each other in memory and the ordering stays consistent with
   *        the IR mappings.
   */
  struct {
    Registers regs;
    FPUState FPU;

    /*!
     * @brief Pseudo-register for keeping track of emulated cycle counts.
     *
     * Incremented by JIT CPU execution. Set to 0 before execution of JIT
     * blocks so it does not require serialization.
     */
    uint32_t m_jit_cycles;
  };

  /*!
   * @brief Internal memory for Store Queue operations. Upper 32-bytes is SQ1.
   */
  uint8_t SQ[64];

  /*!
   * @brief Current state of all CPU MMIO registers
   */
  MMIO m_mmio;

  /*!
   * @brief SH4 operand cache. Currently only implemented as scratch memory.
   */
  u8 m_operand_cache[8 * 1024];

  /*!
   * @brief Entry type in the SH4 interrupt table.
   */
  struct InterruptType {
    const char *name;
    const u32 evt;
    u8 priority;
  };

  /*!
   * @brief All hardware interrupts available on the SH4.
   *
   * NOTE Level 2/4/6 are specific to Dreamcast.
   */
  std::array<InterruptType, NUM_SH4_INTERRUPTS> m_interrupt_table = {
    /* Highest priority at the top */
    InterruptType { "NMI", 0x1C0u, 16u },
    InterruptType { "IRQ0", 0x200u, 15u },
    InterruptType { "IRQ1", 0x220u, 14u },
    InterruptType { "IRQ2", 0x240u, 13u },
    InterruptType { "IRQ3", 0x260u, 12u },
    InterruptType { "IRQ4", 0x280u, 11u },
    InterruptType { "IRQ5", 0x2A0u, 10u },
    InterruptType { "IRQ6", 0x2C0u, 9u },
    InterruptType { "IRQ7", 0x2E0u, 8u },
    InterruptType { "IRQ8", 0x300u, 7u },
    InterruptType { "IRQ9", 0x320u, 6u }, /* "Level 6" */
    InterruptType { "IRQ10", 0x340u, 5u },
    InterruptType { "IRQ11", 0x360u, 4u }, /* "Level 4" */
    InterruptType { "IRQ12", 0x380u, 3u },
    InterruptType { "IRQ13", 0x3A0u, 2u }, /* "Level 2" */
    InterruptType { "IRQ14", 0x3C0u, 1u },
    InterruptType { "DMTE0", 0x640u, 0u },
    InterruptType { "DMTE1", 0x660u, 0u },
    InterruptType { "DMTE2", 0x680u, 0u },
    InterruptType { "DMTE3", 0x6A0u, 0u },
    InterruptType { "DMAE", 0x6C0u, 0u },
    InterruptType { "TUNI0", 0x400u, 0u },
    InterruptType { "TUNI1", 0x420u, 0u },
    InterruptType { "TUNI2", 0x440u, 0u },
    /* Fill the rest in later - they're all priority 0 */
  };

  /*!
   * @brief Whether the previous call to opcode::excute resulted in a branch
   */
  u32 m_executed_branch = false;

  /*!
   * @brief Whether the next instruction to execute is a delay slot
   */
  u32 m_branch_target = 0xFFFFFFFFu;

  /*!
   * @brief Pending interrupts bitmask
   */
  std::atomic<u32> m_pending_interrupts { 0u };

  /* CPU Emulation State */

  /*!
   * @brief Implementation of JIT compilation for CPU emulation.
   */
  const std::unique_ptr<fox::jit::Cache> m_jit_cache;

  /*!
   * @brief Console handle used for scheduling events.
   */
  Console *const m_console;

  /*!
   * @brief Scheduler events for TMU counter underflows.
   */
  EventScheduler::Event m_tmu_event;

  /*!
   * @brief Scheduler event used to sample and emit the state of the SH4 call
   *        stack for profiling tools.
   */
  EventScheduler::Event m_sampling_profiler;

  /*!
   * @brief Most recently execute basic block. Used to avoid cache lookups in
   *        tight loops.
   */
  fox::jit::CacheEntry *m_last_block = nullptr;

  /* CPU Debugging State */

  /*!
   * @brief Whether low-level debugging is enabled. Debugging can only be used
   *        with single-step execution.
   */
  bool m_debug_mode = false;

  /*!
   * @brief Set of instruction breakpoint addresses that will halt CPU
   *        execution (throw BreakpointException()) when enabled.
   */
  std::unordered_set<u32> m_debug_breakpoints;

  /*!
   * @brief Set of memory addresses that will halt CPU
   *        execution (throw BreakpointException()) when read.
   */
  std::unordered_set<u32> m_debug_read_watchpoints;

  /*!
   * @brief Set of memory addresses that will halt CPU
   *        execution (throw BreakpointException()) when written to.
   */
  std::unordered_set<u32> m_debug_write_watchpoints;

  /*!
   * @brief Made true during the execution of a read/write which should halt
   *        the system. Because we need the system to complete it's current
   *        instruction, we cannot throw in the middle of an instruction, so
   *        this flag marks that we should break on the following instruction.
   */
  bool m_debug_watchpoint_triggered = false;

  /*!
   * @brief Whether to respond to pending interrupts or not. Only takes effect
   *        when debug mode is enabled.
   */
  bool m_debug_mask_interrupts = false;

  /*!
   * @brief Execution mode for basic blocks.
   */
  ExecutionMode m_execution_mode = ExecutionMode::Native;

private: /* CPU Internal Methods */
  /*!
   * @brief Wrapper method to execute a basic block, potentially with extra
   *        instrumentation.
   */
  u64 execute_block(BasicBlock *block);

  /*!
   * @brief Internal API for reading from 32-bit CPU MMIO registers
   */
  u32 mmio_read_long(u32 address) const;

  /*!
   * @brief Internal API for reading from 16-bit CPU MMIO registers
   */
  u16 mmio_read_word(u32 address) const;

  /*!
   * @brief Internal API for writing to CPU MMIO registers
   */
  void mmio_write(u32 address, u32 value, u32 size);

  /*!
   * @brief Flush the Store Queue specified by the provided prefetch address
   */
  void sq_flush(u32 address);

  void *get_operand_cache_pointer(u32 offset);

  /*!
   * @brief Map from CPU virtual / internal address to address region and
   *        offset
   * @return Type of memory found at the target location
   *
   * @param[in]  address       The raw address accessed by the CPU
   * @param[in]  is_supervisor Whether the access is privileged
   *
   * Does not check whether a physical address is validly mapped or if a
   * particular register in an MMIO region exists.
   *
   * TODO Might not be const when/if TLB etc. is implemented
   */
  std::pair<AddressType, u32> mem_region(u32 address, bool is_supervisor) const;

  /*!
   * @brief Internal helper (called by step() et al) for handling execution
   *        of a pending interrupt.
   */
  void handle_interrupt(unsigned id);

  /*!
   * @brief Called when a TMU TSTR register is written to.
   */
  void handle_tstr_write(u8 value);

  /*!
   * @brief Called when a TMU TCNT register for channel 'id' is written to.
   */
  void handle_tcnt_write(unsigned id, u32 value);

  /*!
   * @brief Called when a TMU TCR register for channel 'id' is written to.
   */
  void handle_tcr_write(unsigned id, u16 value);

  /*!
   * @brief Scheduler callback for updating TMU channel expiration.
   *
   * Checks underflow and interrupt bits to update internal state and
   * potentially fire interrupts.
   */
  void tick_tmu_channels();

  /*!
   * @brief Perform a power-on reset of the CPU
   */
  void power_on_reset();

  /*!
   * @brief Check for and return details of the next pending interrupt.
   */
  bool check_interrupt(u32 *interrupt_index, u32 *priority);

public: /* TODO: Hack for opcode table */
  /*!
   * @brief Internal implementation of each instruction, templated on the
   *        opcode
   */
  template<typename T>
  void execute_instruction(u16 raw_opcode);

public: /* Debug Instrumentation */
  void push_call_address(u32 new_address);
  void pop_call_address();
  void copy_call_stack(std::vector<u32> &output);

  void set_sampling_profiler_running(bool running);

  const std::vector<u32> &get_call_stack() const
  {
    return m_call_stack;
  }

  ExecutionGraph m_execution_graph;
  std::mutex m_call_stack_mutex;
  std::vector<u32> m_call_stack;
};

}
