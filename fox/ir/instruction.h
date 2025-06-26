// vim: expandtab:ts=2:sw=2

#pragma once

#include <vector>
#include <initializer_list>
#include <cassert>
#include <cstring>

#include "fox/ir_opcode.h"
#include "fox/ir_operand.h"

namespace fox {
namespace ir {

/*!
 * @struct fox::ir::Instruction
 * @brief Generic representation of an IR instruction. The encoding has a
 *        variable length depending on the number of inputs and outputs.
 *
 * The structure size includes optional areas which do not need to be stored.
 * To determine the exact storage size required by an Instruction call the
 * bytes() method.
 */
struct Instruction {
  /*!
   * @brief The maximum number of source and result (total) operands for an
   *        instruction.
   */
  static constexpr size_t OperandLimit = 8u;

  /*!
   * @brief The maximum number of bytes needed to store an Instruction's
   *        opaque (opcode-dependent) fields.
   */
  static constexpr size_t MaxOpaqueStorage =
    sizeof(Operand) * (OperandLimit);

  /*!
   * @brief Construct a new Instruction with the provided configuration.
   */
  Instruction(const Opcode opcode,
              const Type type,
              const std::initializer_list<Operand> &results,
              const std::initializer_list<Operand> &sources)
    : m_opcode(opcode),
      m_type(type),
      m_result_count(results.size()),
      m_source_count(sources.size()),
      m_source_start(m_result_count * sizeof(Operand))
  {
    assert(m_source_count + m_result_count <= OperandLimit);

    for (unsigned i = 0; i < m_result_count; ++i) {
      new (const_cast<Operand *>(&result(i))) Operand(std::data(results)[i]);
    }

    for (unsigned i = 0; i < m_source_count; ++i) {
      new (const_cast<Operand *>(&source(i))) Operand(std::data(sources)[i]);
    }
  }

  Instruction(const Instruction &other)
  {
    /* Do not write past the valid data range, to allow tight packing of
     * instructions in byte arrays based on bytes() size. */
    memcpy(reinterpret_cast<u8 *>(this), &other, other.bytes());
  }

  Instruction &operator=(const Instruction &other)
  {
    /* Do not write past the valid data range, to allow tight packing of
     * instructions in byte arrays based on bytes() size. */
    memcpy(reinterpret_cast<u8 *>(this), &other, other.bytes());
    return *this;
  }

  /*!
   * @brief Return the IR language opcode for this instruction.
   */
  Opcode opcode() const
  {
    return m_opcode;
  }

  /*!
   * @brief The opcode-dependent type information for this instruction.
   */
  Type type() const
  {
    return m_type;
  }

  /*!
   * @brief Return the number of source operands for this instruction.
   */
  unsigned source_count() const
  {
    return m_source_count;
  }

  /*!
   * @brief Return the number of result operands for this instruction.
   */
  unsigned result_count() const
  {
    return m_result_count;
  }

  /*!
   * @brief Return the number of bytes used to store this instruction.
   */
  size_t bytes() const
  {
    return sizeof(Instruction) - sizeof(m_opaque) +
           (sizeof(Operand) * (m_source_count + m_result_count));
  }

  /*!
   * @brief Access the result Operand for this instruction.
   */
  const Operand &result(const unsigned index) const
  {
    assert(index < m_result_count);
    return reinterpret_cast<const Operand *>(m_opaque)[index];
  }

  /*!
   * @brief Access the source Operand for this instruction.
   */
  const Operand &source(const unsigned index) const
  {
    assert(index < m_source_count);
    return reinterpret_cast<const Operand *>(&m_opaque[m_source_start])[index];
  }

private:
  /*!
   * @brief The opcode of this instruction.
   */
  Opcode m_opcode = Opcode::None;

  /*!
   * @brief The opcode-dependent typing for this instruction. Unused for some
   *        opcodes.
   */
  Type m_type = Type::Integer64;

  /*!
   * @brief If true, the value of 'result' is valid and needs to be assigned.
   */
  u8 m_result_count = 0u;

  /*!
   * @brief The number of input registers.
   */
  u8 m_source_count = 0u;

  /*!
   * @brief The offset in bytes from the start of opaque data to the start of
   *        the result assignments array.
   */
  u8 m_source_start = 0u;

  /* Note: 32 bits wasted here as padding. */

  /*!
   * @brief Generic storage for register assignments and saved state. The
   *        layout of the storage is always saved state, then sources, then
   *        results.
   *
   * Note: This layout must start with saved state because saved state has the
   *       greatest alignment requirements. It must always be the last member
   *       of this structure.
   */
  alignas(Operand) uint8_t m_opaque[MaxOpaqueStorage];

  /* Ensure m_result_start can index the entire opaque storage. */
  static_assert(MaxOpaqueStorage <= 256);
};

/*!
 * @brief Basic container for a sequence of IR language instructions.
 *        Automatically packs Instruction objects using the minimum byte count
 *        necessary.
 */
class Instructions {
public:
  Instructions()
  {
    m_data.reserve(4096lu);
  }

  Instructions(const Instructions &other)
    : m_data(other.m_data),
      m_instruction_count(other.m_instruction_count)
  {
    return;
  }

  Instructions(Instructions &&other)
    : m_data(std::move(other.m_data)),
      m_instruction_count(other.m_instruction_count)
  {
    other.m_instruction_count = 0;
  }

  Instructions &operator=(const Instructions &other)
  {
    if (this != &other) {
      m_data = other.m_data;
      m_instruction_count = other.m_instruction_count;
    }
    return *this;
  }

  Instructions &operator=(Instructions &&other)
  {
    if (this != &other) {
      m_data = std::move(other.m_data);
      m_instruction_count = other.m_instruction_count;
      other.m_instruction_count = 0;
    }
    return *this;
  }

  void
  append(const Opcode opcode,
         const Type type,
         const std::initializer_list<Operand> &results,
         const std::initializer_list<Operand> &sources)
  {
    push_back(Instruction(opcode, type, results, sources));
  }

  void push_back(const Instruction &instruction)
  {
    const u8 *const bytes = reinterpret_cast<const u8 *>(&instruction);
    m_data.insert(m_data.end(), bytes, bytes + instruction.bytes());
    ++m_instruction_count;
  }

  size_t bytes() const
  {
    return m_data.size();
  }

  bool empty() const
  {
    return m_instruction_count == 0;
  }

  size_t size() const
  {
    return m_instruction_count;
  }

  void clear()
  {
    m_data.clear();
    m_instruction_count = 0u;
  }

  /*!
   * @brief Debug use only. Validate the contents of the internal data buffer.
   */
  void verify() const
  {
    size_t offset = 0lu, index = 0lu;
    while (index < m_instruction_count) {
      const Instruction *instruction =
        reinterpret_cast<const Instruction *>(m_data.data() + offset);

      assert(offset + instruction->bytes() <= m_data.size());

      offset += instruction->bytes();
      ++index;
    }

    assert(offset == m_data.size());
  }

  /*!
   * @class fox::ir::Instructions::const_iterator
   */
  class const_iterator {
  public:
    const_iterator()
    {
      return;
    }

    const_iterator(const Instructions *const target,
                   const u32 byte_offset = 0u,
                   const u32 index = 0u)
      : m_target(target),
        m_byte_offset(byte_offset),
        m_index(index)
    {
      return;
    }

    const_iterator &operator++()
    {
      m_byte_offset += (*this)->bytes();
      ++m_index;
      return *this;
    }

    const_iterator operator++(int) const
    {
      return ++const_iterator(*this);
    }

    const Instruction &operator*() const
    {
      return *reinterpret_cast<const Instruction *>(
        &m_target->m_data[m_byte_offset]);
    }

    const Instruction *operator->() const
    {
      return reinterpret_cast<const Instruction *>(
        &m_target->m_data[m_byte_offset]);
    }

    /*!
     * @brief Return the position of the IR instruction in within the instruction
     *        stream.
     */
    u32 index() const
    {
      return m_index;
    }

    bool operator==(const const_iterator &other) const
    {
      return m_target == other.m_target && m_byte_offset == other.m_byte_offset;
    }

    bool operator!=(const const_iterator &other) const
    {
      return m_target != other.m_target || m_byte_offset != other.m_byte_offset;
    }

  private:
    const Instructions *const m_target = nullptr;
    u32 m_byte_offset = 0u;
    u32 m_index = 0u;
  };

  const_iterator begin() const
  {
    assert(m_data.size() < UINT32_MAX);
    return const_iterator(this);
  }

  const_iterator end() const
  {
    assert(m_data.size() < UINT32_MAX);
    return const_iterator(this, m_data.size(), m_instruction_count);
  }

private:
  /*!
   * @brief Opaque byte array used to store Instruction instances with
   *        variable sizes.
   */
  std::vector<u8> m_data;

  /*!
   * @brief The number of Instruction instances stored by m_data.
   */
  size_t m_instruction_count = 0u;
};

}
}
