#include <cmath>
#include <stdexcept>

#include "fox/fox_utils.h"
#include "ir/ir_calculator.h"

namespace fox {
namespace ir {

Calculator::Calculator()
{
  return;
}

/*
 * Bit operations - integer targets only.
 */

Operand
Calculator::rotr(const Operand value, const Operand count)
{
  const u32 rot_bits = count.zero_extended();
  switch (value.type()) {
    case Type::Integer8: {
      const u8 result = rotate_right<u8>(value.value().u8_value, rot_bits);
      return Operand(value.type(), Constant { .u8_value = result });
      break;
    }
    case Type::Integer16: {
      const u16 result = rotate_right<u16>(value.value().u16_value, rot_bits);
      return Operand(value.type(), Constant { .u16_value = result });
      break;
    }
    case Type::Integer32: {
      const u32 result = rotate_right<u32>(value.value().u32_value, rot_bits);
      return Operand(value.type(), Constant { .u32_value = result });
      break;
    }
    case Type::Integer64: {
      const u64 result = rotate_right<u64>(value.value().u64_value, rot_bits);
      return Operand(value.type(), Constant { .u64_value = result });
      break;
    }
    default:
      assert(false);
  }
  throw std::runtime_error("unhandled error");
}

Operand
Calculator::rotl(const Operand value, const Operand count)
{
  const u32 rot_bits = count.zero_extended();
  switch (value.type()) {
    case Type::Integer8: {
      const u8 result = rotate_left<u8>(value.value().u8_value, rot_bits);
      return Operand(value.type(), Constant { .u8_value = result });
      break;
    }
    case Type::Integer16: {
      const u16 result = rotate_left<u16>(value.value().u16_value, rot_bits);
      return Operand(value.type(), Constant { .u16_value = result });
      break;
    }
    case Type::Integer32: {
      const u32 result = rotate_left<u32>(value.value().u32_value, rot_bits);
      return Operand(value.type(), Constant { .u32_value = result });
      break;
    }
    case Type::Integer64: {
      const u64 result = rotate_left<u64>(value.value().u64_value, rot_bits);
      return Operand(value.type(), Constant { .u64_value = result });
      break;
    }
    default:
      assert(false);
  }
  throw std::runtime_error("unhandled error");
}

Operand
Calculator::shiftr(const Operand value, const Operand count)
{
  const u32 shift_bits = count.zero_extended();
  switch (value.type()) {
    case Type::Integer8: {
      const u8 result = value.value().u8_value >> shift_bits;
      return Operand(value.type(), Constant { .u8_value = result });
      break;
    }
    case Type::Integer16: {
      const u16 result = value.value().u16_value >> shift_bits;
      return Operand(value.type(), Constant { .u16_value = result });
      break;
    }
    case Type::Integer32: {
      const u32 result = value.value().u32_value >> shift_bits;
      return Operand(value.type(), Constant { .u32_value = result });
      break;
    }
    case Type::Integer64: {
      const u64 result = value.value().u64_value >> shift_bits;
      return Operand(value.type(), Constant { .u64_value = result });
      break;
    }
    default:
      assert(false);
  }
  throw std::runtime_error("unhandled error");
}

Operand
Calculator::shiftl(const Operand value, const Operand count)
{
  const u32 shift_bits = count.zero_extended();
  switch (value.type()) {
    case Type::Integer8: {
      const u8 result = value.value().u8_value << shift_bits;
      return Operand(value.type(), Constant { .u8_value = result });
      break;
    }
    case Type::Integer16: {
      const u16 result = value.value().u16_value << shift_bits;
      return Operand(value.type(), Constant { .u16_value = result });
      break;
    }
    case Type::Integer32: {
      const u32 result = value.value().u32_value << shift_bits;
      return Operand(value.type(), Constant { .u32_value = result });
      break;
    }
    case Type::Integer64: {
      const u64 result = value.value().u64_value << shift_bits;
      return Operand(value.type(), Constant { .u64_value = result });
      break;
    }
    default:
      assert(false);
  }
  throw std::runtime_error("unhandled error");
}

Operand
Calculator::ashiftr(const Operand value, const Operand count)
{
  const u32 shift_bits = count.zero_extended();
  switch (value.type()) {
    case Type::Integer8: {
      const i8 result = value.value().i8_value << shift_bits;
      return Operand(value.type(), Constant { .i8_value = result });
      break;
    }
    case Type::Integer16: {
      const i16 result = value.value().i16_value << shift_bits;
      return Operand(value.type(), Constant { .i16_value = result });
      break;
    }
    case Type::Integer32: {
      const i32 result = value.value().u32_value << shift_bits;
      return Operand(value.type(), Constant { .i32_value = result });
      break;
    }
    case Type::Integer64: {
      const i64 result = value.value().i64_value << shift_bits;
      return Operand(value.type(), Constant { .i64_value = result });
      break;
    }
    default:
      assert(false);
  }
  throw std::runtime_error("unhandled error");
}

Operand
Calculator::_and(const Operand source_a, const Operand source_b)
{
  const Type type = source_a.type();
  switch (type) {
    case Type::Integer8: {
      const u8 result = source_a.value().u8_value & source_b.value().u8_value;
      return Operand(type, Constant { .u8_value = result });
      break;
    }
    case Type::Integer16: {
      const u16 result = source_a.value().u16_value & source_b.value().u16_value;
      return Operand(type, Constant { .u16_value = result });
      break;
    }
    case Type::Integer32: {
      const u32 result = source_a.value().u32_value & source_b.value().u32_value;
      return Operand(type, Constant { .u32_value = result });
      break;
    }
    case Type::Integer64: {
      const u64 result = source_a.value().u64_value & source_b.value().u64_value;
      return Operand(type, Constant { .u64_value = result });
      break;
    }
    default:
      assert(false);
  }
  throw std::runtime_error("unhandled error");
}

Operand
Calculator::_or(const Operand source_a, const Operand source_b)
{
  const Type type = source_a.type();
  switch (type) {
    case Type::Integer8: {
      const u8 result = source_a.value().u8_value | source_b.value().u8_value;
      return Operand(type, Constant { .u8_value = result });
      break;
    }
    case Type::Integer16: {
      const u16 result = source_a.value().u16_value | source_b.value().u16_value;
      return Operand(type, Constant { .u16_value = result });
      break;
    }
    case Type::Integer32: {
      const u32 result = source_a.value().u32_value | source_b.value().u32_value;
      return Operand(type, Constant { .u32_value = result });
      break;
    }
    case Type::Integer64: {
      const u64 result = source_a.value().u64_value | source_b.value().u64_value;
      return Operand(type, Constant { .u64_value = result });
      break;
    }
    default:
      assert(false);
  }
  throw std::runtime_error("unhandled error");
}

Operand
Calculator::_xor(const Operand source_a, const Operand source_b)
{
  const Type type = source_a.type();
  switch (type) {
    case Type::Integer8: {
      const u8 result = source_a.value().u8_value ^ source_b.value().u8_value;
      return Operand(type, Constant { .u8_value = result });
      break;
    }
    case Type::Integer16: {
      const u16 result = source_a.value().u16_value ^ source_b.value().u16_value;
      return Operand(type, Constant { .u16_value = result });
      break;
    }
    case Type::Integer32: {
      const u32 result = source_a.value().u32_value ^ source_b.value().u32_value;
      return Operand(type, Constant { .u32_value = result });
      break;
    }
    case Type::Integer64: {
      const u64 result = source_a.value().u64_value ^ source_b.value().u64_value;
      return Operand(type, Constant { .u64_value = result });
      break;
    }
    default:
      assert(false);
  }
  throw std::runtime_error("unhandled error");
}

Operand
Calculator::_not(const Operand source)
{
  switch (source.type()) {
    case Type::Integer8: {
      const u8 result = ~source.value().u8_value;
      return Operand(source.type(), Constant { .u8_value = result });
      break;
    }
    case Type::Integer16: {
      const u16 result = ~source.value().u16_value;
      return Operand(source.type(), Constant { .u16_value = result });
      break;
    }
    case Type::Integer32: {
      const u32 result = ~source.value().u32_value;
      return Operand(source.type(), Constant { .u32_value = result });
      break;
    }
    case Type::Integer64: {
      const u64 result = ~source.value().u64_value;
      return Operand(source.type(), Constant { .u64_value = result });
      break;
    }
    default:
      assert(false);
  }
  throw std::runtime_error("unhandled error");
}

Operand
Calculator::bsc(const Operand value, const Operand control, Operand position)
{
  const u64 position_bit = u64(1) << position.zero_extended();
  const bool is_set = control.value().bool_value;

  switch (value.type()) {
    case Type::Integer8: {
      u8 input = value.value().u8_value;
      const u8 result = is_set ? (input | position_bit) : (input & ~position_bit);
      return Operand(value.type(), Constant { .u8_value = result });
      break;
    }
    case Type::Integer16: {
      u16 input = value.value().u16_value;
      const u16 result = is_set ? (input | position_bit) : (input & ~position_bit);
      return Operand(value.type(), Constant { .u16_value = result });
      break;
    }
    case Type::Integer32: {
      u32 input = value.value().u32_value;
      const u32 result = is_set ? (input | position_bit) : (input & ~position_bit);
      return Operand(value.type(), Constant { .u32_value = result });
      break;
    }
    case Type::Integer64: {
      u64 input = value.value().u64_value;
      const u64 result = is_set ? (input | position_bit) : (input & ~position_bit);
      return Operand(value.type(), Constant { .u64_value = result });
      break;
    }
    default:
      assert(false);
  }
  throw std::runtime_error("unhandled error");
}

/*
 * Arithmetic operations
 */

Operand
Calculator::add(const Operand source_a, const Operand source_b)
{
  switch (source_a.type()) {
    case Type::Integer8: {
      const u8 result = source_a.value().u8_value + source_b.value().u8_value;
      return Operand(source_a.type(), Constant { .u8_value = result });
      break;
    }
    case Type::Integer16: {
      const u16 result = source_a.value().u16_value + source_b.value().u16_value;
      return Operand(source_a.type(), Constant { .u16_value = result });
      break;
    }
    case Type::Integer32: {
      const u32 result = source_a.value().u32_value + source_b.value().u32_value;
      return Operand(source_a.type(), Constant { .u32_value = result });
      break;
    }
    case Type::Integer64: {
      const u64 result = source_a.value().u64_value + source_b.value().u64_value;
      return Operand(source_a.type(), Constant { .u64_value = result });
      break;
    }
    case Type::Float32: {
      const f32 result = source_a.value().f32_value + source_b.value().f32_value;
      return Operand(source_a.type(), Constant { .f32_value = result });
      break;
    }
    case Type::Float64: {
      const f64 result = source_a.value().f64_value + source_b.value().f64_value;
      return Operand(source_a.type(), Constant { .f64_value = result });
      break;
    }
    default:
      assert(false);
  }
  throw std::runtime_error("unhandled error");
}

Operand
Calculator::sub(const Operand source_a, const Operand source_b)
{
  switch (source_a.type()) {
    case Type::Integer8: {
      const u8 result = source_a.value().u8_value - source_b.value().u8_value;
      return Operand(source_a.type(), Constant { .u8_value = result });
      break;
    }
    case Type::Integer16: {
      const u16 result = source_a.value().u16_value - source_b.value().u16_value;
      return Operand(source_a.type(), Constant { .u16_value = result });
      break;
    }
    case Type::Integer32: {
      const u32 result = source_a.value().u32_value - source_b.value().u32_value;
      return Operand(source_a.type(), Constant { .u32_value = result });
      break;
    }
    case Type::Integer64: {
      const u64 result = source_a.value().u64_value - source_b.value().u64_value;
      return Operand(source_a.type(), Constant { .u64_value = result });
      break;
    }
    case Type::Float32: {
      const f32 result = source_a.value().f32_value - source_b.value().f32_value;
      return Operand(source_a.type(), Constant { .f32_value = result });
      break;
    }
    case Type::Float64: {
      const f64 result = source_a.value().f64_value - source_b.value().f64_value;
      return Operand(source_a.type(), Constant { .f64_value = result });
      break;
    }
    default:
      assert(false);
  }
  throw std::runtime_error("unhandled error");
}

Operand
Calculator::mul(const Operand source_a, const Operand source_b)
{
  switch (source_a.type()) {
    case Type::Integer8: {
      const i8 result = source_a.value().i8_value * source_b.value().i8_value;
      return Operand(source_a.type(), Constant { .i8_value = result });
      break;
    }
    case Type::Integer16: {
      const i16 result = source_a.value().i16_value * source_b.value().i16_value;
      return Operand(source_a.type(), Constant { .i16_value = result });
      break;
    }
    case Type::Integer32: {
      const i32 result = source_a.value().i32_value * source_b.value().i32_value;
      return Operand(source_a.type(), Constant { .i32_value = result });
      break;
    }
    case Type::Integer64: {
      const i64 result = source_a.value().i64_value * source_b.value().i64_value;
      return Operand(source_a.type(), Constant { .i64_value = result });
      break;
    }
    case Type::Float32: {
      const f32 result = source_a.value().f32_value * source_b.value().f32_value;
      return Operand(source_a.type(), Constant { .f32_value = result });
      break;
    }
    case Type::Float64: {
      const f64 result = source_a.value().f64_value * source_b.value().f64_value;
      return Operand(source_a.type(), Constant { .f64_value = result });
      break;
    }
    default:
      assert(false);
  }
  throw std::runtime_error("unhandled error");
}

Operand
Calculator::umul(const Operand source_a, const Operand source_b)
{
  switch (source_a.type()) {
    case Type::Integer8: {
      const u8 result = source_a.value().u8_value * source_b.value().u8_value;
      return Operand(source_a.type(), Constant { .u8_value = result });
      break;
    }
    case Type::Integer16: {
      const u16 result = source_a.value().u16_value * source_b.value().u16_value;
      return Operand(source_a.type(), Constant { .u16_value = result });
      break;
    }
    case Type::Integer32: {
      const u32 result = source_a.value().u32_value * source_b.value().u32_value;
      return Operand(source_a.type(), Constant { .u32_value = result });
      break;
    }
    case Type::Integer64: {
      const u64 result = source_a.value().u64_value * source_b.value().u64_value;
      return Operand(source_a.type(), Constant { .u64_value = result });
      break;
    }
    default:
      assert(false);
  }
  throw std::runtime_error("unhandled error");
}

Operand
Calculator::div(const Operand source_a, const Operand source_b)
{
  switch (source_a.type()) {
    case Type::Integer8: {
      const i8 result = source_a.value().i8_value / source_b.value().i8_value;
      return Operand(source_a.type(), Constant { .i8_value = result });
      break;
    }
    case Type::Integer16: {
      const i16 result = source_a.value().i16_value / source_b.value().i16_value;
      return Operand(source_a.type(), Constant { .i16_value = result });
      break;
    }
    case Type::Integer32: {
      const i32 result = source_a.value().i32_value / source_b.value().i32_value;
      return Operand(source_a.type(), Constant { .i32_value = result });
      break;
    }
    case Type::Integer64: {
      const i64 result = source_a.value().i64_value / source_b.value().i64_value;
      return Operand(source_a.type(), Constant { .i64_value = result });
      break;
    }
    case Type::Float32: {
      const f32 result = source_a.value().f32_value / source_b.value().f32_value;
      return Operand(source_a.type(), Constant { .f32_value = result });
      break;
    }
    case Type::Float64: {
      const f64 result = source_a.value().f64_value / source_b.value().f64_value;
      return Operand(source_a.type(), Constant { .f64_value = result });
      break;
    }
    default:
      assert(false);
  }
  throw std::runtime_error("unhandled error");
}

Operand
Calculator::udiv(const Operand source_a, const Operand source_b)
{
  switch (source_a.type()) {
    case Type::Integer8: {
      const u8 result = source_a.value().u8_value / source_b.value().u8_value;
      return Operand(source_a.type(), Constant { .u8_value = result });
      break;
    }
    case Type::Integer16: {
      const u16 result = source_a.value().u16_value / source_b.value().u16_value;
      return Operand(source_a.type(), Constant { .u16_value = result });
      break;
    }
    case Type::Integer32: {
      const u32 result = source_a.value().u32_value / source_b.value().u32_value;
      return Operand(source_a.type(), Constant { .u32_value = result });
      break;
    }
    case Type::Integer64: {
      const u64 result = source_a.value().u64_value / source_b.value().u64_value;
      return Operand(source_a.type(), Constant { .u64_value = result });
      break;
    }
    default:
      assert(false);
  }
  throw std::runtime_error("unhandled error");
}

Operand
Calculator::mod(const Operand source_a, const Operand source_b)
{
  switch (source_a.type()) {
    case Type::Integer8: {
      const i8 result = source_a.value().i8_value % source_b.value().i8_value;
      return Operand(source_a.type(), Constant { .i8_value = result });
      break;
    }
    case Type::Integer16: {
      const i16 result = source_a.value().i16_value % source_b.value().i16_value;
      return Operand(source_a.type(), Constant { .i16_value = result });
      break;
    }
    case Type::Integer32: {
      const i32 result = source_a.value().i32_value % source_b.value().i32_value;
      return Operand(source_a.type(), Constant { .i32_value = result });
      break;
    }
    case Type::Integer64: {
      const i64 result = source_a.value().i64_value % source_b.value().i64_value;
      return Operand(source_a.type(), Constant { .i64_value = result });
      break;
    }
    case Type::Float32: {
      assert(false && "Let's be careful to define what signed float mod means");
      // const f32 result = fmodf(source_a.value().f32_value, source_b.value().f32_value);
      // return Operand(source_a.type(), Constant { .f32_value = result });
      break;
    }
    case Type::Float64: {
      assert(false && "Let's be careful to define what signed float mod means");
      // const f64 result = fmodf(source_a.value().f64_value, source_b.value().f64_value);
      // return Operand(source_a.type(), Constant { .f64_value = result });
      break;
    }
    default:
      assert(false);
  }
  throw std::runtime_error("unhandled error");
}

Operand
Calculator::sqrt(const Operand source)
{
  switch (source.type()) {
    case Type::Float32: {
      const f32 result = ::sqrtf(source.value().f32_value);
      return Operand(source.type(), Constant { .f32_value = result });
      break;
    }
    case Type::Float64: {
      const f64 result = ::sqrtl(source.value().f64_value);
      return Operand(source.type(), Constant { .f64_value = result });
      break;
    }
    default:
      assert(false);
  }
  throw std::runtime_error("unhandled error");
}

/*
 * Conversion operations.
 */

Operand
Calculator::extend16(const Operand source)
{
  switch (source.type()) {
    case Type::Integer8: {
      const i16 result = source.value().i8_value;
      return Operand(Type::Integer16, Constant { .i16_value = result });
      break;
    }
    default:
      assert(false);
  }
  throw std::runtime_error("unhandled error");
}

Operand
Calculator::extend32(const Operand source)
{
  switch (source.type()) {
    case Type::Integer8: {
      const i32 result = source.value().i8_value;
      return Operand(Type::Integer32, Constant { .i32_value = result });
      break;
    }
    case Type::Integer16: {
      const i32 result = source.value().i16_value;
      return Operand(Type::Integer32, Constant { .i32_value = result });
      break;
    }
    default:
      assert(false);
  }
  throw std::runtime_error("unhandled error");
}

Operand
Calculator::extend64(const Operand source)
{
  switch (source.type()) {
    case Type::Integer8: {
      const i64 result = source.value().i8_value;
      return Operand(Type::Integer64, Constant { .i64_value = result });
      break;
    }
    case Type::Integer16: {
      const i64 result = source.value().i16_value;
      return Operand(Type::Integer64, Constant { .i64_value = result });
      break;
    }
    case Type::Integer32: {
      const i64 result = source.value().i32_value;
      return Operand(Type::Integer64, Constant { .i64_value = result });
      break;
    }
    default:
      assert(false);
  }
  throw std::runtime_error("unhandled error");
}

Operand
Calculator::bitcast(Type out_type, const Operand source)
{
  if (out_type == source.type()) {
    return source;
  }

  /* Note: zero-initializing result here is important for the case where we
   *       bitcast a smaller type to a larger one so that the remaining bits
   *       are zero. */
  const Constant constant = source.value();
  switch (out_type) {
    case Type::Integer8: {
      i8 result = 0;
      if (is_integer_type(source.type())) {
        result = source.zero_extended() & 0xFF;
        return Operand(out_type, Constant { .i8_value = result });
      } else if (source.type() == Type::Float32) {
        memcpy(&result, &constant.f32_value, sizeof(result));
        return Operand(out_type, Constant { .i8_value = result });
      } else if (source.type() == Type::Float64) {
        memcpy(&result, &constant.f64_value, sizeof(result));
        return Operand(out_type, Constant { .i8_value = result });
      } else {
        assert(false);
      }
      break;
    }
    case Type::Integer16: {
      i16 result = 0;
      if (is_integer_type(source.type())) {
        result = source.zero_extended() & 0xFFFF;
        return Operand(out_type, Constant { .i16_value = result });
      } else if (source.type() == Type::Float32) {
        memcpy(&result, &constant.f32_value, sizeof(result));
        return Operand(out_type, Constant { .i16_value = result });
      } else if (source.type() == Type::Float64) {
        memcpy(&result, &constant.f64_value, sizeof(result));
        return Operand(out_type, Constant { .i16_value = result });
      } else {
        assert(false);
      }
      break;
    }
    case Type::Integer32: {
      i32 result = 0;
      if (is_integer_type(source.type())) {
        result = source.zero_extended() & 0xFFFFFFFF;
        return Operand(out_type, Constant { .i32_value = result });
      } else if (source.type() == Type::Float32) {
        memcpy(&result, &constant.f32_value, sizeof(result));
        return Operand(out_type, Constant { .i32_value = result });
      } else if (source.type() == Type::Float64) {
        memcpy(&result, &constant.f64_value, sizeof(result));
        return Operand(out_type, Constant { .i32_value = result });
      } else {
        assert(false);
      }
      break;
    }
    case Type::Integer64: {
      i64 result = 0;
      if (is_integer_type(source.type())) {
        result = source.zero_extended();
        return Operand(out_type, Constant { .i64_value = result });
      } else if (source.type() == Type::Float32) {
        memcpy(&result, &constant.f32_value, sizeof(result));
        return Operand(out_type, Constant { .i64_value = result });
      } else if (source.type() == Type::Float64) {
        memcpy(&result, &constant.f64_value, sizeof(result));
        return Operand(out_type, Constant { .i64_value = result });
      } else {
        assert(false);
      }
      break;
    }
    case Type::Float32: {
      f32 result = 0;
      if (is_integer_type(source.type())) {
        u64 intermediate = source.zero_extended();
        memcpy(&result, &intermediate, sizeof(result));
        return Operand(out_type, Constant { .f32_value = result });
      } else if (source.type() == Type::Float64) {
        memcpy(&result, &constant.f64_value, sizeof(result));
        return Operand(out_type, Constant { .f32_value = result });
      } else {
        assert(false);
      }
      break;
    }
    case Type::Float64: {
      f64 result = 0;
      if (is_integer_type(source.type())) {
        u64 intermediate = source.zero_extended();
        memcpy(&result, &intermediate, sizeof(result));
        return Operand(out_type, Constant { .f64_value = result });
      } else if (source.type() == Type::Float32) {
        memcpy(&result, &constant.f32_value, sizeof(result));
        return Operand(out_type, Constant { .f64_value = result });
      } else {
        assert(false);
      }
      break;
    }
    default:
      assert(false);
  }
  throw std::runtime_error("unhandled error");
}

Operand
Calculator::castf2i(Type out_type, const Operand source)
{
  switch (out_type) {
    case Type::Integer8: {
      if (source.type() == Type::Float32) {
        return Operand(out_type, Constant { .i8_value = (i8)source.value().f32_value });
      } else {
        return Operand(out_type, Constant { .i8_value = (i8)source.value().f64_value });
      }
      break;
    }
    case Type::Integer16: {
      if (source.type() == Type::Float32) {
        return Operand(out_type, Constant { .i16_value = (i16)source.value().f32_value });
      } else {
        return Operand(out_type, Constant { .i16_value = (i16)source.value().f64_value });
      }
    }
    case Type::Integer32: {
      if (source.type() == Type::Float32) {
        return Operand(out_type, Constant { .i32_value = (i32)source.value().f32_value });
      } else {
        return Operand(out_type, Constant { .i32_value = (i32)source.value().f64_value });
      }
    }
    case Type::Integer64: {
      if (source.type() == Type::Float32) {
        return Operand(out_type, Constant { .i64_value = (i64)source.value().f32_value });
      } else {
        return Operand(out_type, Constant { .i64_value = (i64)source.value().f64_value });
      }
    }
    default:
      assert(false);
  }
  throw std::runtime_error("unhandled error");
}

Operand
Calculator::casti2f(Type out_type, const Operand source)
{
  switch (out_type) {
    case Type::Float32: {
      return Operand(out_type, Constant { .f32_value = (f32)source.sign_extended() });
    }
    case Type::Float64: {
      return Operand(out_type, Constant { .f64_value = (f64)source.sign_extended() });
    }
    default:
      assert(false);
  }
  throw std::runtime_error("unhandled error");
}

Operand
Calculator::resizef(Type out_type, const Operand source)
{
  switch (out_type) {
    case Type::Float32: {
      return Operand(out_type, Constant { .f32_value = (f32)source.value().f64_value });
    }
    case Type::Float64: {
      return Operand(out_type, Constant { .f64_value = (f64)source.value().f32_value });
    }
    default:
      assert(false);
  }
  throw std::runtime_error("unhandled error");
}

/*
 * Comparison operations.
 */

Operand
Calculator::test(const Operand source_a, const Operand source_b)
{
  switch (source_a.type()) {
    case Type::Integer8: {
      const bool result = source_a.value().u8_value & source_b.value().u8_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Integer16: {
      const bool result = source_a.value().u16_value & source_b.value().u16_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Integer32: {
      const bool result = source_a.value().u32_value & source_b.value().u32_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Integer64: {
      const bool result = source_a.value().u64_value & source_b.value().u64_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    default:
      assert(false);
  }
  throw std::runtime_error("unhandled error");
}

Operand
Calculator::cmp_eq(const Operand source_a, const Operand source_b)
{
  switch (source_a.type()) {
    case Type::Integer8: {
      const bool result = source_a.value().i8_value == source_b.value().i8_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Integer16: {
      const bool result = source_a.value().i16_value == source_b.value().i16_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Integer32: {
      const bool result = source_a.value().i32_value == source_b.value().i32_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Integer64: {
      const bool result = source_a.value().i64_value == source_b.value().i64_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Float32: {
      const bool result = source_a.value().f32_value == source_b.value().f32_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Float64: {
      const bool result = source_a.value().f64_value == source_b.value().f64_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    default:
      assert(false);
  }
  throw std::runtime_error("unhandled error");
}

Operand
Calculator::cmp_lt(const Operand source_a, const Operand source_b)
{
  switch (source_a.type()) {
    case Type::Integer8: {
      const bool result = source_a.value().i8_value < source_b.value().i8_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Integer16: {
      const bool result = source_a.value().i16_value < source_b.value().i16_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Integer32: {
      const bool result = source_a.value().i32_value < source_b.value().i32_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Integer64: {
      const bool result = source_a.value().i64_value < source_b.value().i64_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Float32: {
      const bool result = source_a.value().f32_value < source_b.value().f32_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Float64: {
      const bool result = source_a.value().f64_value < source_b.value().f64_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    default:
      assert(false);
  }
  throw std::runtime_error("unhandled error");
}

Operand
Calculator::cmp_lte(const Operand source_a, const Operand source_b)
{
  switch (source_a.type()) {
    case Type::Integer8: {
      const bool result = source_a.value().i8_value <= source_b.value().i8_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Integer16: {
      const bool result = source_a.value().i16_value <= source_b.value().i16_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Integer32: {
      const bool result = source_a.value().i32_value <= source_b.value().i32_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Integer64: {
      const bool result = source_a.value().i64_value <= source_b.value().i64_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Float32: {
      const bool result = source_a.value().f32_value <= source_b.value().f32_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Float64: {
      const bool result = source_a.value().f64_value <= source_b.value().f64_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    default:
      assert(false);
  }
  throw std::runtime_error("unhandled error");
}

Operand
Calculator::cmp_gt(const Operand source_a, const Operand source_b)
{
  switch (source_a.type()) {
    case Type::Integer8: {
      const bool result = source_a.value().i8_value > source_b.value().i8_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Integer16: {
      const bool result = source_a.value().i16_value > source_b.value().i16_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Integer32: {
      const bool result = source_a.value().i32_value > source_b.value().i32_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Integer64: {
      const bool result = source_a.value().i64_value > source_b.value().i64_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Float32: {
      const bool result = source_a.value().f32_value > source_b.value().f32_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Float64: {
      const bool result = source_a.value().f64_value > source_b.value().f64_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    default:
      assert(false);
  }
  throw std::runtime_error("unhandled error");
}

Operand
Calculator::cmp_gte(const Operand source_a, const Operand source_b)
{
  switch (source_a.type()) {
    case Type::Integer8: {
      const bool result = source_a.value().i8_value >= source_b.value().i8_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Integer16: {
      const bool result = source_a.value().i16_value >= source_b.value().i16_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Integer32: {
      const bool result = source_a.value().i32_value >= source_b.value().i32_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Integer64: {
      const bool result = source_a.value().i64_value >= source_b.value().i64_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Float32: {
      const bool result = source_a.value().f32_value >= source_b.value().f32_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Float64: {
      const bool result = source_a.value().f64_value >= source_b.value().f64_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    default:
      assert(false);
  }
  throw std::runtime_error("unhandled error");
}

Operand
Calculator::cmp_ult(const Operand source_a, const Operand source_b)
{
  switch (source_a.type()) {
    case Type::Integer8: {
      const bool result = source_a.value().u8_value < source_b.value().u8_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Integer16: {
      const bool result = source_a.value().u16_value < source_b.value().u16_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Integer32: {
      const bool result = source_a.value().u32_value < source_b.value().u32_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Integer64: {
      const bool result = source_a.value().u64_value < source_b.value().u64_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    default:
      assert(false);
  }
  throw std::runtime_error("unhandled error"); 
}

Operand
Calculator::cmp_ulte(const Operand source_a, const Operand source_b)
{
  switch (source_a.type()) {
    case Type::Integer8: {
      const bool result = source_a.value().u8_value <= source_b.value().u8_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Integer16: {
      const bool result = source_a.value().u16_value <= source_b.value().u16_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Integer32: {
      const bool result = source_a.value().u32_value <= source_b.value().u32_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Integer64: {
      const bool result = source_a.value().u64_value <= source_b.value().u64_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    default:
      assert(false);
      throw std::runtime_error("unhandled error");
  }
}

Operand
Calculator::cmp_ugt(const Operand source_a, const Operand source_b)
{
  switch (source_a.type()) {
    case Type::Integer8: {
      const bool result = source_a.value().u8_value > source_b.value().u8_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Integer16: {
      const bool result = source_a.value().u16_value > source_b.value().u16_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Integer32: {
      const bool result = source_a.value().u32_value > source_b.value().u32_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Integer64: {
      const bool result = source_a.value().u64_value > source_b.value().u64_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    default:
      assert(false);
      throw std::runtime_error("unhandled error");
  }
}

Operand
Calculator::cmp_ugte(const Operand source_a, const Operand source_b)
{
  switch (source_a.type()) {
    case Type::Integer8: {
      const bool result = source_a.value().u8_value >= source_b.value().u8_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Integer16: {
      const bool result = source_a.value().u16_value >= source_b.value().u16_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Integer32: {
      const bool result = source_a.value().u32_value >= source_b.value().u32_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    case Type::Integer64: {
      const bool result = source_a.value().u64_value >= source_b.value().u64_value;
      return Operand(Type::Bool, Constant { .bool_value = result });
    }
    default:
      assert(false);
      throw std::runtime_error("unhandled error");
  }
}

/*
 * Control flow operations.
 */

Operand
Calculator::select(const Operand decision, const Operand if_false, const Operand if_true)
{
  return decision.value().bool_value ? if_true : if_false;
}

}
}
