// vim: expandtab:ts=2:sw=2

#pragma once

#include "fox/fox_types.h"

namespace fox {
namespace ir {

using Constant = ::fox::Value;

/*!
 * @brief Basic data types that are attached to each register and that each
 *        opcode operates on.
 */
enum class Type : u8 {
  Integer8,     /**< Signed (two's complement) or unsigned 8-bit integer */
  Integer16,    /**< Signed (two's complement) or unsigned 16-bit integer */
  Integer32,    /**< Signed (two's complement) or unsigned 32-bit integer */
  Integer64,    /**< Signed (two's complement) or unsigned 64-bit integer */
  Float32,      /**< Single precision / 32-bit floating point */
  Float64,      /**< Double precision / 64-bit floating point */
  Bool,         /**< Boolean value (storage format not specified) */
  BranchLabel,  /**< Internal branch label (stored as unsigned 32-bit integer) */
  HostAddress,  /**< Native system address type. */
};

/*!
 * @brief Return a C-string with the name of the given IR data type.
 */
static inline const char *
type_to_name(const Type type)
{
  switch (type) {
    case Type::Integer8:
      return "i8";
    case Type::Integer16:
      return "i16";
    case Type::Integer32:
      return "i32";
    case Type::Integer64:
      return "i64";
    case Type::Float32:
      return "f32";
    case Type::Float64:
      return "f64";
    case Type::Bool:
      return "bool";
    case Type::BranchLabel:
      return "label";
    case Type::HostAddress:
      return "hostptr";
    default:
      return "???";
  }
}

/*!
 * @brief Return whether the given IR data type represents a signed or
 *        unsigned integer.
 */
static inline bool
is_integer_type(const Type type)
{
  switch (type) {
    case Type::Integer8:
      return true;
    case Type::Integer16:
      return true;
    case Type::Integer32:
      return true;
    case Type::Integer64:
      return true;
    default:
      return false;
  }
}

/*!
 * @brief Return whether the given IR data type represents a floating point
 *        value.
 */
static inline bool
is_float_type(const Type type)
{
  switch (type) {
    case Type::Float32:
      return true;
    case Type::Float64:
      return true;
    default:
      return false;
  }
}

/*!
 * @brief Return whether the given IR data type represents a numerical value.
 */
static inline bool
is_numeric_type(const Type type)
{
  switch (type) {
    case Type::Integer8:
      return true;
    case Type::Integer16:
      return true;
    case Type::Integer32:
      return true;
    case Type::Integer64:
      return true;
    case Type::Float32:
      return true;
    case Type::Float64:
      return true;
    default:
      return false;
  }
}

/*!
 * @brief Return the size in bytes of a type's representation in memory.
 */
static inline u32
type_to_size(const Type type)
{
  switch (type) {
    case Type::Integer8:
      return 1;
    case Type::Integer16:
      return 2;
    case Type::Integer32:
      return 4;
    case Type::Integer64:
      return 8;
    case Type::Float32:
      return 4;
    case Type::Float64:
      return 8;
    default:
      assert(false && "unhandled type");
  }
  return 0;
}

}
}
