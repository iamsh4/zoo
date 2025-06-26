#include <map>
#include <cstdio>
#include <climits>
#include <fmt/printf.h>

#include "fox/jit/linear_register_allocator.h"

namespace fox {
namespace jit {


/*****************************************************************************
 * RangeSet                                                                  *
 *****************************************************************************/

void
RangeSet::add_range(const u32 id, u32 start, u32 end)
{
  assert(end > start);

  /* Check to see if a range came before this range that we can merge
   * with. */
  const auto before_it = m_data.find(std::make_pair(id, start));
  if (before_it != m_data.end()) {
    start = before_it->second;
    m_data.erase(before_it);
  }

  /* Check to see if there's a range after this one that can be merged
   * with. */
  const auto after_it = m_data.upper_bound(std::make_pair(id, end));
  if (after_it != m_data.end() && after_it->first.first == id) {
    if (after_it->second == end) {
      end = after_it->first.second;
      m_data.erase(after_it);
    }
  }

  m_data.emplace(std::make_pair(id, end), start);
}

bool
RangeSet::is_contended(const u32 id, const u32 position) const
{
  const auto it = m_data.upper_bound(std::make_pair(id, position));
  if (it == m_data.end() || it->first.first != id) {
    return false;
  }

  return it->second <= position;
}

bool
RangeSet::is_contended_range(const u32 id,
                             const u32 start,
                             const u32 end) const
{
  auto it = m_data.upper_bound(std::make_pair(id, start));
  if (it == m_data.end() || it->first.first != id) {
    return false;
  }

  return it->second < end;
}

void
RangeSet::debug_print()
{
  for (const auto &entry : m_data) {
    printf(
      "[ID:%u]: range(%u, %u)\n", entry.first.first, entry.second, entry.first.second);
  }
}

/*****************************************************************************
 * LinearAllocator                                                           *
 *****************************************************************************/

LinearAllocator::LinearAllocator()
{
  for (u32 i = 0; i < HwRegister::MaxTypes; ++i) {
    /* Default spill limit of 64. */
    const HwRegister::Type type = HwRegister::Type(i);
    m_hw_registers[i] = RegisterSet(type, type == HwRegister::Spill ? 64 : 0);
  }
}

LinearAllocator::~LinearAllocator()
{
  return;
}

void
LinearAllocator::define_register_type(const RegisterSet available)
{
  assert(available.type() != HwRegister::Spill);
  m_hw_registers[(u8)available.type()] = available;
}

RtlProgram
LinearAllocator::execute(RtlProgram &&input)
{
  m_target = std::move(input);

  /* TODO */
  assert(m_target.block_count() == 1lu);

  prepare();
  calculate_live_ranges();
  join_live_ranges();
  assign_registers();
  encode_rtl();
  prune_rtl();

  for (u32 i = 0; i < HwRegister::MaxTypes; ++i) {
    m_target.set_register_usage(m_hw_unused[i]);
  }

  return std::move(m_target);
}

void
LinearAllocator::prepare()
{
  RtlInstructions &block = m_target.block(0);
  std::unique_ptr<RtlInstructions> result(new RtlInstructions(block.label()));
  for (RtlInstruction &entry : block) {
    /* If any input registers are fixed, rename the IR register and insert a
     * move. */
    for (u32 i = 0; i < entry.source_count; ++i) {
      if (!entry.source(i).rtl.valid()) {
        continue;
      }

      assert(entry.source(i).rtl < m_target.ssa_count());
      if (!entry.source(i).hw.assigned()) {
        continue;
      }

      const RtlRegister renamed = m_target.ssa_allocate(entry.source(i).rtl.type());

      RtlInstruction move(1u, 1u);
      move.op = u16(RtlOpcode::Move);
      move.result(0) = RegisterAssignment { renamed, entry.source(i).hw };
      move.source(0) = RegisterAssignment { entry.source(i).rtl,
                                            HwRegister(entry.source(i).hw.type()) };
      result->push_back(move);

      entry.source(i).rtl = renamed;
    }

    /* Copy instruction forward to result stream, maintaining a reference in
     * case IR registers need to be re-assigned with moves below. */
    assert(!(entry.op & (1lu << 31)));
    RtlInstructions::iterator entry_it = result->end();
    result->push_back(entry);

    /* If any output registers are fixed, rename the IR register and insert a
     * move. */
    for (u32 i = 0; i < entry.result_count; ++i) {
      if (!entry.result(i).rtl.valid()) {
        continue;
      }

      assert(entry.result(i).rtl < m_target.ssa_count());

      if (!entry.result(i).hw.assigned()) {
        continue;
      }

      const RtlRegister renamed = m_target.ssa_allocate(entry.result(i).rtl.type());

      RtlInstruction move(1u, 1u);
      move.op = u16(RtlOpcode::Move);
      move.result(0) =
        RegisterAssignment { entry.result(i).rtl, HwRegister(entry.result(i).hw.type()) };
      move.source(0) = RegisterAssignment { renamed, entry.result(i).hw };
      result->push_back(move);

      entry_it->result(i).rtl = renamed;
    }
  }

  m_target.update_block(0, std::move(result));
}

void
LinearAllocator::calculate_live_ranges()
{
  m_live_ranges.clear();
  m_live_ranges.reserve(m_target.ssa_count());
  m_ranges_reverse.resize(m_target.ssa_count(), UINT_MAX);

  size_t i = 0lu;
  RtlInstructions &block = m_target.block(0);
  for (auto it = block.begin(); it != block.end(); ++it, ++i) {
    RtlInstruction &entry = *it;
    for (u32 j = 0u; j < entry.source_count; ++j) {
      if (!entry.source(j).rtl.valid()) {
        /* Allocation disabled for this register. */
        continue;
      }

      const u32 range_index = m_ranges_reverse[entry.source(j).rtl];

      assert(range_index < m_live_ranges.size());
      assert(!entry.source(j).hw.assigned() ||
             entry.source(j).hw == m_live_ranges[range_index].hw);
      m_live_ranges[range_index].to = i;
    }

    /* TODO State saving logic needs some additional verification when used with
     *      multiple results. At which point in the results should we save the
     *      state? */
    assert(!entry.flags.check(RtlFlag::SaveState) || entry.result_count <= 1);

    for (u32 j = 0u; j < entry.result_count; ++j) {
      if (!entry.result(j).rtl.valid()) {
        /* Allocation disabled for this register. */
        continue;
      }

      /* Results always represent the start of a range. */
      assert(m_ranges_reverse[entry.result(j).rtl] == UINT_MAX);
      m_ranges_reverse[entry.result(j).rtl] = m_live_ranges.size();
      m_live_ranges.emplace_back(LiveRange {
        .rtl = entry.result(j).rtl,
        .hw = entry.result(j).hw,
        .state = entry.flags.check(RtlFlag::SaveState) ? &entry.saved_state() : nullptr,
        .from = u32(i),
        .to = u32(i) + 1u,
        .parent = UINT32_MAX,
      });
    }

    if (entry.result_count == 0 && entry.flags.check(RtlFlag::SaveState)) {
      /* Instructions with no output are not usually processed in the allocation
       * phase. If the instruction needs to know register state, insert a place-
       * holder range so state will be captured at the appropriate time. */
      m_live_ranges.emplace_back(LiveRange {
        .rtl = RtlRegister(),
        .hw = HwRegister(),
        .state = &entry.saved_state(),
        .from = u32(i),
        .to = u32(i) + 1u,
        .parent = UINT32_MAX,
      });
    }
  }

#if 0
  printf("Initial live range list:\n");
  debug_draw_ranges();
#endif
}

void
LinearAllocator::join_live_ranges()
{
  for (auto &ranges : m_hw_ranges) {
    ranges.clear();
  }

  /* Build the initial contention map for hardware register allocations. */
  for (const auto &range : m_live_ranges) {
    if (range.hw.assigned()) {
      m_hw_ranges[(u8)range.hw.type()].add_range(range.hw.index(), range.from, range.to);
    }
  }

  /* Set of ranges that include the current instruction. The range_end key
   * value may be stale, and is checked + updated during removal (and inserted
   * again if necessary). Maps: range end => m_live_ranges index */
  const RtlInstructions &block = m_target.block(0);
  std::multimap<uint32_t, uint32_t> active;
  size_t i = 0lu;
  for (auto it = block.begin(); it != block.end(); ++it, ++i) {
    const RtlInstruction &instruction = *it;

    /* Update active live ranges */
    for (auto it = active.begin(); it != active.end() && it->first <= i;) {
      it = active.erase(it);
    }

    /* Instructions with no outputs are not candidates for joining. */
    if (instruction.result_count == 0 || !instruction.result(0).rtl.valid()) {
      continue;
    }

    /* For destructive opcodes, attempt to merge the result's range with the
     * first source's range. */
    if (instruction.flags.check(RtlFlag::Destructive)) {
      assert(instruction.result_count > 0 && instruction.source_count >= 1u);
      join_ranges(m_ranges_reverse[instruction.result(0).rtl],
                  m_ranges_reverse[instruction.source(0).rtl]);

      /* Don't attempt to merge with a source other than 0, to avoid extra
       * moves required to re-order. */
      /* TODO Most instructions could certainly have operands re-ordered just
       *      fine - add, mul, bitwise, etc. so this should work with the
       *      Unordered flag. */
      continue;
    }

#if 0
    /* XXX Redundant with the loop below. Keep? */
    /* For move instructions, attempt to merge the source and destination
     * ranges. */
    if (instruction.op == u16(RtlOpcode::Move)) {
      join_ranges(m_ranges_reverse[instruction.result(0).rtl],
                  m_ranges_reverse[instruction.source(0).rtl]);
      continue;
    }
#endif

    /* For any other instructions, attempt to merge result with either source
     * if possible. */
    for (u32 j = 0; j < instruction.source_count; ++j) {
      const bool joined = join_ranges(m_ranges_reverse[instruction.result(0).rtl],
                                      m_ranges_reverse[instruction.source(j).rtl]);
      if (joined) {
        break;
      }
    }
  }

#if 0
  printf("Live ranges after joining:\n");
  debug_draw_ranges();
#endif
}

void
LinearAllocator::assign_registers()
{
  /* Track registers of each type separately. The initial set of registers (and
   * virtual registers for spill) are initialized from the hardware register set
   * provided by the caller. */
  std::array<RegisterSet, HwRegister::MaxTypes> available;
  for (u32 i = 0; i < HwRegister::MaxTypes; ++i) {
    available[i] = m_hw_registers[i];
    m_hw_unused[i] = m_hw_registers[i];
  }

  /* For all liveliness ranges that intersect the current time point map
   * from end of the range to its position in the m_live_ranges array. */
  std::multimap<u32, std::vector<LiveRange>::iterator> active;
  for (auto it = m_live_ranges.begin(); it != m_live_ranges.end(); ++it) {
    /* For any ranges that are finishing, remove them from the current
     * contention set. */
    for (auto active_it = active.begin(); active_it != active.end();) {
      if (active_it->first > it->from) {
        break;
      }

      const HwRegister reg = active_it->second->hw;
      assert(reg.assigned());
      available[(u8)reg.type()].free(reg);
      active_it = active.erase(active_it);
    }

    /* If requested, save the list of all allocated registers at this point in
     * time. */
    if (it->state != nullptr) {
      *it->state = available;
    }

    /* Ranges that have parents don't need assignment. The parent range handles
     * all allocation. */
    if (it->parent != UINT32_MAX) {
      continue;
    }

    /* Ranges created without an associated RTL do not need to be tracked as
     * active. They are only used to record state. */
    if (!it->rtl.valid()) {
      /* TODO Should this still assert that a fixed hardware assignment on
       *      this range can't be in the allocated set? */
      continue;
    }

    assert(it->hw.type() != HwRegister::Spill);
    active.emplace(it->to, it);

    /* If the register for this range is already assigned don't modify it.
     * Only update the current allocator state. */
    if (it->hw.assigned()) {
      assert(available[(u8)it->hw.type()].is_free(it->hw)); /* XXX Spilling */
      assert(it->hw.type() != HwRegister::Spill);
      available[(u8)it->hw.type()].mark_allocated(it->hw);
      m_hw_unused[(u8)it->hw.type()].mark_allocated_unchecked(it->hw);
      continue;
    }

    /* Allocate a new register for the range. If there are fixed registers
     * already assigned to upcoming ranges we will overlap, avoid them. */
    RegisterSet overlap_hw(available[(u8)it->hw.type()]);
    overlap_hw.mark_allocated(fixed_in_range(it->hw.type(), it + 1, it->to));
    if (!overlap_hw.empty()) {
      it->hw = overlap_hw.allocate();
      available[(u8)it->hw.type()].mark_allocated(it->hw);
      m_hw_unused[(u8)it->hw.type()].mark_allocated_unchecked(it->hw);
      continue;
    }

    /* No registers are available. Use spill memory. */
    /* TODO There are lots of options here. We could kick an existing assignment
     *      into the spill area and re-use it here, then give the first one a
     *      new assignment (if not fixed) later. i.e. bin packing
     *      For now we'll do the simplest possible thing which is to have a set
     *      of spill registers and assign to them as if they were normal
     *      registers, albeit much slower. The RTL emitter backend will handle
     *      inserting the spill instructions. */
    assert(!available[(u8)HwRegister::Spill].empty());
    const HwRegister reg = available[(u8)HwRegister::Spill].allocate();
    m_hw_unused[(u8)HwRegister::Spill].mark_allocated_unchecked(reg);
    it->hw = reg;
  }
}

void
LinearAllocator::encode_rtl()
{
  RtlInstructions &block = m_target.block(0);
  for (RtlInstruction &instruction : block) {
    for (u32 i = 0; i < instruction.result_count; ++i) {
      const RtlRegister rtl = instruction.result(i).rtl;
      if (rtl.valid()) {
        LiveRange &range = m_live_ranges[m_ranges_reverse[rtl]];
        const HwRegister hw =
          range.parent == UINT32_MAX ? range.hw : m_live_ranges[range.parent].hw;
        assert(!instruction.result(i).hw.assigned() || instruction.result(i).hw == hw);
        instruction.result(i).hw = hw;
      }
    }

    for (u32 i = 0; i < instruction.source_count; ++i) {
      const RtlRegister rtl = instruction.source(i).rtl;
      if (!rtl.valid()) {
        continue;
      }

      LiveRange &range = m_live_ranges[m_ranges_reverse[rtl]];
      const HwRegister hw =
        range.parent == UINT32_MAX ? range.hw : m_live_ranges[range.parent].hw;
      assert(!instruction.source(i).hw.assigned() || instruction.source(i).hw == hw);
      instruction.source(i).hw = hw;
    }
  }
}

void
LinearAllocator::prune_rtl()
{
  RtlInstructions &block = m_target.block(0);
  for (RtlInstruction &instruction : block) {
    if (instruction.op == u16(RtlOpcode::Move)) {
      if (instruction.result(0).hw == instruction.source(0).hw) {
        /* The move instruction was successfully coalesced by the range join
         * operations. It can safely be removed. */
        instruction.op = u16(RtlOpcode::None);
      }
    }
  }
}

bool
LinearAllocator::join_ranges(u32 a_index, u32 b_index)
{
  if (m_live_ranges[a_index].parent != UINT32_MAX) {
    a_index = m_live_ranges[a_index].parent;
  }

  if (m_live_ranges[b_index].parent != UINT32_MAX) {
    b_index = m_live_ranges[b_index].parent;
  }

  if (a_index > b_index) {
    std::swap(a_index, b_index);
  } else if (a_index == b_index) {
    /* No-op */
    return true;
  }

  LiveRange &target = m_live_ranges[a_index];
  LiveRange &later = m_live_ranges[b_index];
  assert(target.parent == UINT32_MAX);
  assert(later.parent == UINT32_MAX);
  assert(target.from <= later.from);

  if (target.to > later.from || target.from == later.from) {
    /* Ranges overlap. */
    return false;
  } else if (target.hw.type() != later.hw.type()) {
    /* Register types are different. */
    return false;
  }

  /* Check for fixed hw register assignments in the candidate ranges. */
  HwRegister fixed_hw(target.hw.type());
  if (target.hw.assigned()) {
    if (later.hw.assigned() && later.hw != target.hw) {
      /* Mismatched fixed register allocation. */
      return false;
    }
    fixed_hw = target.hw;
  } else if (later.hw.assigned()) {
    fixed_hw = later.hw;
  }

  /* Check for fixed register contention with other live ranges. */
  u32 new_fixed_start = 0, new_fixed_end = 0;
  if (fixed_hw.assigned()) {
    if (!later.hw.assigned()) {
      /* Fixed register comes from earlier range. */
      new_fixed_start = target.to;
      new_fixed_end = later.to;
    } else if (!target.hw.assigned()) {
      /* Fixed register comes from later range. */
      new_fixed_start = target.from;
      new_fixed_end = later.from;
    } else {
      /* Both have a fixed range already. Only check the hole between ranges. */
      new_fixed_start = target.to;
      new_fixed_end = later.from;
    }

    /* If there's a hole that will be filled by combining ranges, check for
     * existing contention on the register assignment. */
    if (new_fixed_start != new_fixed_end) {
      const RangeSet &ranges = m_hw_ranges[(u8)fixed_hw.type()];
      if (ranges.is_contended_range(fixed_hw.index(), new_fixed_start, new_fixed_end)) {
        return false;
      }
    }
  }

#if 0
  printf("join RTL:%u to RTL:%u\n", later.rtl, target.rtl);
#endif

  /* Join the two liveliness ranges. */
  target.hw = fixed_hw;
  target.to = later.to;
  later.parent = a_index;

  /* If there was a fixed register and ranges were added to its allocation,
   * update the range set. */
  if (new_fixed_start != new_fixed_end) {
    RangeSet &ranges = m_hw_ranges[(u8)fixed_hw.type()];
    ranges.add_range(fixed_hw.index(), new_fixed_start, new_fixed_end);
  }

  return true;
}

RegisterSet
LinearAllocator::fixed_in_range(const HwRegister::Type type,
                                std::vector<LiveRange>::iterator it,
                                const u32 until_instruction) const
{
  /* TODO Maybe have a "fast" lookup for the common case where most registers
   *      don't have any kind of fixed assignment? */
  RegisterSet result(m_hw_registers[(u8)type]);
  while (it != m_live_ranges.end() && it->from < until_instruction) {
    if (it->parent == UINT32_MAX && it->hw.assigned() && it->hw.type() == type) {
      if (result.is_free(it->hw)) {
        result.mark_allocated(it->hw);
      }
    }
    ++it;
  }

  return result;
}

void
LinearAllocator::debug_draw_ranges()
{
  const RtlInstructions &block = m_target.block(0);

  printf("\tRTL   ");
  for (u32 i = 0; i < block.size(); ++i) {
    printf("|%3u|", i);
  }
  printf("|\n");

  /* Loop over all ranges twice. First time only print the final range set.
   * Second time print ranges that were joined to a parent. */
  for (u32 set = 0; set < 2; ++set) {
    for (u32 i = 0; i < m_live_ranges.size(); ++i) {
      const LiveRange &range = m_live_ranges[i];

      if ((range.parent != UINT32_MAX) ^ (set == 1)) {
        continue;
      }

      printf("\t%3u   ", range.rtl.index());
      for (u32 j = 0; j < range.from; ++j) {
        printf("     ");
      }

      if (set == 0) {
        if (range.from == range.to) {
          printf("  #  ");
        } else {
          printf("  ###");
          for (u32 j = range.from + 1u; j < range.to; ++j) {
            printf("#####");
          }
          printf("##   ");
        }
      } else {
        if (range.from == range.to) {
          printf("  -  ");
        } else {
          printf("  ---");
          for (u32 j = range.from + 1u; j < range.to; ++j) {
            printf("-----");
          }
          printf("--   ");
        }
      }

      for (u32 j = range.to; j < block.size(); ++j) {
        printf("     ");
      }
      if (range.parent != UINT32_MAX) {
        printf(" (merged with RTL:%u)", m_live_ranges[range.parent].rtl.index());
      }
      if (range.hw.assigned()) {
        printf(" (HW:%u)", range.hw.index());
      }
      printf(" (%u -> %u)", range.from, range.to);
      printf("\n");
    }
  }
}

}
}
