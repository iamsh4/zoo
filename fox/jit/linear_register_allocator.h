// vim: expandtab:ts=2:sw=2

#pragma once

#include <map>

#include "fox/jit/register_allocator.h"

namespace fox {
namespace jit {

/*!
 * @class fox::jit::RangeSet
 * @brief A data structure that can store multiple ranges. Each range is
 *        associated with an ownership id. All ranges for a particular id
 *        must be distinct, but ranges from different ids can overlap. It
 *        allows adding ranges and querying for contention per ownership
 *        id.
 */
class RangeSet {
public:
  RangeSet()
  {
    return;
  }

  ~RangeSet()
  {
    return;
  }

  /*!
   * @brief Erase all internal interval data.
   */
  void clear()
  {
    m_data.clear();
  }

  /*!
   * @brief Add a range to the set for the ownership id. The range is
   *        closed on the start and open on the end.
   *
   * The range must not overlap with any existing ranges with the same id.
   */
  void add_range(u32 id, u32 start, u32 end);

  /*!
   * @brief Returns true if the range set contains a range for id that overlaps
   *        a position.
   */
  bool is_contended(u32 id, u32 position) const;

  /*!
   * @brief Returns true if there are any ranges for id that intersect
   *        with the provided range.
   */
  bool is_contended_range(u32 id, u32 start, u32 end) const;

  void debug_print();

private:
  /*!
   * @brief Maps from a pair<ownership id, range end> to the start of an
   *        range.
   */
  std::multimap<std::pair<u32, u32>, u32> m_data;
};

/*!
 * @class fox::jit::LinearAllocator
 * @brief Implementation of an SSA linear scan register allocator with support
 *        for basic register constraints. Works on a single extended basic block
 *        at a time (no phi support).
 */
class LinearAllocator : public RegisterAllocator {
public:
  LinearAllocator();
  ~LinearAllocator();

  void define_register_type(RegisterSet available) override final;
  RtlProgram execute(RtlProgram &&input) override final;

private:
  /*!
   * @struct fox::jit::LinearAllocator::LiveRange
   * @brief Metadata for a register assignment over a contiguous range of
   *        rtl instructions.
   */
  struct LiveRange {
    RtlRegister rtl;
    HwRegister hw;
    RegisterState *state;
    u32 from;
    u32 to;

    /*!
     * @brief Parent range's index. UINT32_MAX when there is no parent.
     *
     * When two ranges are merged the earlier range becomes the parent of the
     * later range and assignment is done only on the parent. The child
     * inherits the parent's register assignment after allocation.
     */
    u32 parent;
  };

  /*!
   * @brief The set of general purpose registers available for allocation in
   *        each register set / class.
   *
   * Register values are not distinct to each set. Register sets may overlap.
   * The first set must include all possible registers.
   */
  std::array<RegisterSet, HwRegister::MaxTypes> m_hw_registers;

  /*!
   * @brief The set of registers that were allocated at least once by the
   *        register allocator.
   *
   * Unused registers can be skipped when preparing stack frames, and in the
   * case of spills, indicates how much spill memory is required.
   *
   * Includes the set of registers with fixed assignments before allocation.
   */
  std::array<RegisterSet, HwRegister::MaxTypes> m_hw_unused;

  /*!
   * @brief Set of ranges where hardware registers have already been allocated
   *        for each register type. The ownership id is the hardware register.
   */
  std::array<RangeSet, HwRegister::MaxTypes> m_hw_ranges;

  /*!
   * @brief The sequence of RTL instructions currently being used. This is
   *        modified through each stage and doesn't equal the original input.
   */
  RtlProgram m_target;

  /*!
   * @brief List of liveliness ranges for RTL (SSA) registers, sorted by their
   *        start instruction.
   */
  std::vector<LiveRange> m_live_ranges;

  /*!
   * @brief Array of indexes for m_live_ranges. An RTL register index into this
   *        array returns the associated live range for that index.
   */
  std::vector<u32> m_ranges_reverse;

  /*!
   * @brief Prepare the incoming instruction sequence for allocation by adding
   *        register<->register move operations at points with fixed register
   *        allocations.
   *
   * Reads and writes to m_target.
   */
  void prepare();

  /*!
   * @brief Calculate the range of instructions where each IR register is
   *        active, and place the results in m_live_ranges ordered by their
   *        start position.
   */
  void calculate_live_ranges();

  /*!
   * @brief For ranges that are compatible and benefit from sharing a register,
   *        join them. This reduces the runtime of the linear scan assignment
   *        and optimizes for register constraints.
   */
  void join_live_ranges();

  /*!
   * @brief Assign hardware registers to all liveliness ranges.
   */
  void assign_registers();

  /*!
   * @brief Apply the liveliness range register allocations back to the RTL
   *        result.
   */
  void encode_rtl();

  /*!
   * @brief Remove coalesced move instructions from the RTL stream.
   */
  void prune_rtl();

  /*!
   * @brief Helper that attempts to join the two liveliness ranges provided via
   *        their index in m_live_ranges. Returns true if the ranges were able
   *        to be joined.
   */
  bool join_ranges(u32 a_index, u32 b_index);

  /*!
   * @brief Helper that finds any registers with existing assignments in a
   *        range. The range is specified by a starting index into m_live_ranges
   *        and an ending position in the RTL instruction sequence.
   */
  RegisterSet fixed_in_range(HwRegister::Type type,
                             std::vector<LiveRange>::iterator from,
                             u32 until_instruction) const;

  /*!
   * @brief Debug method which prints an ASCII diagram of the calculated
   *        LiveRange data.
   */
  void debug_draw_ranges();
};

}
}
