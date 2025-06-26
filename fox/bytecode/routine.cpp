// vim: expandtab:ts=2:sw=2

#include <cstring>
#include <cmath>

#include <fmt/core.h>

#include "fox/fox_utils.h"
#include "fox/bytecode/bytecode.h"
#include "fox/bytecode/opcode.h"

namespace fox {
namespace bytecode {

#define INSTRUCTION(type)                                                                \
  const type *const data = reinterpret_cast<const type *>(&m_storage[offset]);           \
  offset += sizeof(type);

uint64_t
Routine::execute(Guest *const guest, void *memory_base, void *register_base)
{
  ir::Constant spill[32];
  ir::Constant registers[16];
  size_t offset = 0;
  while (true) {
    assert(offset < m_bytes);
    switch (Opcodes(m_storage[offset])) {
      case Opcodes::Constant8: {
        INSTRUCTION(Instruction16R1C0);
        registers[data->rA].u64_value = *(u8 *)&m_storage[offset];
        offset += 1;
        break;
      }

      case Opcodes::Constant16: {
        INSTRUCTION(Instruction16R1C0);
        registers[data->rA].u64_value = *(u16 *)&m_storage[offset];
        offset += 2;
        break;
      }

      case Opcodes::Constant32: {
        INSTRUCTION(Instruction16R1C0);
        u32 imm;
        memcpy(&imm, &m_storage[offset], sizeof(u32));
        registers[data->rA].u64_value = imm;
        offset += 4;
        break;
      }

      case Opcodes::Constant64: {
        INSTRUCTION(Instruction16R1C0);
        u64 imm;
        memcpy(&imm, &m_storage[offset], sizeof(u64));
        registers[data->rA].u64_value = imm;
        offset += 8;
        break;
      }

      case Opcodes::ExtendConstant8: {
        INSTRUCTION(Instruction16R1C0);
        registers[data->rA].i64_value = *(i8 *)&m_storage[offset];
        offset += 1;
        break;
      }

      case Opcodes::ExtendConstant16: {
        INSTRUCTION(Instruction16R1C0);
        registers[data->rA].i64_value = *(i16 *)&m_storage[offset];
        offset += 2;
        break;
      }

      case Opcodes::ExtendConstant32: {
        INSTRUCTION(Instruction16R1C0);
        registers[data->rA].i64_value = *(i32 *)&m_storage[offset];
        offset += 4;
        break;
      }

      case Opcodes::ReadRegister8: {
        INSTRUCTION(Instruction32R1C2);
        registers[data->rA] = guest->guest_register_read(data->constant, 1);
        break;
      }

      case Opcodes::ReadRegister16: {
        INSTRUCTION(Instruction32R1C2);
        registers[data->rA] = guest->guest_register_read(data->constant, 2);
        break;
      }

      case Opcodes::ReadRegister32: {
        INSTRUCTION(Instruction32R1C2);
        registers[data->rA] = guest->guest_register_read(data->constant, 4);
        break;
      }

      case Opcodes::ReadRegister64: {
        INSTRUCTION(Instruction32R1C2);
        registers[data->rA] = guest->guest_register_read(data->constant, 8);
        break;
      }

      case Opcodes::WriteRegister8: {
        INSTRUCTION(Instruction32R1C2);
        guest->guest_register_write(data->constant, 1, registers[data->rA]);
        break;
      }

      case Opcodes::WriteRegister16: {
        INSTRUCTION(Instruction32R1C2);
        guest->guest_register_write(data->constant, 2, registers[data->rA]);
        break;
      }

      case Opcodes::WriteRegister32: {
        INSTRUCTION(Instruction32R1C2);
        guest->guest_register_write(data->constant, 4, registers[data->rA]);
        break;
      }

      case Opcodes::WriteRegister64: {
        INSTRUCTION(Instruction32R1C2);
        guest->guest_register_write(data->constant, 8, registers[data->rA]);
        break;
      }

      case Opcodes::Load8: {
        INSTRUCTION(Instruction16R2C0);
        registers[data->rA].u64_value =
          guest->guest_load(registers[data->rB].u32_value, 1).u8_value;
        break;
      }

      case Opcodes::Load16: {
        INSTRUCTION(Instruction16R2C0);
        registers[data->rA].u64_value =
          guest->guest_load(registers[data->rB].u32_value, 2).u16_value;
        break;
      }

      case Opcodes::Load32: {
        INSTRUCTION(Instruction16R2C0);
        registers[data->rA].u64_value =
          guest->guest_load(registers[data->rB].u32_value, 4).u32_value;
        break;
      }

      case Opcodes::Load64: {
        INSTRUCTION(Instruction16R2C0);
        registers[data->rA].u64_value =
          guest->guest_load(registers[data->rB].u32_value, 8).u64_value;
        break;
      }

      case Opcodes::Store8: {
        INSTRUCTION(Instruction16R2C0);
        guest->guest_store(registers[data->rA].u32_value, 1, registers[data->rB]);
        break;
      }

      case Opcodes::Store16: {
        INSTRUCTION(Instruction16R2C0);
        guest->guest_store(registers[data->rA].u32_value, 2, registers[data->rB]);
        break;
      }

      case Opcodes::Store32: {
        INSTRUCTION(Instruction16R2C0);
        guest->guest_store(registers[data->rA].u32_value, 4, registers[data->rB]);
        break;
      }

      case Opcodes::Store64: {
        INSTRUCTION(Instruction16R2C0);
        guest->guest_store(registers[data->rA].u32_value, 8, registers[data->rB]);
        break;
      }

      case Opcodes::RotateRight8: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u8_value =
          rotate_right<u8>(registers[data->rB].u8_value, registers[data->rC].u8_value);
        break;
      }

      case Opcodes::RotateRight16: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u16_value =
          rotate_right<u16>(registers[data->rB].u16_value, registers[data->rC].u8_value);
        break;
      }

      case Opcodes::RotateRight32: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u32_value =
          rotate_right<u32>(registers[data->rB].u32_value, registers[data->rC].u8_value);
        break;
      }

      case Opcodes::RotateRight64: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u64_value =
          rotate_right<u64>(registers[data->rB].u64_value, registers[data->rC].u8_value);
        break;
      }

      case Opcodes::RotateLeft8: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u8_value =
          rotate_left<u8>(registers[data->rB].u8_value, registers[data->rC].u8_value);
        break;
      }

      case Opcodes::RotateLeft16: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u16_value =
          rotate_left<u16>(registers[data->rB].u16_value, registers[data->rC].u8_value);
        break;
      }

      case Opcodes::RotateLeft32: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u32_value =
          rotate_left<u32>(registers[data->rB].u32_value, registers[data->rC].u8_value);
        break;
      }

      case Opcodes::RotateLeft64: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u64_value =
          rotate_left<u64>(registers[data->rB].u64_value, registers[data->rC].u8_value);
        break;
      }

      case Opcodes::ShiftRight8: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u8_value =
          registers[data->rB].u8_value >> (registers[data->rC].u8_value & 7);
        break;
      }

      case Opcodes::ShiftRight16: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u16_value =
          registers[data->rB].u16_value >> (registers[data->rC].u8_value & 15);
        break;
      }

      case Opcodes::ShiftRight32: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u32_value =
          registers[data->rB].u32_value >> (registers[data->rC].u8_value & 31);
        break;
      }

      case Opcodes::ShiftRight64: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u64_value =
          registers[data->rB].u64_value >> (registers[data->rC].u8_value & 63);
        break;
      }

      case Opcodes::ShiftLeft8: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u8_value = registers[data->rB].u8_value
                                       << (registers[data->rC].u8_value & 7);
        break;
      }

      case Opcodes::ShiftLeft16: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u16_value = registers[data->rB].u16_value
                                        << (registers[data->rC].u8_value & 15);
        break;
      }

      case Opcodes::ShiftLeft32: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u32_value = registers[data->rB].u32_value
                                        << (registers[data->rC].u8_value & 31);
        break;
      }

      case Opcodes::ShiftLeft64: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u64_value = registers[data->rB].u64_value
                                        << (registers[data->rC].u8_value & 63);
        break;
      }

      case Opcodes::ArithmeticShiftRight8: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].i8_value =
          registers[data->rB].i8_value >> (registers[data->rC].u8_value & 7);
        break;
      }

      case Opcodes::ArithmeticShiftRight16: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].i16_value =
          registers[data->rB].i16_value >> (registers[data->rC].u8_value & 15);
        break;
      }

      case Opcodes::ArithmeticShiftRight32: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].i32_value =
          registers[data->rB].i32_value >> (registers[data->rC].u8_value & 31);
        break;
      }

      case Opcodes::ArithmeticShiftRight64: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].i64_value =
          registers[data->rB].i64_value >> (registers[data->rC].u8_value & 63);
        break;
      }

      case Opcodes::And8: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u8_value =
          registers[data->rB].u8_value & registers[data->rC].u8_value;
        break;
      }

      case Opcodes::And16: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u16_value =
          registers[data->rB].u16_value & registers[data->rC].u16_value;
        break;
      }

      case Opcodes::And32: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u32_value =
          registers[data->rB].u32_value & registers[data->rC].u32_value;
        break;
      }

      case Opcodes::And64: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u64_value =
          registers[data->rB].u64_value & registers[data->rC].u64_value;
        break;
      }

      case Opcodes::AndBool: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].bool_value =
          registers[data->rB].bool_value && registers[data->rC].bool_value;
        break;
      }

      case Opcodes::Or8: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u8_value =
          registers[data->rB].u8_value | registers[data->rC].u8_value;
        break;
      }

      case Opcodes::Or16: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u16_value =
          registers[data->rB].u16_value | registers[data->rC].u16_value;
        break;
      }

      case Opcodes::Or32: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u32_value =
          registers[data->rB].u32_value | registers[data->rC].u32_value;
        break;
      }

      case Opcodes::Or64: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u64_value =
          registers[data->rB].u64_value | registers[data->rC].u64_value;
        break;
      }

      case Opcodes::OrBool: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].bool_value =
          registers[data->rB].bool_value || registers[data->rC].bool_value;
        break;
      }

      case Opcodes::Xor8: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u8_value =
          registers[data->rB].u8_value ^ registers[data->rC].u8_value;
        break;
      }

      case Opcodes::Xor16: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u16_value =
          registers[data->rB].u16_value ^ registers[data->rC].u16_value;
        break;
      }

      case Opcodes::Xor32: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u32_value =
          registers[data->rB].u32_value ^ registers[data->rC].u32_value;
        break;
      }

      case Opcodes::Xor64: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u64_value =
          registers[data->rB].u64_value ^ registers[data->rC].u64_value;
        break;
      }

      case Opcodes::Not8: {
        INSTRUCTION(Instruction16R2C0);
        registers[data->rA].u8_value = ~registers[data->rB].u8_value;
        break;
      }

      case Opcodes::Not16: {
        INSTRUCTION(Instruction16R2C0);
        registers[data->rA].u16_value = ~registers[data->rB].u16_value;
        break;
      }

      case Opcodes::Not32: {
        INSTRUCTION(Instruction16R2C0);
        registers[data->rA].u32_value = ~registers[data->rB].u32_value;
        break;
      }

      case Opcodes::Not64: {
        INSTRUCTION(Instruction16R2C0);
        registers[data->rA].u64_value = ~registers[data->rB].u64_value;
        break;
      }

      case Opcodes::NotBool: {
        INSTRUCTION(Instruction16R2C0);
        registers[data->rA].bool_value = !registers[data->rB].bool_value;
        break;
      }

      case Opcodes::BitSetClear8: {
        INSTRUCTION(Instruction32R3C1);
        registers[data->rA].u8_value =
          (registers[data->rB].u8_value & ~(1u << data->constant)) |
          ((registers[data->rC].bool_value ? 1u : 0u) << data->constant);
        break;
      }

      case Opcodes::BitSetClear16: {
        INSTRUCTION(Instruction32R3C1);
        registers[data->rA].u16_value =
          (registers[data->rB].u16_value & ~(1u << data->constant)) |
          ((registers[data->rC].bool_value ? 1u : 0u) << data->constant);
        break;
      }

      case Opcodes::BitSetClear32: {
        INSTRUCTION(Instruction32R3C1);
        registers[data->rA].u32_value =
          (registers[data->rB].u32_value & ~(1u << data->constant)) |
          ((registers[data->rC].bool_value ? 1u : 0u) << data->constant);
        break;
      }

      case Opcodes::BitSetClear64: {
        INSTRUCTION(Instruction32R3C1);
        registers[data->rA].u64_value =
          (registers[data->rB].u64_value & ~(1lu << data->constant)) |
          ((registers[data->rC].bool_value ? 1lu : 0lu) << data->constant);
        break;
      }

      case Opcodes::AddInteger: {
        /* For addition of smaller register sizes, the upper bits can simply
         * be ignored in the result. */
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u64_value =
          registers[data->rB].u64_value + registers[data->rC].u64_value;
        break;
      }

      case Opcodes::AddFloat32: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].f32_value =
          registers[data->rB].f32_value + registers[data->rC].f32_value;
        break;
      }

      case Opcodes::AddFloat64: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].f64_value =
          registers[data->rB].f64_value + registers[data->rC].f64_value;
        break;
      }

      case Opcodes::SubInteger8: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u8_value =
          registers[data->rB].u8_value - registers[data->rC].u8_value;
        break;
      }

      case Opcodes::SubInteger16: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u16_value =
          registers[data->rB].u16_value - registers[data->rC].u16_value;
        break;
      }

      case Opcodes::SubInteger32: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u32_value =
          registers[data->rB].u32_value - registers[data->rC].u32_value;
        break;
      }

      case Opcodes::SubInteger64: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u64_value =
          registers[data->rB].u64_value - registers[data->rC].u64_value;
        break;
      }

      case Opcodes::SubFloat32: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].f32_value =
          registers[data->rB].f32_value - registers[data->rC].f32_value;
        break;
      }

      case Opcodes::SubFloat64: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].f64_value =
          registers[data->rB].f64_value - registers[data->rC].f64_value;
        break;
      }

      case Opcodes::MultiplyI8: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].i8_value =
          registers[data->rB].i8_value * registers[data->rC].i8_value;
        break;
      }

      case Opcodes::MultiplyI16: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].i16_value =
          registers[data->rB].i16_value * registers[data->rC].i16_value;
        break;
      }

      case Opcodes::MultiplyI32: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].i32_value =
          registers[data->rB].i32_value * registers[data->rC].i32_value;
        break;
      }

      case Opcodes::MultiplyI64: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].i64_value =
          registers[data->rB].i64_value * registers[data->rC].i64_value;
        break;
      }

      case Opcodes::MultiplyU8: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u8_value =
          registers[data->rB].u8_value * registers[data->rC].u8_value;
        break;
      }

      case Opcodes::MultiplyU16: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u16_value =
          registers[data->rB].u16_value * registers[data->rC].u16_value;
        break;
      }

      case Opcodes::MultiplyU32: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u32_value =
          registers[data->rB].u32_value * registers[data->rC].u32_value;
        break;
      }

      case Opcodes::MultiplyU64: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u64_value =
          registers[data->rB].u64_value * registers[data->rC].u64_value;
        break;
      }

      case Opcodes::MultiplyF32: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].f32_value =
          registers[data->rB].f32_value * registers[data->rC].f32_value;
        break;
      }

      case Opcodes::MultiplyF64: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].f64_value =
          registers[data->rB].f64_value * registers[data->rC].f64_value;
        break;
      }

      case Opcodes::DivideI8: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].i8_value =
          registers[data->rB].i8_value / registers[data->rC].i8_value;
        break;
      }

      case Opcodes::DivideI16: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].i16_value =
          registers[data->rB].i16_value / registers[data->rC].i16_value;
        break;
      }

      case Opcodes::DivideI32: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].i32_value =
          registers[data->rB].i32_value / registers[data->rC].i32_value;
        break;
      }

      case Opcodes::DivideI64: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].i64_value =
          registers[data->rB].i64_value / registers[data->rC].i64_value;
        break;
      }

      case Opcodes::DivideU8: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u8_value =
          registers[data->rB].u8_value / registers[data->rC].u8_value;
        break;
      }

      case Opcodes::DivideU16: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u16_value =
          registers[data->rB].u16_value / registers[data->rC].u16_value;
        break;
      }

      case Opcodes::DivideU32: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u32_value =
          registers[data->rB].u32_value / registers[data->rC].u32_value;
        break;
      }

      case Opcodes::DivideU64: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].u64_value =
          registers[data->rB].u64_value / registers[data->rC].u64_value;
        break;
      }

      case Opcodes::DivideF32: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].f32_value =
          registers[data->rB].f32_value / registers[data->rC].f32_value;
        break;
      }

      case Opcodes::DivideF64: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].f64_value =
          registers[data->rB].f64_value / registers[data->rC].f64_value;
        break;
      }

      case Opcodes::SquareRootF32: {
        INSTRUCTION(Instruction16R2C0);
        registers[data->rA].f32_value = sqrtf(registers[data->rB].f32_value);
        break;
      }

      case Opcodes::SquareRootF64: {
        INSTRUCTION(Instruction16R2C0);
        registers[data->rA].f64_value = sqrt(registers[data->rB].f64_value);
        break;
      }

      case Opcodes::Extend8to16: {
        INSTRUCTION(Instruction16R2C0);
        registers[data->rA].i16_value = registers[data->rB].i8_value;
        break;
      }

      case Opcodes::Extend8to32: {
        INSTRUCTION(Instruction16R2C0);
        registers[data->rA].i32_value = registers[data->rB].i8_value;
        break;
      }

      case Opcodes::Extend8to64: {
        INSTRUCTION(Instruction16R2C0);
        registers[data->rA].i64_value = registers[data->rB].i8_value;
        break;
      }

      case Opcodes::Extend16to32: {
        INSTRUCTION(Instruction16R2C0);
        registers[data->rA].i32_value = registers[data->rB].i16_value;
        break;
      }

      case Opcodes::Extend16to64: {
        INSTRUCTION(Instruction16R2C0);
        registers[data->rA].i64_value = registers[data->rB].i16_value;
        break;
      }

      case Opcodes::Extend32to64: {
        INSTRUCTION(Instruction16R2C0);
        registers[data->rA].i64_value = registers[data->rB].i32_value;
        break;
      }

      case Opcodes::Float32to64: {
        INSTRUCTION(Instruction16R2C0);
        registers[data->rA].f64_value = registers[data->rB].f32_value;
        break;
      }

      case Opcodes::Float64to32: {
        INSTRUCTION(Instruction16R2C0);
        registers[data->rA].f32_value = registers[data->rB].f64_value;
        break;
      }

      case Opcodes::Cast8: { /* XXX */
        INSTRUCTION(Instruction16R2C0);
        registers[data->rA].u64_value = (u8)registers[data->rB].u64_value;
        break;
      }

      case Opcodes::Cast16: { /* XXX */
        INSTRUCTION(Instruction16R2C0);
        registers[data->rA].u64_value = (u16)registers[data->rB].u64_value;
        break;
      }

      case Opcodes::Cast32: { /* XXX */
        INSTRUCTION(Instruction16R2C0);
        registers[data->rA].u64_value = (u32)registers[data->rB].u64_value;
        break;
      }

      case Opcodes::Cast64: { /* XXX */
        INSTRUCTION(Instruction16R2C0);
        registers[data->rA].u64_value = registers[data->rB].u64_value;
        break;
      }

      case Opcodes::CastF32toI32: {
        INSTRUCTION(Instruction16R2C0);
        registers[data->rA].i32_value = registers[data->rB].f32_value;
        break;
      }

      case Opcodes::CastF64toI32: {
        INSTRUCTION(Instruction16R2C0);
        registers[data->rA].i32_value = registers[data->rB].f64_value;
        break;
      }

      case Opcodes::CastF32toI64: {
        INSTRUCTION(Instruction16R2C0);
        registers[data->rA].i64_value = registers[data->rB].f32_value;
        break;
      }

      case Opcodes::CastF64toI64: {
        INSTRUCTION(Instruction16R2C0);
        registers[data->rA].i64_value = registers[data->rB].f64_value;
        break;
      }

      case Opcodes::CastI32toF32: {
        INSTRUCTION(Instruction16R2C0);
        registers[data->rA].f32_value = registers[data->rB].i32_value;
        break;
      }

      case Opcodes::CastI32toF64: {
        INSTRUCTION(Instruction16R2C0);
        registers[data->rA].f64_value = registers[data->rB].i32_value;
        break;
      }

      case Opcodes::CastI64toF32: {
        INSTRUCTION(Instruction16R2C0);
        registers[data->rA].f32_value = registers[data->rB].i64_value;
        break;
      }

      case Opcodes::CastI64toF64: {
        INSTRUCTION(Instruction16R2C0);
        registers[data->rA].f64_value = registers[data->rB].i64_value;
        break;
      }

      case Opcodes::Test8: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].bool_value =
          !!(registers[data->rB].u8_value & registers[data->rC].u8_value);
        break;
      }

      case Opcodes::Test16: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].bool_value =
          !!(registers[data->rB].u16_value & registers[data->rC].u16_value);
        break;
      }

      case Opcodes::Test32: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].bool_value =
          !!(registers[data->rB].u32_value & registers[data->rC].u32_value);
        break;
      }

      case Opcodes::Test64: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].bool_value =
          !!(registers[data->rB].u64_value & registers[data->rC].u64_value);
        break;
      }

      case Opcodes::CompareEq8: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].bool_value =
          (registers[data->rB].u8_value == registers[data->rC].u8_value);
        break;
      }

      case Opcodes::CompareEq16: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].bool_value =
          (registers[data->rB].u16_value == registers[data->rC].u16_value);
        break;
      }

      case Opcodes::CompareEq32: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].bool_value =
          (registers[data->rB].u32_value == registers[data->rC].u32_value);
        break;
      }

      case Opcodes::CompareEq64: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].bool_value =
          (registers[data->rB].u64_value == registers[data->rC].u64_value);
        break;
      }

      case Opcodes::CompareEqF32: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].bool_value =
          (registers[data->rB].f32_value == registers[data->rC].f32_value);
        break;
      }

      case Opcodes::CompareEqF64: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].bool_value =
          (registers[data->rB].f64_value == registers[data->rC].f64_value);
        break;
      }

      case Opcodes::CompareEqBool: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].bool_value =
          (registers[data->rB].bool_value == registers[data->rC].bool_value);
        break;
      }

      case Opcodes::CompareLtI8: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].bool_value =
          (registers[data->rB].i8_value < registers[data->rC].i8_value);
        break;
      }

      case Opcodes::CompareLtI16: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].bool_value =
          (registers[data->rB].i16_value < registers[data->rC].i16_value);
        break;
      }

      case Opcodes::CompareLtI32: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].bool_value =
          (registers[data->rB].i32_value < registers[data->rC].i32_value);
        break;
      }

      case Opcodes::CompareLtI64: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].bool_value =
          (registers[data->rB].i64_value < registers[data->rC].i64_value);
        break;
      }

      case Opcodes::CompareLtU8: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].bool_value =
          (registers[data->rB].u8_value < registers[data->rC].u8_value);
        break;
      }

      case Opcodes::CompareLtU16: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].bool_value =
          (registers[data->rB].u16_value < registers[data->rC].u16_value);
        break;
      }

      case Opcodes::CompareLtU32: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].bool_value =
          (registers[data->rB].u32_value < registers[data->rC].u32_value);
        break;
      }

      case Opcodes::CompareLtU64: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].bool_value =
          (registers[data->rB].u64_value < registers[data->rC].u64_value);
        break;
      }

      case Opcodes::CompareLtF32: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].bool_value =
          (registers[data->rB].f32_value < registers[data->rC].f32_value);
        break;
      }

      case Opcodes::CompareLtF64: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].bool_value =
          (registers[data->rB].f64_value < registers[data->rC].f64_value);
        break;
      }

      case Opcodes::CompareLteI8: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].bool_value =
          (registers[data->rB].i8_value <= registers[data->rC].i8_value);
        break;
      }

      case Opcodes::CompareLteI16: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].bool_value =
          (registers[data->rB].i16_value <= registers[data->rC].i16_value);
        break;
      }

      case Opcodes::CompareLteI32: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].bool_value =
          (registers[data->rB].i32_value <= registers[data->rC].i32_value);
        break;
      }

      case Opcodes::CompareLteI64: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].bool_value =
          (registers[data->rB].i64_value <= registers[data->rC].i64_value);
        break;
      }

      case Opcodes::CompareLteU8: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].bool_value =
          (registers[data->rB].u8_value <= registers[data->rC].u8_value);
        break;
      }

      case Opcodes::CompareLteU16: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].bool_value =
          (registers[data->rB].u16_value <= registers[data->rC].u16_value);
        break;
      }

      case Opcodes::CompareLteU32: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].bool_value =
          (registers[data->rB].u32_value <= registers[data->rC].u32_value);
        break;
      }

      case Opcodes::CompareLteU64: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].bool_value =
          (registers[data->rB].u64_value <= registers[data->rC].u64_value);
        break;
      }

      case Opcodes::CompareLteF32: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].bool_value =
          (registers[data->rB].f32_value <= registers[data->rC].f32_value);
        break;
      }

      case Opcodes::CompareLteF64: {
        INSTRUCTION(Instruction32R3C0);
        registers[data->rA].bool_value =
          (registers[data->rB].f64_value <= registers[data->rC].f64_value);
        break;
      }

      case Opcodes::Select: {
        INSTRUCTION(Instruction32R4C0);
        registers[data->rA].u64_value = registers[data->rB].bool_value
                                          ? registers[data->rD].u64_value
                                          : registers[data->rC].u64_value;
        break;
      }

      case Opcodes::Exit: {
        INSTRUCTION(Instruction32R0C3);
        return data->constant;
      }

      case Opcodes::ExitIf: {
        INSTRUCTION(Instruction32R1C2);
        if (registers[data->rA].bool_value) {
          return data->constant;
        }
        break;
      }

      case Opcodes::HostVoidCall0: {
        typedef void (*function_t)(Guest *);
        INSTRUCTION(Instruction16R1C0);
        function_t function = reinterpret_cast<function_t>(registers[data->rA].u64_value);
        function(guest);
        break;
      }

      case Opcodes::HostCall0: {
        typedef ir::Constant (*function_t)(Guest *);
        INSTRUCTION(Instruction16R2C0);
        function_t function = reinterpret_cast<function_t>(registers[data->rB].u64_value);
        registers[data->rA] = function(guest);
        break;
      }

      case Opcodes::HostCall1: {
        typedef ir::Constant (*function_t)(Guest *, ir::Constant);
        INSTRUCTION(Instruction32R3C0);
        function_t function = reinterpret_cast<function_t>(registers[data->rB].u64_value);
        registers[data->rA] = function(guest, registers[data->rC]);
        break;
      }

      case Opcodes::HostCall2: {
        typedef ir::Constant (*function_t)(Guest *, ir::Constant, ir::Constant);
        INSTRUCTION(Instruction32R4C0);
        function_t function = reinterpret_cast<function_t>(registers[data->rB].u64_value);
        registers[data->rA] = function(guest, registers[data->rC], registers[data->rD]);
        break;
      }

      case Opcodes::LoadSpill: {
        INSTRUCTION(Instruction32R1C2);
        registers[data->rA] = spill[data->constant & 0x1f];
        break;
      }

      case Opcodes::StoreSpill: {
        INSTRUCTION(Instruction32R1C2);
        spill[data->constant & 0x1f] = registers[data->rA];
        break;
      }

      default:
        assert(false);
    }
  }
}

void
Routine::debug_print() const
{
  printf("%s", disassemble().c_str());
}

std::string
Routine::disassemble() const
{
  size_t offset = 0, line = 0;
  std::string result;

  /* Estimate of disassembly size - very rough. */
  result.reserve(m_bytes * 16u);

  while (true) {
    assert(offset < m_bytes);

    char buffer[128];
    const int prefix_length = snprintf(buffer, sizeof(buffer), "[%04lu] ", line++);
    result.append(buffer, prefix_length);
    switch (Opcodes(m_storage[offset])) {
      case Opcodes::Constant8: {
        INSTRUCTION(Instruction16R1C0);
        strcpy(
          buffer,
          fmt::format("imm8 r{}, #{:x}\n", data->rA, (u64) * (u8 *)&m_storage[offset])
            .c_str());
        offset += 1;
        break;
      }

      case Opcodes::Constant16: {
        INSTRUCTION(Instruction16R1C0);
        strcpy(
          buffer,
          fmt::format("imm16 r{}, #{:x}\n", data->rA, (u64) * (u16 *)&m_storage[offset])
            .c_str());
        offset += 2;
        break;
      }

      case Opcodes::Constant32: {
        INSTRUCTION(Instruction16R1C0);
        strcpy(
          buffer,
          fmt::format("imm32 r{}, #{:x}\n", data->rA, (u64) * (u32 *)&m_storage[offset])
            .c_str());
        offset += 4;
        break;
      }

      case Opcodes::Constant64: {
        INSTRUCTION(Instruction16R1C0);
        strcpy(
          buffer,
          fmt::format("imm64 r{}, #{:x}\n", data->rA, (u64) * (u64 *)&m_storage[offset])
            .c_str());
        offset += 8;
        break;
      }

      case Opcodes::ExtendConstant8: {
        INSTRUCTION(Instruction16R1C0);
        strcpy(buffer,
               fmt::format("imm8e r{}, #{}\n", data->rA, (i64) * (i8 *)&m_storage[offset])
                 .c_str());
        offset += 1;
        break;
      }

      case Opcodes::ExtendConstant16: {
        INSTRUCTION(Instruction16R1C0);
        strcpy(
          buffer,
          fmt::format("imm16e r{}, #{}\n", data->rA, (i64) * (i16 *)&m_storage[offset])
            .c_str());
        offset += 2;
        break;
      }

      case Opcodes::ExtendConstant32: {
        INSTRUCTION(Instruction16R1C0);
        strcpy(
          buffer,
          fmt::format("imm32e r{}, #{}\n", data->rA, (i64) * (i32 *)&m_storage[offset])
            .c_str());
        offset += 4;
        break;
      }

      case Opcodes::ReadRegister8: {
        INSTRUCTION(Instruction32R1C2);
        snprintf(buffer, sizeof(buffer), "readgr8 r%u, GR%u\n", data->rA, data->constant);
        break;
      }

      case Opcodes::ReadRegister16: {
        INSTRUCTION(Instruction32R1C2);
        snprintf(
          buffer, sizeof(buffer), "readgr16 r%u, GR%u\n", data->rA, data->constant);
        break;
      }

      case Opcodes::ReadRegister32: {
        INSTRUCTION(Instruction32R1C2);
        snprintf(
          buffer, sizeof(buffer), "readgr32 r%u, GR%u\n", data->rA, data->constant);
        break;
      }

      case Opcodes::ReadRegister64: {
        INSTRUCTION(Instruction32R1C2);
        snprintf(
          buffer, sizeof(buffer), "readgr64 r%u, GR%u\n", data->rA, data->constant);
        break;
      }

      case Opcodes::WriteRegister8: {
        INSTRUCTION(Instruction32R1C2);
        snprintf(
          buffer, sizeof(buffer), "writegr8 GR%u, r%u\n", data->constant, data->rA);
        break;
      }

      case Opcodes::WriteRegister16: {
        INSTRUCTION(Instruction32R1C2);
        snprintf(
          buffer, sizeof(buffer), "writegr16 GR%u, r%u\n", data->constant, data->rA);
        break;
      }

      case Opcodes::WriteRegister32: {
        INSTRUCTION(Instruction32R1C2);
        snprintf(
          buffer, sizeof(buffer), "writegr32 GR%u, r%u\n", data->constant, data->rA);
        break;
      }

      case Opcodes::WriteRegister64: {
        INSTRUCTION(Instruction32R1C2);
        snprintf(
          buffer, sizeof(buffer), "writegr64 GR%u, r%u\n", data->constant, data->rA);
        break;
      }

      case Opcodes::Load8: {
        INSTRUCTION(Instruction16R2C0);
        snprintf(buffer, sizeof(buffer), "load8 r%u, [r%u]\n", data->rA, data->rB);
        break;
      }

      case Opcodes::Load16: {
        INSTRUCTION(Instruction16R2C0);
        snprintf(buffer, sizeof(buffer), "load16 r%u, [r%u]\n", data->rA, data->rB);
        break;
      }

      case Opcodes::Load32: {
        INSTRUCTION(Instruction16R2C0);
        snprintf(buffer, sizeof(buffer), "load32 r%u, [r%u]\n", data->rA, data->rB);
        break;
      }

      case Opcodes::Load64: {
        INSTRUCTION(Instruction16R2C0);
        snprintf(buffer, sizeof(buffer), "load64 r%u, [r%u]\n", data->rA, data->rB);
        break;
      }

      case Opcodes::Store8: {
        INSTRUCTION(Instruction16R2C0);
        snprintf(buffer, sizeof(buffer), "store8 [r%u], r%u\n", data->rA, data->rB);
        break;
      }

      case Opcodes::Store16: {
        INSTRUCTION(Instruction16R2C0);
        snprintf(buffer, sizeof(buffer), "store16 [r%u], r%u\n", data->rA, data->rB);
        break;
      }

      case Opcodes::Store32: {
        INSTRUCTION(Instruction16R2C0);
        snprintf(buffer, sizeof(buffer), "store32 [r%u], r%u\n", data->rA, data->rB);
        break;
      }

      case Opcodes::Store64: {
        INSTRUCTION(Instruction16R2C0);
        snprintf(buffer, sizeof(buffer), "store64 [r%u], r%u\n", data->rA, data->rB);
        break;
      }

      case Opcodes::RotateRight8: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "rotr8 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::RotateRight16: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "rotr16 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::RotateRight32: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "rotr32 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::RotateRight64: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "rotr64 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::RotateLeft8: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "rotl8 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::RotateLeft16: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "rotl16 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::RotateLeft32: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "rotl32 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::RotateLeft64: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "rotl64 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::ShiftRight8: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(buffer,
                 sizeof(buffer),
                 "shiftr8 r%u, r%u, r%u\n",
                 data->rA,
                 data->rB,
                 data->rC);
        break;
      }

      case Opcodes::ShiftRight16: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(buffer,
                 sizeof(buffer),
                 "shiftr16 r%u, r%u, r%u\n",
                 data->rA,
                 data->rB,
                 data->rC);
        break;
      }

      case Opcodes::ShiftRight32: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(buffer,
                 sizeof(buffer),
                 "shiftr32 r%u, r%u, r%u\n",
                 data->rA,
                 data->rB,
                 data->rC);
        break;
      }

      case Opcodes::ShiftRight64: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(buffer,
                 sizeof(buffer),
                 "shiftr64 r%u, r%u, r%u\n",
                 data->rA,
                 data->rB,
                 data->rC);
        break;
      }

      case Opcodes::ShiftLeft8: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(buffer,
                 sizeof(buffer),
                 "shiftl8 r%u, r%u, r%u\n",
                 data->rA,
                 data->rB,
                 data->rC);
        break;
      }

      case Opcodes::ShiftLeft16: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(buffer,
                 sizeof(buffer),
                 "shiftl16 r%u, r%u, r%u\n",
                 data->rA,
                 data->rB,
                 data->rC);
        break;
      }

      case Opcodes::ShiftLeft32: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(buffer,
                 sizeof(buffer),
                 "shiftl32 r%u, r%u, r%u\n",
                 data->rA,
                 data->rB,
                 data->rC);
        break;
      }

      case Opcodes::ShiftLeft64: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(buffer,
                 sizeof(buffer),
                 "shiftl64 r%u, r%u, r%u\n",
                 data->rA,
                 data->rB,
                 data->rC);
        break;
      }

      case Opcodes::And8: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "and8 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::And16: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "and16 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::And32: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "and32 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::And64: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "and64 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::AndBool: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "andb r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::Or8: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "or8 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::Or16: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "or16 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::Or32: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "or32 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::Or64: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "or64 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::Xor8: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "xor8 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::Xor16: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "xor16 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::Xor32: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "xor32 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::Xor64: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "xor64 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::Not8: {
        INSTRUCTION(Instruction16R2C0);
        snprintf(buffer, sizeof(buffer), "not8 r%u, r%u\n", data->rA, data->rB);
        break;
      }

      case Opcodes::Not16: {
        INSTRUCTION(Instruction16R2C0);
        snprintf(buffer, sizeof(buffer), "not16 r%u, r%u\n", data->rA, data->rB);
        break;
      }

      case Opcodes::Not32: {
        INSTRUCTION(Instruction16R2C0);
        snprintf(buffer, sizeof(buffer), "not32 r%u, r%u\n", data->rA, data->rB);
        break;
      }

      case Opcodes::Not64: {
        INSTRUCTION(Instruction16R2C0);
        snprintf(buffer, sizeof(buffer), "not64 r%u, r%u\n", data->rA, data->rB);
        break;
      }

      case Opcodes::NotBool: {
        INSTRUCTION(Instruction16R2C0);
        snprintf(buffer, sizeof(buffer), "notb r%u, r%u\n", data->rA, data->rB);
        break;
      }

      case Opcodes::BitSetClear8: {
        INSTRUCTION(Instruction32R3C1);
        snprintf(buffer,
                 sizeof(buffer),
                 "bsc8 r%u, r%u, r%u, %u\n",
                 data->rA,
                 data->rB,
                 data->rC,
                 data->constant);
        break;
      }

      case Opcodes::BitSetClear16: {
        INSTRUCTION(Instruction32R3C1);
        snprintf(buffer,
                 sizeof(buffer),
                 "bsc16 r%u, r%u, r%u, %u\n",
                 data->rA,
                 data->rB,
                 data->rC,
                 data->constant);
        break;
      }

      case Opcodes::BitSetClear32: {
        INSTRUCTION(Instruction32R3C1);
        snprintf(buffer,
                 sizeof(buffer),
                 "bsc32 r%u, r%u, r%u, %u\n",
                 data->rA,
                 data->rB,
                 data->rC,
                 data->constant);
        break;
      }

      case Opcodes::BitSetClear64: {
        INSTRUCTION(Instruction32R3C1);
        snprintf(buffer,
                 sizeof(buffer),
                 "bsc64 r%u, r%u, r%u, %u\n",
                 data->rA,
                 data->rB,
                 data->rC,
                 data->constant);
        break;
      }

      case Opcodes::AddInteger: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "add r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::AddFloat32: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "addf32 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::AddFloat64: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "addf64 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::SubInteger8: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "sub8 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::SubInteger16: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "sub16 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::SubInteger32: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "sub32 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::SubInteger64: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "sub64 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::SubFloat32: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "subf32 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::SubFloat64: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "subf64 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::MultiplyI8: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "muls8 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::MultiplyI16: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "muls16 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::MultiplyI32: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "muls32 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::MultiplyI64: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "muls64 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::MultiplyU8: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "mulu8 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::MultiplyU16: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "mulu16 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::MultiplyU32: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "mulu32 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::MultiplyU64: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "mulu64 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::MultiplyF32: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "mulf32 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::MultiplyF64: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "mulf64 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::DivideI8: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "divs8 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::DivideI16: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "divs16 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::DivideI32: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "divs32 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::DivideI64: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "divs64 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::DivideU8: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "divu8 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::DivideU16: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "divu16 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::DivideU32: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "divu32 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::DivideU64: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "divu64 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::DivideF32: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "divf32 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::DivideF64: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "divf64 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::SquareRootF32: {
        INSTRUCTION(Instruction16R2C0);
        snprintf(buffer, sizeof(buffer), "sqrtf32 r%u, r%u\n", data->rA, data->rB);
        break;
      }

      case Opcodes::SquareRootF64: {
        INSTRUCTION(Instruction16R2C0);
        snprintf(buffer, sizeof(buffer), "sqrtf64 r%u, r%u\n", data->rA, data->rB);
        break;
      }

      case Opcodes::Extend8to16: {
        INSTRUCTION(Instruction16R2C0);
        snprintf(buffer, sizeof(buffer), "se8to16 r%u, r%u\n", data->rA, data->rB);
        break;
      }

      case Opcodes::Extend8to32: {
        INSTRUCTION(Instruction16R2C0);
        snprintf(buffer, sizeof(buffer), "se8to32 r%u, r%u\n", data->rA, data->rB);
        break;
      }

      case Opcodes::Extend8to64: {
        INSTRUCTION(Instruction16R2C0);
        snprintf(buffer, sizeof(buffer), "se8to64 r%u, r%u\n", data->rA, data->rB);
        break;
      }

      case Opcodes::Extend16to32: {
        INSTRUCTION(Instruction16R2C0);
        snprintf(buffer, sizeof(buffer), "se16to32 r%u, r%u\n", data->rA, data->rB);
        break;
      }

      case Opcodes::Extend16to64: {
        INSTRUCTION(Instruction16R2C0);
        snprintf(buffer, sizeof(buffer), "se16to64 r%u, r%u\n", data->rA, data->rB);
        break;
      }

      case Opcodes::Extend32to64: {
        INSTRUCTION(Instruction16R2C0);
        snprintf(buffer, sizeof(buffer), "se32to64 r%u, r%u\n", data->rA, data->rB);
        break;
      }

      case Opcodes::Float32to64: {
        INSTRUCTION(Instruction16R2C0);
        snprintf(buffer, sizeof(buffer), "f32to64 r%u, r%u\n", data->rA, data->rB);
        break;
      }

      case Opcodes::Float64to32: {
        INSTRUCTION(Instruction16R2C0);
        snprintf(buffer, sizeof(buffer), "f64to32 r%u, r%u\n", data->rA, data->rB);
        break;
      }

      case Opcodes::Cast8: { /* XXX */
        INSTRUCTION(Instruction16R2C0);
        snprintf(buffer, sizeof(buffer), "cast8 r%u, r%u\n", data->rA, data->rB);
        break;
      }

      case Opcodes::Cast16: { /* XXX */
        INSTRUCTION(Instruction16R2C0);
        snprintf(buffer, sizeof(buffer), "cast16 r%u, r%u\n", data->rA, data->rB);
        break;
      }

      case Opcodes::Cast32: { /* XXX */
        INSTRUCTION(Instruction16R2C0);
        snprintf(buffer, sizeof(buffer), "cast32 r%u, r%u\n", data->rA, data->rB);
        break;
      }

      case Opcodes::Cast64: { /* XXX */
        INSTRUCTION(Instruction16R2C0);
        snprintf(buffer, sizeof(buffer), "cast64 r%u, r%u\n", data->rA, data->rB);
        break;
      }

      case Opcodes::CastF32toI32: {
        INSTRUCTION(Instruction16R2C0);
        snprintf(buffer, sizeof(buffer), "f32toi32 r%u, r%u\n", data->rA, data->rB);
        break;
      }

      case Opcodes::CastF64toI32: {
        INSTRUCTION(Instruction16R2C0);
        snprintf(buffer, sizeof(buffer), "f64toi32 r%u, r%u\n", data->rA, data->rB);
        break;
      }

      case Opcodes::CastI32toF32: {
        INSTRUCTION(Instruction16R2C0);
        snprintf(buffer, sizeof(buffer), "i32tof32 r%u, r%u\n", data->rA, data->rB);
        break;
      }

      case Opcodes::CastI32toF64: {
        INSTRUCTION(Instruction16R2C0);
        snprintf(buffer, sizeof(buffer), "i32tof64 r%u, r%u\n", data->rA, data->rB);
        break;
      }

      case Opcodes::Test8: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "test8 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::Test16: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "test16 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::Test32: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "test32 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::Test64: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "test64 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::CompareEq8: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "cmpeq8 r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::CompareEq16: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(buffer,
                 sizeof(buffer),
                 "cmpeq16 r%u, r%u, r%u\n",
                 data->rA,
                 data->rB,
                 data->rC);
        break;
      }

      case Opcodes::CompareEq32: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(buffer,
                 sizeof(buffer),
                 "cmpeq32 r%u, r%u, r%u\n",
                 data->rA,
                 data->rB,
                 data->rC);
        break;
      }

      case Opcodes::CompareEq64: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(buffer,
                 sizeof(buffer),
                 "cmpeq64 r%u, r%u, r%u\n",
                 data->rA,
                 data->rB,
                 data->rC);
        break;
      }

      case Opcodes::CompareEqF32: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(buffer,
                 sizeof(buffer),
                 "cmpeq32f r%u, r%u, r%u\n",
                 data->rA,
                 data->rB,
                 data->rC);
        break;
      }

      case Opcodes::CompareEqF64: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(buffer,
                 sizeof(buffer),
                 "cmpeq64f r%u, r%u, r%u\n",
                 data->rA,
                 data->rB,
                 data->rC);
        break;
      }

      case Opcodes::CompareEqBool: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "cmpeqb r%u, r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::CompareLtI8: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(buffer,
                 sizeof(buffer),
                 "cmplt8s r%u, r%u, r%u\n",
                 data->rA,
                 data->rB,
                 data->rC);
        break;
      }

      case Opcodes::CompareLtI16: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(buffer,
                 sizeof(buffer),
                 "cmplt16s r%u, r%u, r%u\n",
                 data->rA,
                 data->rB,
                 data->rC);
        break;
      }

      case Opcodes::CompareLtI32: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(buffer,
                 sizeof(buffer),
                 "cmplt32s r%u, r%u, r%u\n",
                 data->rA,
                 data->rB,
                 data->rC);
        break;
      }

      case Opcodes::CompareLtI64: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(buffer,
                 sizeof(buffer),
                 "cmplt64s r%u, r%u, r%u\n",
                 data->rA,
                 data->rB,
                 data->rC);
        break;
      }

      case Opcodes::CompareLtU8: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(buffer,
                 sizeof(buffer),
                 "cmplt8u r%u, r%u, r%u\n",
                 data->rA,
                 data->rB,
                 data->rC);
        break;
      }

      case Opcodes::CompareLtU16: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(buffer,
                 sizeof(buffer),
                 "cmplt16u r%u, r%u, r%u\n",
                 data->rA,
                 data->rB,
                 data->rC);
        break;
      }

      case Opcodes::CompareLtU32: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(buffer,
                 sizeof(buffer),
                 "cmplt32u r%u, r%u, r%u\n",
                 data->rA,
                 data->rB,
                 data->rC);
        break;
      }

      case Opcodes::CompareLtU64: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(buffer,
                 sizeof(buffer),
                 "cmplt64u r%u, r%u, r%u\n",
                 data->rA,
                 data->rB,
                 data->rC);
        break;
      }

      case Opcodes::CompareLtF32: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(buffer,
                 sizeof(buffer),
                 "cmplt32f r%u, r%u, r%u\n",
                 data->rA,
                 data->rB,
                 data->rC);
        break;
      }

      case Opcodes::CompareLtF64: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(buffer,
                 sizeof(buffer),
                 "cmplt64f r%u, r%u, r%u\n",
                 data->rA,
                 data->rB,
                 data->rC);
        break;
      }

      case Opcodes::CompareLteI8: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(buffer,
                 sizeof(buffer),
                 "cmplte8s r%u, r%u, r%u\n",
                 data->rA,
                 data->rB,
                 data->rC);
        break;
      }

      case Opcodes::CompareLteI16: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(buffer,
                 sizeof(buffer),
                 "cmplte16s r%u, r%u, r%u\n",
                 data->rA,
                 data->rB,
                 data->rC);
        break;
      }

      case Opcodes::CompareLteI32: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(buffer,
                 sizeof(buffer),
                 "cmplte32s r%u, r%u, r%u\n",
                 data->rA,
                 data->rB,
                 data->rC);
        break;
      }

      case Opcodes::CompareLteI64: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(buffer,
                 sizeof(buffer),
                 "cmplte64s r%u, r%u, r%u\n",
                 data->rA,
                 data->rB,
                 data->rC);
        break;
      }

      case Opcodes::CompareLteU8: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(buffer,
                 sizeof(buffer),
                 "cmplte8u r%u, r%u, r%u\n",
                 data->rA,
                 data->rB,
                 data->rC);
        break;
      }

      case Opcodes::CompareLteU16: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(buffer,
                 sizeof(buffer),
                 "cmplte16u r%u, r%u, r%u\n",
                 data->rA,
                 data->rB,
                 data->rC);
        break;
      }

      case Opcodes::CompareLteU32: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(buffer,
                 sizeof(buffer),
                 "cmplte32u r%u, r%u, r%u\n",
                 data->rA,
                 data->rB,
                 data->rC);
        break;
      }

      case Opcodes::CompareLteU64: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(buffer,
                 sizeof(buffer),
                 "cmplte64u r%u, r%u, r%u\n",
                 data->rA,
                 data->rB,
                 data->rC);
        break;
      }

      case Opcodes::CompareLteF32: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(buffer,
                 sizeof(buffer),
                 "cmplte32f r%u, r%u, r%u\n",
                 data->rA,
                 data->rB,
                 data->rC);
        break;
      }

      case Opcodes::CompareLteF64: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(buffer,
                 sizeof(buffer),
                 "cmplte64f r%u, r%u, r%u\n",
                 data->rA,
                 data->rB,
                 data->rC);
        break;
      }

      case Opcodes::Select: {
        INSTRUCTION(Instruction32R4C0);
        snprintf(buffer,
                 sizeof(buffer),
                 "select r%u, r%u, r%u, r%u\n",
                 data->rA,
                 data->rB,
                 data->rC,
                 data->rD);
        break;
      }

      case Opcodes::Exit: {
        INSTRUCTION(Instruction32R0C3);
        snprintf(buffer, sizeof(buffer), "exit 0x%06x\n", data->constant);
        result.append(buffer);
        return result;
      }

      case Opcodes::ExitIf: {
        INSTRUCTION(Instruction32R1C2);
        snprintf(
          buffer, sizeof(buffer), "exitif r%u, 0x%04x\n", data->rA, data->constant);
        break;
      }

      case Opcodes::HostVoidCall0: {
        INSTRUCTION(Instruction16R1C0);
        snprintf(buffer, sizeof(buffer), "call @r%u\n", data->rA);
        break;
      }

      case Opcodes::HostCall0: {
        INSTRUCTION(Instruction16R2C0);
        snprintf(buffer, sizeof(buffer), "call r%u, @r%u\n", data->rA, data->rB);
        break;
      }

      case Opcodes::HostCall1: {
        INSTRUCTION(Instruction32R3C0);
        snprintf(
          buffer, sizeof(buffer), "call r%u, @r%u, r%u\n", data->rA, data->rB, data->rC);
        break;
      }

      case Opcodes::HostCall2: {
        INSTRUCTION(Instruction32R4C0);
        snprintf(buffer,
                 sizeof(buffer),
                 "call r%u, @r%u, r%u, r%u\n",
                 data->rA,
                 data->rB,
                 data->rC,
                 data->rD);
        break;
      }

      case Opcodes::LoadSpill: {
        INSTRUCTION(Instruction32R1C2);
        snprintf(buffer, sizeof(buffer), "rspill r%u, #%u\n", data->rA, data->constant);
        break;
      }

      case Opcodes::StoreSpill: {
        INSTRUCTION(Instruction32R1C2);
        snprintf(buffer, sizeof(buffer), "wspill #%u, r%u\n", data->constant, data->rA);
        break;
      }

      default:
        assert(false && "Unhandled opcode in disassemble");
    }

    result.append(buffer);
  }

  return result;
}

}
}
