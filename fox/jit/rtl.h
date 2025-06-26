// vim: expandtab:ts=2:sw=2

#pragma once

#include <array>
#include <initializer_list>
#include <functional>
#include <memory>
#include <vector>
#include <string>
#include <cstring>

#include "fox/ir_types.h"
#include "fox/jit/types.h"

namespace fox {
namespace jit {

using Value = ir::Constant;

/*!
 * @struct fox::jit::RegisterAssignment
 * @brief Correspondence between an RTL virtual register assignment and a
 *        hardware register assignment. The virtual register is typically an
 *        input to a register allocator and the hardware register an output.
 */
struct RegisterAssignment {
  /* Register transfer language SSA form assignment. */
  RtlRegister rtl;

  /* Hardware level register assignment. */
  HwRegister hw;
};

/*!
 * @class fox::jit::RegisterSet
 * @brief Very basic representation of a set of available / in-use registers
 *        using a bitmap data structure. The registers must be numbered from
 *        0 to N-1, with N at most 64.
 */
class RegisterSet {
public:
  explicit RegisterSet(const HwRegister::Type type = HwRegister::Spill,
                       const unsigned register_count = 0)
    : m_type(type),
      m_count(register_count),
      m_state(register_count < 64u ? (1lu << register_count) - 1lu : ~(0lu))
  {
    assert(register_count <= 64);
  }

  bool operator==(const RegisterSet &other) const
  {
    return m_type == other.m_type && m_state == other.m_state;
  }

  RegisterSet operator|(const RegisterSet &other) const
  {
    assert(m_type == other.m_type);

    RegisterSet result;
    result.m_state = m_state | other.m_state;
    return result;
  }

  RegisterSet operator&(const RegisterSet &other) const
  {
    assert(m_type == other.m_type);

    RegisterSet result(*this);
    result.m_state = m_state & other.m_state;
    return result;
  }

  RegisterSet operator~() const
  {
    RegisterSet result(*this);
    result.m_state = ~m_state;
    return result;
  }

  HwRegister::Type type() const
  {
    return m_type;
  }

  HwRegister allocate()
  {
    assert(m_state != 0lu);
    const unsigned index = __builtin_ctzl(m_state);
    m_state &= ~(1lu << index);
    return HwRegister(m_type, index);
  }

  void mark_allocated(const HwRegister hw)
  {
    assert((m_state & (1lu << hw.raw_index())) != 0lu);
    assert(hw.type() == m_type);
    m_state &= ~(1lu << hw.raw_index());
  }

  void mark_allocated(const RegisterSet &other)
  {
    assert(m_type == other.m_type);
    m_state = m_state & other.m_state;
  }

  void mark_allocated_unchecked(const HwRegister hw)
  {
    assert(hw.type() == m_type);
    m_state &= ~(1lu << hw.raw_index());
  }

  void free(const HwRegister hw)
  {
    assert((m_state & (1lu << hw.raw_index())) == 0lu);
    assert(hw.type() == m_type);
    m_state |= (1lu << hw.raw_index());
  }

  bool is_free(const HwRegister hw) const
  {
    assert(hw.type() == m_type);
    return !!(m_state & (1lu << hw.raw_index()));
  }

  bool empty() const
  {
    return m_state == 0lu;
  }

  unsigned available_count() const
  {
    return __builtin_popcountl(m_state);
  }

  unsigned allocated_count() const
  {
    return m_count - __builtin_popcountl(m_state);
  }

  unsigned total_count() const
  {
    return m_count;
  }

private:
  /*!
   * @brief The register type that this instance allocates.
   */
  HwRegister::Type m_type;

  /*!
   * @brief The total number of registers in this set.
   */
  u8 m_count;

  /*!
   * @brief Bitmap tracking which registers are currently allocated. A '1' bit
   *        indicates the register is currently available. A '0' bit indicates
   *        the register is already allocated or does not exist in this set.
   */
  uint64_t m_state;
};

typedef std::array<RegisterSet, HwRegister::MaxTypes> RegisterState;

/*!
 * @enum  fox::jit::RtlOpcode
 * @brief Opcode IDs reserved for use by the register allocator or with special
 *        meaning to the allocator.
 *
 * All of these opcodes must have the MSB set. Lower values are reserved for
 * use by the calling code.
 */
enum class RtlOpcode : u16
{
  /* Move the contents of one register to another. This is inserted by the
   * register allocator to handle register constraints if the target register
   * could not be merged with its source. */
  Move = (1lu << 15u) | 100,

  /* No operation. This can be used to replace instructions that are no longer
   * necessary after register allocation. Backend logic should ignore these
   * instructions and all their associated state. */
  None = (1lu << 15u) | 101,

  /* Placeholder for uninitialized RtlInstructions. */
  Invalid = 0xffffu,
};

/*!
 * @enum  fox::jit::RtlFlag
 * @brief Flags that control register assignment for RTL instructions. These
 *        form a bitmask in the RtlInstruction::flags field.
 */
enum class RtlFlag : u16
{
  /* The instruction that will be emitted uses the first source as the output.
   * The source operand is therefore destroyed. If set, the register allocator
   * will attempt to merge the source and result register assignments. */
  Destructive,

  /* The instruction's meaning doesn't change if the order of the source
   * operands is changed. Currently not used for anything. */
  Unordered,

  /* Request a snapshot of register state to be saved at the time of this
   * instruction. Used for instructions that need to save and restore register
   * state. */
  SaveState,
};

typedef FlagSet<RtlFlag, u16> RtlFlags;

/*!
 * @struct fox::jit::RtlInstruction
 * @brief Generic representation of an RTL (register transfer language)
 *        instruction. The encoding of the opcode details is handled by each
 *        backend, and treated as an opaque value to the register allocator.
 *
 * The structure size includes optional areas which do not need to be stored.
 * To determine the exact storage size required by an RtlInstruction call the
 * bytes() method.
 *
 * TODO Add constraints that are distinct from sources.
 */
struct RtlInstruction {
  /*!
   * @brief The maximum number of source and result constraints for an
   *        instruction.
   */
  static constexpr size_t OperandLimit = 8u;

  /*!
   * @brief The maximum number of bytes needed to store an RtlInstruction's
   *        opaque (type-dependent) fields.
   */
  static constexpr size_t MaxOpaqueStorage =
    sizeof(RegisterAssignment) * (OperandLimit) + sizeof(RegisterState);

  /*!
   * @brief Construct a new RtlInstruction with the provided configuration.
   */
  RtlInstruction(const unsigned source_count,
                 const unsigned result_count,
                 const RtlFlags flags = {})
    : flags(flags),
      result_count(result_count),
      source_count(source_count),
      m_source_start(flags.check(RtlFlag::SaveState) * sizeof(RegisterState)),
      m_result_start(m_source_start + source_count * sizeof(RegisterAssignment))
  {
    assert(source_count + result_count <= OperandLimit);
    if (flags.check(RtlFlag::SaveState)) {
      new (&m_opaque[0]) RegisterState();
    }

    for (unsigned i = 0; i < source_count; ++i) {
      new (&source(i)) RegisterAssignment;
    }

    for (unsigned i = 0; i < result_count; ++i) {
      new (&result(i)) RegisterAssignment;
    }
  }

  /*!
   * @brief Construct a new RtlInstruction with no opaque data.
   */
  RtlInstruction(const u16 _op,
                 const std::initializer_list<RegisterAssignment> &results,
                 const std::initializer_list<RegisterAssignment> &sources,
                 const RtlFlags flags = {})
    : op(_op),
      flags(flags),
      result_count(results.size()),
      source_count(sources.size()),
      m_source_start(flags.check(RtlFlag::SaveState) * sizeof(RegisterState)),
      m_result_start(m_source_start + source_count * sizeof(RegisterAssignment))
  {
    assert(source_count + result_count <= OperandLimit);
    if (flags.check(RtlFlag::SaveState)) {
      new (&m_opaque[0]) RegisterState();
    }

    for (unsigned i = 0; i < source_count; ++i) {
      new (&source(i)) RegisterAssignment(std::data(sources)[i]);
    }

    for (unsigned i = 0; i < result_count; ++i) {
      new (&result(i)) RegisterAssignment(std::data(results)[i]);
    }
  }

  /*!
   * @brief Construct a new RtlInstruction with the provided opaque data.
   */
  RtlInstruction(const u16 _op,
                 const Value _data,
                 const std::initializer_list<RegisterAssignment> &results,
                 const std::initializer_list<RegisterAssignment> &sources,
                 const RtlFlags flags = {})
    : m_data(_data),
      op(_op),
      flags(flags),
      result_count(results.size()),
      source_count(sources.size()),
      m_source_start(flags.check(RtlFlag::SaveState) * sizeof(RegisterState)),
      m_result_start(m_source_start + source_count * sizeof(RegisterAssignment))
  {
    assert(source_count + result_count <= OperandLimit);

    if (flags.check(RtlFlag::SaveState)) {
      new (&m_opaque[0]) RegisterState();
    }

    for (unsigned i = 0; i < source_count; ++i) {
      new (&source(i)) RegisterAssignment(std::data(sources)[i]);
    }

    for (unsigned i = 0; i < result_count; ++i) {
      new (&result(i)) RegisterAssignment(std::data(results)[i]);
    }
  }

  RtlInstruction(const RtlInstruction &other)
  {
    /* Do not write past the valid data range, to allow tight packing of
     * instructions in byte arrays based on bytes() size. */
    memcpy(reinterpret_cast<u8 *>(this), &other, other.bytes());
  }

  RtlInstruction &operator=(const RtlInstruction &other)
  {
    /* Do not write past the valid data range, to allow tight packing of
     * instructions in byte arrays based on bytes() size. */
    memcpy(reinterpret_cast<u8 *>(this), &other, other.bytes());
    return *this;
  }

  /*!
   * @brief Return the number of bytes used to store this instruction.
   */
  size_t bytes() const
  {
    return sizeof(RtlInstruction) - sizeof(m_opaque) +
           (sizeof(RegisterAssignment) * (source_count + result_count)) +
           (sizeof(RegisterState) * flags.check(RtlFlag::SaveState));
  }

  /*!
   * @brief Access the result register assignments for this instruction.
   */
  RegisterAssignment &result(const unsigned index)
  {
    assert(index < result_count);
    return reinterpret_cast<RegisterAssignment *>(&m_opaque[m_result_start])[index];
  }

  const RegisterAssignment &result(const unsigned index) const
  {
    assert(index < result_count);
    return reinterpret_cast<const RegisterAssignment *>(&m_opaque[m_result_start])[index];
  }

  /*!
   * @brief Access the source register assignments for this instruction.
   */
  RegisterAssignment &source(const unsigned index)
  {
    assert(index < source_count);
    return reinterpret_cast<RegisterAssignment *>(&m_opaque[m_source_start])[index];
  }

  const RegisterAssignment &source(const unsigned index) const
  {
    assert(index < source_count);
    return reinterpret_cast<const RegisterAssignment *>(&m_opaque[m_source_start])[index];
  }

  /*!
   * @brief Return the stored RegisterState from the register allocator that
   *        was in effect for this instruction. Only available if the
   *        RtlFlag::SaveState flag was set.
   */
  RegisterState &saved_state()
  {
    /* XXX Pointer math cleanup */
    assert(flags.check(RtlFlag::SaveState));
    return *reinterpret_cast<RegisterState *>(&m_opaque[0]);
  }

  const RegisterState &saved_state() const
  {
    /* XXX Pointer math cleanup */
    assert(flags.check(RtlFlag::SaveState));
    return *reinterpret_cast<const RegisterState *>(&m_opaque[0]);
  }

  /*!
   * @brief Get the implementation-specific data attached to this instruction.
   *
   * TODO Remove public data field and name this data().
   */
  const Value &get_data() const
  {
    return m_data;
  }

  /*!
   * @brief Opcode / codegen specific data storage.
   *
   * TODO Make private
   */
  union {
    u64 data = 0u;
    Value m_data;
  };

  /*!
   * @brief The backend specific instruction chosen. Values with the MSB set
   *        are reserved for use by the register allocator - see the enum
   *        RtlOpcode for details.
   *
   * Note: Placed after data for better structure packing.
   */
  u16 op = u16(RtlOpcode::Invalid);

  /*!
   * @brief Additional flags that control how the register allocator must treat
   *        this instruction. See fox::jit::RtlFlag for details.
   */
  const RtlFlags flags;

  /*!
   * @brief If true, the value of 'result' is valid and needs to be assigned.
   */
  const u8 result_count = 0u;

  /*!
   * @brief The number of input registers.
   */
  const u8 source_count = 0u;

  /* Note: 16 bits unused when applying 8-byte alignment. */

  /*!
   * @brief The assigned position of this instruction in the flattened RTL
   *        stream. Instruction positions are assigned by the register
   *        allocator.
   */
  u32 position = 0xffffffffu;

private:
  /*!
   * @brief The offset in bytes from the start of opaque data to the start of
   *        the source assignments array.
   */
  const u16 m_source_start = 0u;

  /*!
   * @brief The offset in bytes from the start of opaque data to the start of
   *        the result assignments array.
   */
  const u16 m_result_start = 0u;

  /*!
   * @brief Generic storage for register assignments and saved state. The
   *        layout of the storage is always saved state, then sources, then
   *        results.
   *
   * This storage is sufficient for all supported configurations but is
   * typically larger than necessary. This ensures the structure can be
   * instantiated as a normal member, stack variable, etc. When stored in
   * specialized containers it can be more tightly packed using bytes() to
   * get the useful size for a particular configuration.
   *
   * Note: This layout must start with saved state because saved state has the
   *       greatest alignment requirements.
   * Note: Opaque storage must always be the last member of this structure.
   */
  alignas(RegisterState) uint8_t m_opaque[MaxOpaqueStorage];

  /* Ensure alignment of data placed in _opaque will be honored. */
  static_assert(alignof(RegisterAssignment) <= alignof(RegisterState));
};

/*!
 * @brief Basic sequence of register transfer language instructions representing
 *        a single EBB in an RtlProgram.
 */
class RtlInstructions {
public:
  RtlInstructions(const std::string &label);

  const std::string &label() const
  {
    return m_label;
  }

  /* XXX Register allocator relies on this not invalidating iterators, and
   *     the end() iterator pointing to the new instruction! */
  void push_back(const RtlInstruction &instruction)
  {
    const size_t old_size = m_instructions.size();
    m_instructions.resize(old_size + instruction.bytes());
    memcpy(m_instructions.data() + old_size, &instruction, instruction.bytes());
    ++m_instruction_count;
  }

  /* XXX Register allocator relies on this not invalidating iterators, and
   *     the end() iterator pointing to the new instruction! */
  void append(const u16 _op,
              const std::initializer_list<RegisterAssignment> &results,
              const std::initializer_list<RegisterAssignment> &sources,
              const RtlFlags flags = {})
  {
    const RtlInstruction instruction(_op, results, sources, flags);
    const size_t old_size = m_instructions.size();
    m_instructions.resize(old_size + instruction.bytes());
    memcpy(m_instructions.data() + old_size, &instruction, instruction.bytes());
    ++m_instruction_count;
  }

  /* XXX Register allocator relies on this not invalidating iterators, and
   *     the end() iterator pointing to the new instruction! */
  void append(const u16 _op,
              const Value _data,
              const std::initializer_list<RegisterAssignment> &results,
              const std::initializer_list<RegisterAssignment> &sources,
              const RtlFlags flags = {})
  {
    const RtlInstruction instruction(_op, _data, results, sources, flags);
    const size_t old_size = m_instructions.size();
    m_instructions.resize(old_size + instruction.bytes());
    memcpy(m_instructions.data() + old_size, &instruction, instruction.bytes());
    ++m_instruction_count;
  }

  size_t bytes() const
  {
    return m_instructions.size();
  }

  size_t size() const
  {
    return m_instruction_count;
  }

  void clear()
  {
    m_instructions.clear();
    m_instruction_count = 0u;
  }

  void debug_print(std::function<const char *(u16)> opcode_name);

  /*!
   * @class RtlInstructions::iterator
   */
  class iterator {
  public:
    iterator()
    {
      return;
    }

    iterator(RtlInstructions *const target, const size_t offset = 0lu)
      : m_target(target),
        m_offset(offset)
    {
      return;
    }

    iterator &operator++()
    {
      m_offset += (*this)->bytes();
      return *this;
    }

    iterator operator++(int) const
    {
      return ++iterator(*this);
    }

    RtlInstruction &operator*()
    {
      return *reinterpret_cast<RtlInstruction *>(&m_target->m_instructions[m_offset]);
    }

    RtlInstruction *operator->()
    {
      return reinterpret_cast<RtlInstruction *>(&m_target->m_instructions[m_offset]);
    }

    bool operator==(const iterator &other) const
    {
      return m_target == other.m_target && m_offset == other.m_offset;
    }

    bool operator!=(const iterator &other) const
    {
      return m_target != other.m_target || m_offset != other.m_offset;
    }

  private:
    RtlInstructions *const m_target = nullptr;
    size_t m_offset = 0lu;
  };

  iterator begin()
  {
    return iterator(this);
  }

  iterator end()
  {
    return iterator(this, m_instructions.size());
  }

  /*!
   * @class RtlInstructions::const_iterator
   */
  class const_iterator {
  public:
    const_iterator()
    {
      return;
    }

    const_iterator(const RtlInstructions *const target, const size_t offset = 0lu)
      : m_target(target),
        m_offset(offset)
    {
      return;
    }

    const_iterator &operator++()
    {
      m_offset += (*this)->bytes();
      return *this;
    }

    const_iterator operator++(int) const
    {
      return ++const_iterator(*this);
    }

    const RtlInstruction &operator*() const
    {
      return *reinterpret_cast<const RtlInstruction *>(
        &m_target->m_instructions[m_offset]);
    }

    const RtlInstruction *operator->() const
    {
      return reinterpret_cast<const RtlInstruction *>(
        &m_target->m_instructions[m_offset]);
    }

    bool operator==(const const_iterator &other) const
    {
      return m_target == other.m_target && m_offset == other.m_offset;
    }

    bool operator!=(const const_iterator &other) const
    {
      return m_target != other.m_target || m_offset != other.m_offset;
    }

  private:
    const RtlInstructions *const m_target = nullptr;
    size_t m_offset = 0lu;
  };

  const_iterator begin() const
  {
    return const_iterator(this);
  }

  const_iterator end() const
  {
    return const_iterator(this, m_instructions.size());
  }

private:
  /*!
   * @brief A human-readable label assigned to this block of instructions.
   *        Used for debugging, not for lookup.
   */
  const std::string m_label;

  /*!
   * @brief Opaque byte array used to store RtlInstruction instances with
   *        variable sizes.
   */
  std::vector<uint8_t> m_instructions;

  /*!
   * @brief The number of RtlInstruction instances stored by m_instructions.
   */
  size_t m_instruction_count = 0u;
};

/*!
 * @brief High level logic for constructing RTL programs with control flow.
 *        Each block is an extended basic block which has exactly one entrance
 *        (the first instruction) but can have more than one exit. Loops are
 *        not allowed.
 */
class RtlProgram {
public:
  /* TODO Use type-safe handle */
  typedef unsigned BlockHandle;

  RtlProgram();
  RtlProgram(RtlProgram &&from);
  RtlProgram &operator=(RtlProgram &&from);
  RtlProgram(const RtlProgram &) = delete;

  /*!
   * @brief Create a new RTL register with the specified type. Types are defined
   *        by the caller, but the value 0 should map to Any / Unknown.
   */
  RtlRegister ssa_allocate(const unsigned type)
  {
    return RtlRegister(m_next_ssa++, type);
  }

  /*!
   * @brief Return the number of RTL registers that have been allocated for this
   *        program.
   */
  uint32_t ssa_count() const
  {
    return m_next_ssa;
  }

  /*!
   * @brief Set the index of the next RTL register that will be allocated. This
   *        should only be used within register allocator logic.
   */
  void ssa_set_next(const uint32_t next_ir)
  {
    assert(next_ir >= m_next_ssa);
    m_next_ssa = next_ir;
  }

  /*!
   * @brief Returns the number of EBBs in this program.
   */
  size_t block_count() const
  {
    return m_blocks.size();
  }

  /*!
   * @brief Allocate a new EBB within the program. Instructions can be appended
   *        directly to the returned block, and other blocks can reference this
   *        block via control flow.
   */
  BlockHandle allocate_block(const std::string &label)
  {
    RtlInstructions *const block = new RtlInstructions(label);
    m_blocks.emplace_back(block);
    return m_blocks.size() - 1lu;
  }

  /*!
   * @brief Replace the indicated block new instructions. The new block may
   *        not add, remove, or change control flow. This API is only for the
   *        register allocator implementations.
   *
   * XXX There's probably a better way to expose this - probably on the block
   *     itself.
   */
  void update_block(const BlockHandle handle, std::unique_ptr<RtlInstructions> &&block)
  {
    assert(handle < m_blocks.size());
    m_blocks[handle] = std::move(block);
  }

  /*!
   * @brief Access an EBB within the program via its handle.
   */
  RtlInstructions &block(const BlockHandle handle)
  {
    assert(handle < m_blocks.size());
    return *m_blocks[handle].get();
  }

  /*!
   * @brief Access an EBB within the program via its handle.
   */
  const RtlInstructions &block(const BlockHandle handle) const
  {
    assert(handle < m_blocks.size());
    return *m_blocks[handle].get();
  }

  void set_register_usage(const RegisterSet &peak)
  {
    m_register_usage[(unsigned)peak.type()] = peak;
  }

  const RegisterSet &register_usage(const HwRegister::Type type)
  {
    return m_register_usage[(unsigned)type];
  }

  uint32_t spill_size() const
  {
    return m_register_usage[(unsigned)HwRegister::Spill].allocated_count();
  }

  void clear()
  {
    *this = RtlProgram();
  }

  void debug_print(std::function<const char *(u16)> opcode_name = [](u16){ return "OPCODE"; });

private:
  /*!
   * @brief The set of EBBs that make up this program. Until the program is
   *        flattened, these may be in any order.
   */
  std::vector<std::unique_ptr<RtlInstructions>> m_blocks;

  /*!
   * @brief The set of all registers used by the instruction sequence.
   *        Initialized by the register allocator.
   */
  RegisterState m_register_usage;

  /*!
   * @brief The next SSA index to be allocated.
   */
  uint32_t m_next_ssa = 0u;
};

}
}
