#pragma once

#include "fox/fox_types.h"

namespace fox {
namespace jit {

/*!
 * @class fox::jit::HwRegister
 * @brief A hardware register assignment (or potential set of assignments)
 *        for an RTL instruction.
 */
class HwRegister {
public:
  /*!
   * @brief The maximum number of register types that can be defined.
   */
  static constexpr unsigned MaxTypes = 4;

  /*!
   * @brief Type used to refer to register sets, without allowing an implicit
   *        cast from integers.
   *
   * The 0th type is reserved to refer to spill registers.
   */
  enum class Type : u8 {};

  /*!
   * @brief Special value for Type that indicates the register was assigned to
   *        spill memory.
   */
  static constexpr Type Spill = Type(0);

  HwRegister() : m_assigned(0), m_type(0), m_index(0x7777)
  {
    return;
  }

  HwRegister(const Type type) : m_assigned(0), m_type((u8)type), m_index(0x7777)
  {
    assert(m_type < MaxTypes);
  }

  HwRegister(const Type type, const u32 value)
    : m_assigned(1),
      m_type((u8)type),
      m_index(value)
  {
    return;
  }

  /*!
   * @brief Two HwRegister's are equal if they are either both unassigned of
   *        the same register type, or are both assigned the same index.
   */
  bool operator==(const HwRegister &other) const
  {
    if (!m_assigned && !other.m_assigned) {
      return m_type == other.m_type;
    } else if (m_assigned && other.m_assigned) {
      return m_type == other.m_type && m_index == other.m_index;
    }
    return false;
  }

  bool operator!=(const HwRegister &other) const
  {
    return !(*this == other);
  }

  Type type() const
  {
    return (Type)m_type;
  }

  u32 index() const
  {
    assert(!is_spill());
    return m_index;
  }

  u32 spill_index() const
  {
    assert(is_spill());
    return m_index;
  }

  u32 raw_index() const
  {
    return m_index;
  }

  bool is_spill() const
  {
    return Type(m_type) == Spill;
  }

  bool assigned() const
  {
    return m_assigned;
  }

private:
  /*!
   * @brief If set, a specific hardware register has been assigned. If not set,
   *        only m_type is valid.
   */
  u32 m_assigned : 1;

  /*!
   * @brief Identifier for the set of registers that can be assigned. This field
   *        is always set. Register identities are not specific to a set -
   *        this is just a way to refer to a subset of the potential m_index
   *        values.
   *
   * A value of 0 means the register is in spill memory.
   */
  u32 m_type : 7;

  /*!
   * @brief If m_assigned is set, this is the hardware register that was
   *        assigned.
   */
  u32 m_index : 24;
};

/*!
 * @class fox::jit::RtlRegister
 * @brief A SSA register allocation. In addition to the assigned SSA ID, the
 *        codegen backend can store type information which will automatically
 *        be propagated by the register allocator.
 */
class RtlRegister {
public:
  RtlRegister() : m_unused(0), m_valid(0), m_type(0), m_index(0)
  {
    return;
  }

  explicit RtlRegister(const unsigned value)
    : m_unused(0),
      m_valid(1),
      m_type(0),
      m_index(value)
  {
    assert(value <= 16777215);
  }

  RtlRegister(const unsigned value, const unsigned type)
    : m_unused(0),
      m_valid(1),
      m_type(type),
      m_index(value)
  {
    assert(value <= 16777215);
    assert(type <= 15);
  }

  bool operator==(const RtlRegister &other) const
  {
    if (!m_valid && !other.m_valid) {
      return true;
    } else if (m_valid && other.m_valid && m_index == other.m_index) {
      return true;
    }

    return false;
  }

  bool operator!=(const RtlRegister &other) const
  {
    return !(*this == other);
  }

  size_t type() const
  {
    return m_type;
  }

  u32 index() const
  {
    return m_index;
  }

  bool valid() const
  {
    return m_valid;
  }

  operator u32() const
  {
    return m_index;
  }

private:
  u32 m_unused : 3;

  /*!
   * @brief Set to 1 if this is a valid SSA register allocation. Default
   *        constructed instances are invalid.
   */
  u32 m_valid : 1;

  /*!
   * @brief Type information controlled by the codegen backend. Optional, but
   *        must be preserved by any RTL passes including register allocation.
   */
  u32 m_type : 4;

  /*!
   * @brief The SSA register index. Only contains valid information if m_valid
   *        is true.
   */
  u32 m_index : 24;
};

}
}
