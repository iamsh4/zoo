// vim: expandtab:ts=2:sw=2

#pragma once

#include <vector>
#include <initializer_list>
#include <cassert>
#include <cstring>

#include "fox/ir_opcode.h"

namespace fox {
namespace ir {

/* TODO Cleanup */
class ExecutionUnit;

/*!
 * @struct fox::ir::Operand
 * @brief Operand to an IR instruction. This can be an input or output operand.
 *        This stores the constant value (if present)
 */
class Operand {
public:
  static constexpr u32 InvalidRegister = 0xffffffu;

  /*!
   * @brief Default operand constructor. The resulting Operand is not valid.
   */
  Operand()
    : m_type(u32(Type::Integer64)),
      m_is_constant(0u),
      m_unused0(0u),
      m_register(InvalidRegister),
      m_value({ .u64_value = 0lu })
  {
    return;
  }

  /*!
   * @brief Create Operand that refers to an inline stored constant value.
   */
  Operand(const Type type, const Constant value)
    : m_type(u32(type)),
      m_is_constant(1u),
      m_unused0(0u),
      m_register(0u),
      m_value(value)
  {
    return;
  }

  template<typename T>
  static Operand constant(const T value);

  bool operator==(const Operand &other) const
  {
    if (m_type != other.m_type) {
      return false;
    } else if (m_is_constant != other.m_is_constant) {
      return false;
    } else if (!m_is_constant) {
      return m_register == other.m_register;
    }

    /* TODO Optimize */
    switch (type()) {
      case Type::Integer8:
        return m_value.u8_value == other.m_value.u8_value;

      case Type::Integer16:
        return m_value.u16_value == other.m_value.u16_value;

      case Type::Integer32:
      case Type::Float32:
        return m_value.u32_value == other.m_value.u32_value;

      case Type::Integer64:
      case Type::Float64:
        return m_value.u64_value == other.m_value.u64_value;

      case Type::Bool:
        return m_value.bool_value == other.m_value.bool_value;

      case Type::BranchLabel:
        return m_value.label_value == other.m_value.label_value;

      case Type::HostAddress:
        return m_value.hostptr_value == other.m_value.hostptr_value;

      default:
        assert(false);
    }
  }

  bool operator!=(const Operand &other) const
  {
    return !(*this == other);
  }

  /*!
   * @brief Return whether this Operand has a valid register assignment.
   */
  bool is_valid() const
  {
    return m_register != InvalidRegister || m_is_constant == 1u;
  }

  /*!
   * @brief Return the IR register number this operand represents. The operand
   *        must be a register (is_register() returns true).
   */
  u32 register_index() const
  {
    assert(!is_constant() && is_valid());
    return m_register;
  }

  /*!
   * @brief Return the constant value stored by this operand. The operand must
   *        be a constant (is_constant() returns true).
   */
  Constant value() const
  {
    assert(is_constant() && is_valid());
    return m_value;
  }

  /*!
   * @brief Return the internal constant zero-extended to a u64.
   *
   * The Operand must be a integer constant.
   */
  u64 zero_extended() const
  {
    assert(is_numeric());
    switch (type()) {
      case Type::Integer8:
        return m_value.u8_value;
      case Type::Integer16:
        return m_value.u16_value;
      case Type::Integer32:
        return m_value.u32_value;
      case Type::Integer64:
        return m_value.u64_value;
      default:
        assert(false);
    }
    return 0;
  }

  /*!
   * @brief Return the internal constant sign-extended to an i64.
   *
   * The Operand must be a integer constant.
   */
  i64 sign_extended() const
  {
    assert(is_numeric());
    switch (type()) {
      case Type::Integer8:
        return (i8)m_value.u8_value;
      case Type::Integer16:
        return (i16)m_value.u16_value;
      case Type::Integer32:
        return (i32)m_value.u32_value;
      case Type::Integer64:
        return (i64)m_value.u64_value;
      default:
        assert(false);
    }
    return 0;
  }

  /*!
   * @brief Returns true if the constant value is 0 or 0.0f. Not valid for
   *        boolean values.
   */
  bool is_zero() const
  {
    assert(is_numeric());
    switch (type()) {
      case Type::Integer8:
        return m_value.u8_value == 0u;
      case Type::Integer16:
        return m_value.u16_value == 0u;
      case Type::Integer32:
        return m_value.u32_value == 0u;
      case Type::Integer64:
        return m_value.u64_value == 0lu;
      case Type::Float32:
        return m_value.f32_value == 0.0f;
      case Type::Float64:
        return m_value.f64_value == 0.0;
      default:
        assert(false);
    }
    return false;
  }

  Type type() const
  {
    return Type(m_type);
  }

  bool is_constant() const
  {
    return is_valid() && m_is_constant == 1u;
  }

  bool is_numeric() const
  {
    return is_valid() && m_is_constant == 1u && is_numeric_type(type());
  }

  bool is_register() const
  {
    return is_valid() && m_is_constant == 0u;
  }

private:
  /* TODO Only for access to register index constructor */
  friend class ExecutionUnit;

  /*
   * Total size is 128-bits. There are 32 bits for metadata, 32 bits for the
   * register index (if this is a register), and 64 bits for the constant value
   * (if it's a constant).
   *
   * If m_inline_constant is true, m_value holds a value of the specified type.
   * Otherwise m_value is unused and m_register holds the register index.
   */
  u32 m_type : 4;
  u32 m_is_constant : 1;
  u32 m_unused0 : 27;
  u32 m_register : 32;
  Constant m_value;

  /*!
   * @brief Create Operand that refers to a register stored in an ExecutionUnit.
   */
  Operand(const Type type, const u32 register_index)
    : m_type((u32)type),
      m_is_constant(0u),
      m_unused0(0u),
      m_register(register_index),
      m_value({ .u64_value = 0lu })
  {
    return;
  }
};

}
}
