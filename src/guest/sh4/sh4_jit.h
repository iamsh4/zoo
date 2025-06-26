// vim: expandtab:ts=2:sw=2

#pragma once

#include <set>
#include <list>
#include <unordered_map>
#include <mutex>
#include <signal.h>

#include "fox/memtable.h"
#include "fox/bytecode/bytecode.h"
#include "fox/codegen/routine.h"
#include "fox/jit/cache.h"
#include "sh4.h"
#include "sh4_ir.h"

namespace gui {
class JitCacheWindow;
class JitWorkbenchWindow;
class IrAnalysisWidget;
}

namespace cpu {

typedef std::vector<InstructionDetail> BasicBlockOpcodes;

/*!
 * @class cpu::SH4::BasicBlock
 * @brief Specialization of JIT'd CacheEntry that stores JIT'd and cached
 *        instruction sequences for the SH4 CPU. Tracks multiple compilation
 *        versions (e.g. bytecode + amd64), flags affecting their execution,
 *        and runtime statistics.
 */
class SH4::BasicBlock : public fox::jit::CacheEntry {
public:
  /*!
   * @brief Mask to get physical (SH4 bus) addresses from virtual addresses.
   */
  static constexpr uint32_t ADDRESS_MASK = 0x1FFFFFFFu;

  /*!
   * @brief Basic statistics collected from running this basic block, which
   *        can be used to decide when / how to compile the block.
   */
  struct Stats {
    /*!
     * @brief Number of times the block has been executed.
     */
    uint64_t count_executed = 0lu;

    /*!
     * @brief Number of times the block has been interpreted.
     */
    uint64_t count_interpreted = 0lu;

    /*!
     * @brief Number of times the block has been run after compilation.
     */
    uint64_t count_compiled = 0lu;

    /*!
     * @brief Number of times the block attempted to execute native code but
     *        failed because the block wasn't remapped yet.
     */
    uint64_t count_not_remapped = 0lu;

    /* Number of times the block has been reverted to interpreter mode after
     * previously being compiled. */
    // uint64_t count_decompiled = 0lu;

    /*!
     * @brief The combination of CPU flags in effect the last time this block
     *        was run.
     */
    uint64_t last_flags = 0lu;

    /*!
     * @brief The number of times in a row that this block has been executed
     *        with the CPU flags in last_flags. */
    uint64_t last_flags_count = 0lu;

    /*!
     * @brief The number of times execution had to fall back to interpreter
     *        because the guard flag check failed. */
    uint64_t guard_failed = 0lu;
  };

  enum class StopReason
  {
    SizeLimit,     /**< Stopped after reaching the maximum instruction count. */
    Branch,        /**< Stopped at an unconditional branch. */
    StartOfBlock,  /**< Stopped at the start of another EBB block. */
    Barrier,       /**< Stopped because a barrier instruction was encountered. */
    InvalidOpcode, /**< Stopped because decoding an instruction failed. */
  };

  enum JitFlag
  {
    DIRTY = 1lu << 0u,           /**< Previously compiled, that copy was invalidated. */
    DISABLE_FASTMEM = 1lu << 1u, /**< Use function call for memory access. */
  };

  enum CpuFlag
  {
    SR_RB = 1lu << 0u,    /**< Select CPU register bank. */
    FPSCR_FR = 1lu << 1u, /**< Select FPU register bank. */
    FPSCR_SZ = 1lu << 2u, /**< Select FPU transfer size. */
    FPSCR_PR = 1lu << 3u, /**< Select FPU math precision. */
  };

  /*!
   * @brief Construct a new basic block from the provided range and sequence
   *        of SH4 opcodes.
   */
  BasicBlock(u32 start_address,
             u32 end_address,
             BasicBlockOpcodes &&instructions,
             u32 guard_flags,
             u32 jit_flags,
             StopReason reason);

  ~BasicBlock();

  /*!
   * @brief Perform compilation of the JIT block, taking into account all
   *        modifiers and trace results. Can be called multiple times, with
   *        the new compilation replacing the old one.
   */
  bool compile() override final;

  void add_flag(const JitFlag new_flag)
  {
    m_flags |= (new_flag | DIRTY);
  }

  u32 instruction_count() const
  {
    return m_instructions.size();
  }

  const BasicBlockOpcodes &instructions() const
  {
    return m_instructions;
  }

  u32 flags() const
  {
    return m_flags;
  }

  u32 guard_flags() const
  {
    return m_guard_flags;
  }

  void mark_dirty()
  {
    m_flags |= DIRTY;
  }

  void mark_clean()
  {
    m_flags &= ~DIRTY;
  }

  const Stats &stats() const
  {
    return m_stats;
  }

  StopReason stop_reason() const
  {
    return m_stop_reason;
  }

  const std::unique_ptr<fox::ir::ExecutionUnit> &unit() const
  {
    return m_unit;
  }

  /*!
   * @brief Execute the basic block with the most appropriate backend (bytecode,
   *        native, etc.).
   *
   * Execution will increment the SH4 instance's cycle count pseudo register.
   */
  void execute(SH4 *guest);

private:
  /*!
   * @brief The reason the block was cut off / didn't include more instructions.
   */
  const StopReason m_stop_reason;

  /*!
   * @brief The set of CPU flags that this code block will be affected by. If
   *        the flags change between executions of this block it will need to
   *        fallback to the interpreter.
   */
  const uint32_t m_guard_flags;

  /*!
   * @brief The raw series of SH4 instructions being executed / translated.
   */
  const BasicBlockOpcodes m_instructions;

  /*!
   * @brief Statistics collected for the block.
   */
  Stats m_stats;

  /*!
   * @brief SSA intermediate form for the instruction sequence, when
   *        available.
   */
  std::unique_ptr<fox::ir::ExecutionUnit> m_unit;

  /*!
   * @brief Storage for the bytecode compilation of the instruction sequence,
   *        when available.
   */
  std::unique_ptr<fox::jit::Routine> m_bytecode;

  /*!
   * @brief Storage for the host-native compilation of the instruction sequence,
   *        when available.
   */
  std::unique_ptr<fox::codegen::Routine> m_native;

  /*!
   * @brief The state of CPU flags that was in effect when the entry was queued
   *        for compilation. These are used to control IR generation, and the
   *        flag set is copied to m_compiled_flags when compilation finishes.
   */
  std::atomic<uint32_t> m_target_flags;

  /*!
   * @brief The required values for the guard flags to allow use of the native
   *        compilation.
   */
  u32 m_compiled_flags;

  /*!
   * @brief Modifier flags that affect how compilation will be done.
   */
  std::atomic<u32> m_flags;

  friend gui::JitCacheWindow;
  friend gui::JitWorkbenchWindow;
  friend gui::IrAnalysisWidget;

  /*!
   * @brief Helper method to calculate the current set of guard flags from
   *        the CPU's state.
   */
  u32 calculate_guard_flags(SH4 *target);

  /*!
   * @brief Helper method to execute the native compilation a single time.
   *        Returns the number of CPU cycles executed.
   */
  u32 execute_native(SH4 *guest);

  /*!
   * @brief Helper method to execute the bytecode compilation a single time.
   *        Returns the number of CPU cycles executed.
   */
  u32 execute_bytecode(SH4 *guest);

  /*!
   * @brief Helper method to interpret the block a single time. Returns the
   *        number of CPU cycles executed.
   */
  u32 execute_interpreter(SH4 *guest);
};

fox::ir::ExecutionUnit optimize(fox::ir::ExecutionUnit &input);

}
