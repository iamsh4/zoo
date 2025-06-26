// vim: expandtab:ts=2:sw=2

#include <climits>
#include <cstring>

#include "fox/jit/linear_register_allocator.h"
#include "fox/bytecode/compiler.h"
#include "fox/bytecode/opcode.h"

namespace fox {
namespace bytecode {

#define ENCODE_R0C0(_opcode)                                                             \
  do {                                                                                   \
    Instruction8R0C0 ____i;                                                              \
    ____i.opcode = (u8)_opcode;                                                          \
    memcpy(&m_result[m_result_size], &____i, sizeof(____i));                             \
    m_result_size += sizeof(____i);                                                      \
  } while (0)

#define ENCODE_R0C3(_opcode, _constant)                                                  \
  do {                                                                                   \
    Instruction32R0C3 ____i;                                                             \
    ____i.opcode = (u8)_opcode;                                                          \
    ____i.constant = _constant;                                                          \
    memcpy(&m_result[m_result_size], &____i, sizeof(____i));                             \
    m_result_size += sizeof(____i);                                                      \
  } while (0)

#define ENCODE_R1C0(_opcode, _rA)                                                        \
  do {                                                                                   \
    Instruction16R1C0 ____i;                                                             \
    ____i.opcode = (u8)_opcode;                                                          \
    ____i.rA = _rA;                                                                      \
    memcpy(&m_result[m_result_size], &____i, sizeof(____i));                             \
    m_result_size += sizeof(____i);                                                      \
  } while (0)

#define ENCODE_R1C2(_opcode, _constant, _rA)                                             \
  do {                                                                                   \
    Instruction32R1C2 ____i;                                                             \
    ____i.opcode = (u8)_opcode;                                                          \
    ____i.rA = _rA;                                                                      \
    ____i.constant = _constant;                                                          \
    memcpy(&m_result[m_result_size], &____i, sizeof(____i));                             \
    m_result_size += sizeof(____i);                                                      \
  } while (0)

#define ENCODE_R2C0(_opcode, _rA, _rB)                                                   \
  do {                                                                                   \
    Instruction16R2C0 ____i;                                                             \
    ____i.opcode = (u8)_opcode;                                                          \
    ____i.rA = _rA;                                                                      \
    ____i.rB = _rB;                                                                      \
    memcpy(&m_result[m_result_size], &____i, sizeof(____i));                             \
    m_result_size += sizeof(____i);                                                      \
  } while (0)

#define ENCODE_R3C0(_opcode, _rA, _rB, _rC)                                              \
  do {                                                                                   \
    Instruction32R3C0 ____i;                                                             \
    ____i.opcode = (u8)_opcode;                                                          \
    ____i.rA = _rA;                                                                      \
    ____i.rB = _rB;                                                                      \
    ____i.rC = _rC;                                                                      \
    memcpy(&m_result[m_result_size], &____i, sizeof(____i));                             \
    m_result_size += sizeof(____i);                                                      \
  } while (0)

#define ENCODE_R3C1(_opcode, _constant, _rA, _rB, _rC)                                   \
  do {                                                                                   \
    Instruction32R3C1 ____i;                                                             \
    ____i.opcode = (u8)_opcode;                                                          \
    ____i.rA = _rA;                                                                      \
    ____i.rB = _rB;                                                                      \
    ____i.rC = _rC;                                                                      \
    ____i.constant = _constant;                                                          \
    memcpy(&m_result[m_result_size], &____i, sizeof(____i));                             \
    m_result_size += sizeof(____i);                                                      \
  } while (0)

#define ENCODE_R4C0(_opcode, _rA, _rB, _rC, _rD)                                         \
  do {                                                                                   \
    Instruction32R4C0 ____i;                                                             \
    ____i.opcode = (u8)_opcode;                                                          \
    ____i.rA = _rA;                                                                      \
    ____i.rB = _rB;                                                                      \
    ____i.rC = _rC;                                                                      \
    ____i.rD = _rD;                                                                      \
    memcpy(&m_result[m_result_size], &____i, sizeof(____i));                             \
    m_result_size += sizeof(____i);                                                      \
  } while (0)

static constexpr jit::HwRegister::Type RegisterType(jit::HwRegister::Type(1));

std::unique_ptr<jit::Routine>
Compiler::compile(ir::ExecutionUnit &&in_source)
{
  m_source = std::move(in_source);

  generate_rtl();
  assign_registers();
  assemble();

  if (false) {
    /* Debug */
    printf("====================================================\n");
    m_rtl.debug_print(&rtl_opcode_names);
    printf("Spill: %u\n", m_rtl.spill_size());
    printf("====================================================\n");
  }

  u8 *const allocation = new u8[m_result_size];
  memcpy(allocation, m_result, m_result_size);
  return std::unique_ptr<jit::Routine>(new Routine(allocation, m_result_size));
}

#define OPCODE(x) u16(Opcodes::x)
#define R_ANY(ssa)                                                                       \
  jit::RegisterAssignment                                                                \
  {                                                                                      \
    ssa, RegisterType                                                                    \
  }

void
Compiler::generate_rtl()
{
  m_rtl.clear();

  /* Allocate the single EBB used for all generated instructions. */
  /* TODO Support control flow once required logic is available in RTL. */
  jit::RtlProgram::BlockHandle block_handle = m_rtl.allocate_block("bytecode_entry");
  jit::RtlInstructions &block = m_rtl.block(block_handle);
  assert(block_handle == 0);

  /* Perform mostly 1:1 translation of IR instructions to RTL bytecode. */
  for (const ir::Instruction &current : m_source.instructions()) {
    switch (current.opcode()) {
      /* Read from a guest register in host memory. */
      case ir::Opcode::ReadGuest: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const u16 index = current.source(0).zero_extended();
        switch (current.result(0).type()) {
          case ir::Type::Integer8:
            block.append(OPCODE(ReadRegister8),
                         jit::Value { .u16_value = index },
                         { R_ANY(ssa_result) },
                         {});
            break;

          case ir::Type::Integer16:
            block.append(OPCODE(ReadRegister16),
                         jit::Value { .u16_value = index },
                         { R_ANY(ssa_result) },
                         {});
            break;

          case ir::Type::Integer32:
          case ir::Type::Float32:
            block.append(OPCODE(ReadRegister32),
                         jit::Value { .u16_value = index },
                         { R_ANY(ssa_result) },
                         {});
            break;

          case ir::Type::Integer64:
          case ir::Type::Float64:
            block.append(OPCODE(ReadRegister64),
                         jit::Value { .u16_value = index },
                         { R_ANY(ssa_result) },
                         {});
            break;

          default:
            assert(false);
        }
        break;
      }

      /* Write to a guest register in host memory. */
      case ir::Opcode::WriteGuest: {
        const u16 index = current.source(0).zero_extended();
        const jit::RtlRegister ssa_value = get_rtl_ssa(current.source(1));
        switch (current.source(1).type()) {
          case ir::Type::Integer8:
            block.append(OPCODE(WriteRegister8),
                         jit::Value { .u16_value = index },
                         {},
                         { R_ANY(ssa_value) });
            break;

          case ir::Type::Integer16:
            block.append(OPCODE(WriteRegister16),
                         jit::Value { .u16_value = index },
                         {},
                         { R_ANY(ssa_value) });
            break;

          case ir::Type::Integer32:
          case ir::Type::Float32:
            block.append(OPCODE(WriteRegister32),
                         jit::Value { .u16_value = index },
                         {},
                         { R_ANY(ssa_value) });
            break;

          case ir::Type::Integer64:
          case ir::Type::Float64:
            block.append(OPCODE(WriteRegister64),
                         jit::Value { .u16_value = index },
                         {},
                         { R_ANY(ssa_value) });
            break;

          default:
            assert(false);
        }
        break;
      }

      /* Load a value from guest memory. */
      case ir::Opcode::Load: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_address = get_rtl_ssa(current.source(0));
        switch (current.result(0).type()) {
          case ir::Type::Integer8:
            block.append(OPCODE(Load8), { R_ANY(ssa_result) }, { R_ANY(ssa_address) });
            break;

          case ir::Type::Integer16:
            block.append(OPCODE(Load16), { R_ANY(ssa_result) }, { R_ANY(ssa_address) });
            break;

          case ir::Type::Integer32:
          case ir::Type::Float32:
            block.append(OPCODE(Load32), { R_ANY(ssa_result) }, { R_ANY(ssa_address) });
            break;

          case ir::Type::Integer64:
          case ir::Type::Float64:
            block.append(OPCODE(Load64), { R_ANY(ssa_result) }, { R_ANY(ssa_address) });
            break;

          default:
            assert(false);
        }
        break;
      }

      /* Store a value to guest memory. */
      case ir::Opcode::Store: {
        const jit::RtlRegister ssa_address = get_rtl_ssa(current.source(0));
        const jit::RtlRegister ssa_value = get_rtl_ssa(current.source(1));
        switch (current.source(1).type()) {
          case ir::Type::Integer8:
            block.append(OPCODE(Store8), {}, { R_ANY(ssa_address), R_ANY(ssa_value) });
            break;

          case ir::Type::Integer16:
            block.append(OPCODE(Store16), {}, { R_ANY(ssa_address), R_ANY(ssa_value) });
            break;

          case ir::Type::Integer32:
          case ir::Type::Float32:
            block.append(OPCODE(Store32), {}, { R_ANY(ssa_address), R_ANY(ssa_value) });
            break;

          case ir::Type::Integer64:
          case ir::Type::Float64:
            block.append(OPCODE(Store64), {}, { R_ANY(ssa_address), R_ANY(ssa_value) });
            break;

          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::LogicalShiftRight: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_value = get_rtl_ssa(current.source(0));
        const jit::RtlRegister ssa_bits = get_rtl_ssa(current.source(1));
        switch (current.result(0).type()) {
          case ir::Type::Integer8:
            block.append(OPCODE(ShiftRight8),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_value), R_ANY(ssa_bits) });
            break;

          case ir::Type::Integer16:
            block.append(OPCODE(ShiftRight16),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_value), R_ANY(ssa_bits) });
            break;

          case ir::Type::Integer32:
            block.append(OPCODE(ShiftRight32),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_value), R_ANY(ssa_bits) });
            break;

          case ir::Type::Integer64:
            block.append(OPCODE(ShiftRight64),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_value), R_ANY(ssa_bits) });
            break;

          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::LogicalShiftLeft: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_value = get_rtl_ssa(current.source(0));
        const jit::RtlRegister ssa_bits = get_rtl_ssa(current.source(1));
        switch (current.result(0).type()) {
          case ir::Type::Integer8:
            block.append(OPCODE(ShiftLeft8),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_value), R_ANY(ssa_bits) });
            break;

          case ir::Type::Integer16:
            block.append(OPCODE(ShiftLeft16),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_value), R_ANY(ssa_bits) });
            break;

          case ir::Type::Integer32:
            block.append(OPCODE(ShiftLeft32),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_value), R_ANY(ssa_bits) });
            break;

          case ir::Type::Integer64:
            block.append(OPCODE(ShiftLeft64),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_value), R_ANY(ssa_bits) });
            break;

          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::RotateRight: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_value = get_rtl_ssa(current.source(0));
        const jit::RtlRegister ssa_bits = get_rtl_ssa(current.source(1));
        switch (current.result(0).type()) {
          case ir::Type::Integer8:
            block.append(OPCODE(RotateRight8),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_value), R_ANY(ssa_bits) });
            break;

          case ir::Type::Integer16:
            block.append(OPCODE(RotateRight16),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_value), R_ANY(ssa_bits) });
            break;

          case ir::Type::Integer32:
            block.append(OPCODE(RotateRight32),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_value), R_ANY(ssa_bits) });
            break;

          case ir::Type::Integer64:
            block.append(OPCODE(RotateRight64),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_value), R_ANY(ssa_bits) });
            break;

          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::RotateLeft: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_value = get_rtl_ssa(current.source(0));
        const jit::RtlRegister ssa_bits = get_rtl_ssa(current.source(1));
        switch (current.result(0).type()) {
          case ir::Type::Integer8:
            block.append(OPCODE(RotateLeft8),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_value), R_ANY(ssa_bits) });
            break;

          case ir::Type::Integer16:
            block.append(OPCODE(RotateLeft16),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_value), R_ANY(ssa_bits) });
            break;

          case ir::Type::Integer32:
            block.append(OPCODE(RotateLeft32),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_value), R_ANY(ssa_bits) });
            break;

          case ir::Type::Integer64:
            block.append(OPCODE(RotateLeft64),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_value), R_ANY(ssa_bits) });
            break;

          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::ArithmeticShiftRight: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_value = get_rtl_ssa(current.source(0));
        const jit::RtlRegister ssa_bits = get_rtl_ssa(current.source(1));
        switch (current.result(0).type()) {
          case ir::Type::Integer8:
            block.append(OPCODE(ArithmeticShiftRight8),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_value), R_ANY(ssa_bits) });
            break;

          case ir::Type::Integer16:
            block.append(OPCODE(ArithmeticShiftRight16),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_value), R_ANY(ssa_bits) });
            break;

          case ir::Type::Integer32:
            block.append(OPCODE(ArithmeticShiftRight32),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_value), R_ANY(ssa_bits) });
            break;

          case ir::Type::Integer64:
            block.append(OPCODE(ArithmeticShiftRight64),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_value), R_ANY(ssa_bits) });
            break;

          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::And: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(0));
        const jit::RtlRegister ssa_arg2 = get_rtl_ssa(current.source(1));
        switch (current.result(0).type()) {
          case ir::Type::Integer8:
            block.append(
              OPCODE(And8), { R_ANY(ssa_result) }, { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer16:
            block.append(
              OPCODE(And16), { R_ANY(ssa_result) }, { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer32:
            block.append(
              OPCODE(And32), { R_ANY(ssa_result) }, { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer64:
            block.append(
              OPCODE(And64), { R_ANY(ssa_result) }, { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Bool:
            block.append(OPCODE(AndBool),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::Or: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(0));
        const jit::RtlRegister ssa_arg2 = get_rtl_ssa(current.source(1));
        switch (current.result(0).type()) {
          case ir::Type::Integer8:
            block.append(
              OPCODE(Or8), { R_ANY(ssa_result) }, { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer16:
            block.append(
              OPCODE(Or16), { R_ANY(ssa_result) }, { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer32:
            block.append(
              OPCODE(Or32), { R_ANY(ssa_result) }, { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer64:
            block.append(
              OPCODE(Or64), { R_ANY(ssa_result) }, { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Bool:
            block.append(OPCODE(OrBool),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::ExclusiveOr: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(0));
        const jit::RtlRegister ssa_arg2 = get_rtl_ssa(current.source(1));
        switch (current.result(0).type()) {
          case ir::Type::Integer8:
            block.append(
              OPCODE(Xor8), { R_ANY(ssa_result) }, { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer16:
            block.append(
              OPCODE(Xor16), { R_ANY(ssa_result) }, { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer32:
            block.append(
              OPCODE(Xor32), { R_ANY(ssa_result) }, { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer64:
            block.append(
              OPCODE(Xor64), { R_ANY(ssa_result) }, { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Bool:
            assert(false && "todo");
            /*
            block.append(OPCODE(XorBool),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            */
            break;

          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::Not: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(0));
        switch (current.result(0).type()) {
          case ir::Type::Integer8:
            block.append(OPCODE(Not8), { R_ANY(ssa_result) }, { R_ANY(ssa_arg1) });
            break;

          case ir::Type::Integer16:
            block.append(OPCODE(Not16), { R_ANY(ssa_result) }, { R_ANY(ssa_arg1) });
            break;

          case ir::Type::Integer32:
            block.append(OPCODE(Not32), { R_ANY(ssa_result) }, { R_ANY(ssa_arg1) });
            break;

          case ir::Type::Integer64:
            block.append(OPCODE(Not64), { R_ANY(ssa_result) }, { R_ANY(ssa_arg1) });
            break;

          case ir::Type::Bool:
            block.append(OPCODE(NotBool), { R_ANY(ssa_result) }, { R_ANY(ssa_arg1) });
            break;

          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::BitSetClear: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(0));
        const jit::RtlRegister ssa_arg2 = get_rtl_ssa(current.source(1));
        const u8 bit = current.source(2).zero_extended();
        switch (current.result(0).type()) {
          case ir::Type::Integer8:
            block.append(OPCODE(BitSetClear8),
                         jit::Value { .u8_value = bit },
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer16:
            block.append(OPCODE(BitSetClear16),
                         jit::Value { .u8_value = bit },
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer32:
            block.append(OPCODE(BitSetClear32),
                         jit::Value { .u8_value = bit },
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer64:
            block.append(OPCODE(BitSetClear64),
                         jit::Value { .u8_value = bit },
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::Add: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(0));
        const jit::RtlRegister ssa_arg2 = get_rtl_ssa(current.source(1));
        switch (current.result(0).type()) {
          case ir::Type::Integer8:
          case ir::Type::Integer16:
          case ir::Type::Integer32:
          case ir::Type::Integer64:
            block.append(OPCODE(AddInteger),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Float32:
            block.append(OPCODE(AddFloat32),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Float64:
            block.append(OPCODE(AddFloat64),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::Subtract: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(0));
        const jit::RtlRegister ssa_arg2 = get_rtl_ssa(current.source(1));
        switch (current.result(0).type()) {
          case ir::Type::Integer8:
            block.append(OPCODE(SubInteger8),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer16:
            block.append(OPCODE(SubInteger16),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer32:
            block.append(OPCODE(SubInteger32),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer64:
            block.append(OPCODE(SubInteger64),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Float32:
            block.append(OPCODE(SubFloat32),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Float64:
            block.append(OPCODE(SubFloat64),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::Multiply: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(0));
        const jit::RtlRegister ssa_arg2 = get_rtl_ssa(current.source(1));
        switch (current.result(0).type()) {
          case ir::Type::Integer8:
            block.append(OPCODE(MultiplyI8),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer16:
            block.append(OPCODE(MultiplyI16),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer32:
            block.append(OPCODE(MultiplyI32),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer64:
            block.append(OPCODE(MultiplyI64),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Float32:
            block.append(OPCODE(MultiplyF32),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Float64:
            block.append(OPCODE(MultiplyF64),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::Multiply_u: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(0));
        const jit::RtlRegister ssa_arg2 = get_rtl_ssa(current.source(1));
        switch (current.result(0).type()) {
          case ir::Type::Integer8:
            block.append(OPCODE(MultiplyU8),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer16:
            block.append(OPCODE(MultiplyU16),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer32:
            block.append(OPCODE(MultiplyU32),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer64:
            block.append(OPCODE(MultiplyU64),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::Divide: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(0));
        const jit::RtlRegister ssa_arg2 = get_rtl_ssa(current.source(1));
        switch (current.result(0).type()) {
          case ir::Type::Integer8:
            block.append(OPCODE(DivideI8),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer16:
            block.append(OPCODE(DivideI16),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer32:
            block.append(OPCODE(DivideI32),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer64:
            block.append(OPCODE(DivideI64),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Float32:
            block.append(OPCODE(DivideF32),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Float64:
            block.append(OPCODE(DivideF64),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::Divide_u: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(0));
        const jit::RtlRegister ssa_arg2 = get_rtl_ssa(current.source(1));
        switch (current.result(0).type()) {
          case ir::Type::Integer8:
            block.append(OPCODE(MultiplyU8),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer16:
            block.append(OPCODE(MultiplyU16),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer32:
            block.append(OPCODE(MultiplyU32),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer64:
            block.append(OPCODE(MultiplyU64),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::SquareRoot: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(0));
        switch (current.result(0).type()) {
          case ir::Type::Float32:
            block.append(
              OPCODE(SquareRootF32), { R_ANY(ssa_result) }, { R_ANY(ssa_arg1) });
            break;

          case ir::Type::Float64:
            block.append(
              OPCODE(SquareRootF64), { R_ANY(ssa_result) }, { R_ANY(ssa_arg1) });
            break;

          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::Call: {
        const jit::RtlRegister function = get_rtl_ssa(current.source(0));
        if (current.result_count() == 0) {
          assert(current.source_count() == 1);
          block.append(OPCODE(HostVoidCall0), {}, { R_ANY(function) });
          break;
        }

        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        if (current.source_count() == 1) {
          assert(current.result_count() == 1);
          block.append(OPCODE(HostCall0), { R_ANY(ssa_result) }, { R_ANY(function) });
        } else if (current.source_count() == 2) {
          assert(current.result_count() == 1);

          const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(1));
          block.append(OPCODE(HostCall1),
                       { R_ANY(ssa_result) },
                       { R_ANY(function), R_ANY(ssa_arg1) });
        } else if (current.source_count() == 3) {
          assert(current.result_count() == 1);

          const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(1));
          const jit::RtlRegister ssa_arg2 = get_rtl_ssa(current.source(2));
          block.append(OPCODE(HostCall2),
                       { R_ANY(ssa_result) },
                       { R_ANY(function), R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
        } else {
          assert(false);
        }
        break;
      }

      case ir::Opcode::Extend16: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(0));
        switch (current.source(0).type()) {
          case ir::Type::Integer8:
            block.append(OPCODE(Extend8to16), { R_ANY(ssa_result) }, { R_ANY(ssa_arg1) });
            break;

          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::Extend32: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(0));
        switch (current.source(0).type()) {
          case ir::Type::Integer8:
            block.append(OPCODE(Extend8to32), { R_ANY(ssa_result) }, { R_ANY(ssa_arg1) });
            break;

          case ir::Type::Integer16:
            block.append(
              OPCODE(Extend16to32), { R_ANY(ssa_result) }, { R_ANY(ssa_arg1) });
            break;

          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::Extend64: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(0));
        switch (current.source(0).type()) {
          case ir::Type::Integer8:
            block.append(OPCODE(Extend8to64), { R_ANY(ssa_result) }, { R_ANY(ssa_arg1) });
            break;

          case ir::Type::Integer16:
            block.append(
              OPCODE(Extend16to64), { R_ANY(ssa_result) }, { R_ANY(ssa_arg1) });
            break;

          case ir::Type::Integer32:
            block.append(
              OPCODE(Extend32to64), { R_ANY(ssa_result) }, { R_ANY(ssa_arg1) });
            break;

          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::BitCast: {
        /* XXX I don't think up-bitcasts are necessary, because the implementation
         *     is always careful to clear upper bits for smaller registers. But
         *     we still need to update register map details and potentially
         *     load constant values, so for now we'll include it for simplicity. */
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(0));
        switch (current.result(0).type()) {
          case ir::Type::Integer8:
            block.append(OPCODE(Cast8), { R_ANY(ssa_result) }, { R_ANY(ssa_arg1) });
            break;

          case ir::Type::Integer16:
            block.append(OPCODE(Cast16), { R_ANY(ssa_result) }, { R_ANY(ssa_arg1) });
            break;

          case ir::Type::Integer32:
          case ir::Type::Float32:
            block.append(OPCODE(Cast32), { R_ANY(ssa_result) }, { R_ANY(ssa_arg1) });
            break;

          case ir::Type::Integer64:
          case ir::Type::Float64:
            block.append(OPCODE(Cast64), { R_ANY(ssa_result) }, { R_ANY(ssa_arg1) });
            break;

          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::CastFloatInt: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(0));
        if (current.source(0).type() == ir::Type::Float32) {
          switch (current.result(0).type()) {
            case ir::Type::Integer32:
              block.append(
                OPCODE(CastF32toI32), { R_ANY(ssa_result) }, { R_ANY(ssa_arg1) });
              break;

            case ir::Type::Integer64:
              block.append(
                OPCODE(CastF32toI64), { R_ANY(ssa_result) }, { R_ANY(ssa_arg1) });
              break;

            default:
              assert(false && "Float to integer only supports 32/64 bit");
          }
        } else {
          assert(current.source(0).type() == ir::Type::Float64);
          switch (current.result(0).type()) {
            case ir::Type::Integer32:
              block.append(
                OPCODE(CastF64toI32), { R_ANY(ssa_result) }, { R_ANY(ssa_arg1) });
              break;

            case ir::Type::Integer64:
              block.append(
                OPCODE(CastF64toI64), { R_ANY(ssa_result) }, { R_ANY(ssa_arg1) });
              break;

            default:
              assert(false && "Float to integer only supports 32/64 bit");
          }
        }
        break;
      }

      case ir::Opcode::CastIntFloat: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(0));
        if (current.result(0).type() == ir::Type::Float32) {
          switch (current.source(0).type()) {
            case ir::Type::Integer32:
              block.append(
                OPCODE(CastI32toF32), { R_ANY(ssa_result) }, { R_ANY(ssa_arg1) });
              break;

            case ir::Type::Integer64:
              block.append(
                OPCODE(CastI64toF32), { R_ANY(ssa_result) }, { R_ANY(ssa_arg1) });
              break;

            default:
              assert(false && "Float to integer only supports 32/64 bit");
          }
        } else {
          assert(current.result(0).type() == ir::Type::Float64);
          switch (current.source(0).type()) {
            case ir::Type::Integer32:
              block.append(
                OPCODE(CastI32toF64), { R_ANY(ssa_result) }, { R_ANY(ssa_arg1) });
              break;

            case ir::Type::Integer64:
              block.append(
                OPCODE(CastI64toF64), { R_ANY(ssa_result) }, { R_ANY(ssa_arg1) });
              break;

            default:
              assert(false && "Float to integer only supports 32/64 bit");
          }
        }
        break;
      }

      case ir::Opcode::Test: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(0));
        const jit::RtlRegister ssa_arg2 = get_rtl_ssa(current.source(1));
        switch (current.source(0).type()) {
          case ir::Type::Integer8:
            block.append(
              OPCODE(Test8), { R_ANY(ssa_result) }, { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer16:
            block.append(OPCODE(Test16),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer32:
            block.append(OPCODE(Test32),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer64:
            block.append(OPCODE(Test64),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::Compare_eq: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(0));
        const jit::RtlRegister ssa_arg2 = get_rtl_ssa(current.source(1));
        switch (current.source(0).type()) {
          case ir::Type::Integer8:
            block.append(OPCODE(CompareEq8),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer16:
            block.append(OPCODE(CompareEq16),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer32:
            block.append(OPCODE(CompareEq32),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer64:
            block.append(OPCODE(CompareEq64),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Float32:
            block.append(OPCODE(CompareEqF32),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Float64:
            block.append(OPCODE(CompareEqF64),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Bool:
            block.append(OPCODE(CompareEqBool),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::Compare_lt: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(0));
        const jit::RtlRegister ssa_arg2 = get_rtl_ssa(current.source(1));
        switch (current.source(0).type()) {
          case ir::Type::Integer8:
            block.append(OPCODE(CompareLtI8),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer16:
            block.append(OPCODE(CompareLtI16),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer32:
            block.append(OPCODE(CompareLtI32),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer64:
            block.append(OPCODE(CompareLtI64),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Float32:
            block.append(OPCODE(CompareLtF32),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Float64:
            block.append(OPCODE(CompareLtF64),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::Compare_lte: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(0));
        const jit::RtlRegister ssa_arg2 = get_rtl_ssa(current.source(1));
        switch (current.source(0).type()) {
          case ir::Type::Integer8:
            block.append(OPCODE(CompareLteI8),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer16:
            block.append(OPCODE(CompareLteI16),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer32:
            block.append(OPCODE(CompareLteI32),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer64:
            block.append(OPCODE(CompareLteI64),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Float32:
            block.append(OPCODE(CompareLteF32),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Float64:
            block.append(OPCODE(CompareLteF64),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::Compare_ult: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(0));
        const jit::RtlRegister ssa_arg2 = get_rtl_ssa(current.source(1));
        switch (current.source(0).type()) {
          case ir::Type::Integer8:
            block.append(OPCODE(CompareLtU8),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer16:
            block.append(OPCODE(CompareLtU16),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer32:
            block.append(OPCODE(CompareLtU32),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer64:
            block.append(OPCODE(CompareLtU64),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::Compare_ulte: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(0));
        const jit::RtlRegister ssa_arg2 = get_rtl_ssa(current.source(1));
        switch (current.source(0).type()) {
          case ir::Type::Integer8:
            block.append(OPCODE(CompareLteU8),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer16:
            block.append(OPCODE(CompareLteU16),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer32:
            block.append(OPCODE(CompareLteU32),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          case ir::Type::Integer64:
            block.append(OPCODE(CompareLteU64),
                         { R_ANY(ssa_result) },
                         { R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
            break;

          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::Select: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_decision = get_rtl_ssa(current.source(0));
        const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(1));
        const jit::RtlRegister ssa_arg2 = get_rtl_ssa(current.source(2));
        block.append(OPCODE(Select),
                     { R_ANY(ssa_result) },
                     { R_ANY(ssa_decision), R_ANY(ssa_arg1), R_ANY(ssa_arg2) });
        break;
      }

      case ir::Opcode::Exit: {
        /* XXX */
        assert(current.source(1).is_constant());
        assert(current.source(1).type() == ir::Type::Integer64);

        if (current.source(0).is_constant()) {
          if (!current.source(0).value().bool_value) {
            /* Exit never taken */
            break;
          }

          block.append(OPCODE(Exit), current.source(1).value(), {}, {});
        } else {
          const jit::RtlRegister ssa_decision = get_rtl_ssa(current.source(0));
          block.append(
            OPCODE(ExitIf), current.source(1).value(), {}, { R_ANY(ssa_decision) });
        }
        break;
      }

      case ir::Opcode::None: {
        continue;
      }

      default:
        assert(false && "Unhandled IR opcode");
    }
  }
}

void
Compiler::assign_registers()
{
  /* Registers R13, R14, and R15 are reserved as temporaries for moving
   * between register and spill. */
  jit::RegisterSet register_set(RegisterType, 13);
  jit::LinearAllocator register_allocator;
  register_allocator.define_register_type(register_set);

  m_rtl = register_allocator.execute(std::move(m_rtl));
}

void
Compiler::assemble()
{
  for (const jit::RtlInstruction &rtl : m_rtl.block(0)) {
    assert((m_result_size + 32u) < sizeof(m_result));

    /* Generic RTL opcode types. */
    if (rtl.op & 0x8000u) {
      switch (jit::RtlOpcode(rtl.op)) {
        case jit::RtlOpcode::Move: {
          /* Move instructions can be inserted by the register allocator to
           * preserve constraints that hit conflicts. Bytecode has no
           * constraints, so none should be generated. */
          assert(false);
          break;
        }

        case jit::RtlOpcode::None: {
          /* No-op */
          break;
        }

        default: {
          printf("Invalid RTL opcode: %u\n", (unsigned)rtl.op);
          assert(false);
        }
      }

      continue;
    }

    assert(rtl.source_count <= 3);
    assert(rtl.result_count <= 1);

    /* Bytecode instructions cannot directly access spill storage. For any
     * inputs to the bytecode instruction in spill, move them to the
     * scratch registers R13 / R14 / R15. */
    u32 sources[3];
    for (unsigned i = 0; i < rtl.source_count; ++i) {
      if (rtl.source(i).hw.is_spill()) {
        ENCODE_R1C2(Opcodes::LoadSpill, rtl.source(i).hw.spill_index(), 13 + i);
        sources[i] = 13 + i;
      } else {
        sources[i] = rtl.source(i).hw.index();
      }
    }

    /* Same for result register, but the spill instruction needs to be
     * generated after the target instruction. */
    u32 result = 0;
    if (rtl.result_count == 1) {
      if (rtl.result(0).hw.is_spill()) {
        result = 13;
      } else {
        result = rtl.result(0).hw.index();
      }
    }

    const Opcodes opcode = Opcodes(rtl.op);
    switch (opcode) {
      case Opcodes::Exit: {
        ENCODE_R0C3(opcode, rtl.get_data().u32_value);
        break;
      }

      case Opcodes::Constant8: {
        ENCODE_R1C0(Opcodes::Constant8, result);
        memcpy(&m_result[m_result_size], &rtl.get_data().u8_value, 1lu);
        m_result_size += 1lu;
        break;
      }

      case Opcodes::Constant16: {
        ENCODE_R1C0(Opcodes::Constant16, result);
        memcpy(&m_result[m_result_size], &rtl.get_data().u16_value, 2lu);
        m_result_size += 2lu;
        break;
      }

      case Opcodes::Constant32: {
        ENCODE_R1C0(Opcodes::Constant32, result);
        memcpy(&m_result[m_result_size], &rtl.get_data().u32_value, 4lu);
        m_result_size += 4lu;
        break;
      }

      case Opcodes::Constant64: {
        ENCODE_R1C0(Opcodes::Constant64, result);
        memcpy(&m_result[m_result_size], &rtl.get_data().u64_value, 8lu);
        m_result_size += 8lu;
        break;
      }

      case Opcodes::ReadRegister8:
      case Opcodes::ReadRegister16:
      case Opcodes::ReadRegister32:
      case Opcodes::ReadRegister64: {
        ENCODE_R1C2(opcode, rtl.get_data().u16_value, result);
        break;
      }

      case Opcodes::ExitIf: {
        ENCODE_R1C2(opcode, rtl.get_data().u16_value, sources[0]);
        break;
      }

      case Opcodes::WriteRegister8:
      case Opcodes::WriteRegister16:
      case Opcodes::WriteRegister32:
      case Opcodes::WriteRegister64: {
        ENCODE_R1C2(opcode, rtl.get_data().u16_value, sources[0]);
        break;
      }

      case Opcodes::Load8:
      case Opcodes::Load16:
      case Opcodes::Load32:
      case Opcodes::Load64: {
        ENCODE_R2C0(opcode, result, sources[0]);
        break;
      }

      case Opcodes::Store8:
      case Opcodes::Store16:
      case Opcodes::Store32:
      case Opcodes::Store64: {
        ENCODE_R2C0(opcode, sources[0], sources[1]);
        break;
      }

      case Opcodes::Not8:
      case Opcodes::Not16:
      case Opcodes::Not32:
      case Opcodes::Not64:
      case Opcodes::NotBool:
      case Opcodes::SquareRootF32:
      case Opcodes::SquareRootF64:
      case Opcodes::Extend8to16:
      case Opcodes::Extend8to32:
      case Opcodes::Extend8to64:
      case Opcodes::Extend16to32:
      case Opcodes::Extend16to64:
      case Opcodes::Extend32to64:
      case Opcodes::Float32to64:
      case Opcodes::Float64to32:
      case Opcodes::Cast8:
      case Opcodes::Cast16:
      case Opcodes::Cast32:
      case Opcodes::Cast64:
      case Opcodes::CastF32toI32:
      case Opcodes::CastF64toI32:
      case Opcodes::CastF32toI64:
      case Opcodes::CastF64toI64:
      case Opcodes::CastI32toF32:
      case Opcodes::CastI32toF64:
      case Opcodes::CastI64toF32:
      case Opcodes::CastI64toF64: {
        ENCODE_R2C0(opcode, result, sources[0]);
        break;
      }

      case Opcodes::RotateRight8:
      case Opcodes::RotateRight16:
      case Opcodes::RotateRight32:
      case Opcodes::RotateRight64:
      case Opcodes::RotateLeft8:
      case Opcodes::RotateLeft16:
      case Opcodes::RotateLeft32:
      case Opcodes::RotateLeft64:
      case Opcodes::ShiftRight8:
      case Opcodes::ShiftRight16:
      case Opcodes::ShiftRight32:
      case Opcodes::ShiftRight64:
      case Opcodes::ShiftLeft8:
      case Opcodes::ShiftLeft16:
      case Opcodes::ShiftLeft32:
      case Opcodes::ShiftLeft64:
      case Opcodes::ArithmeticShiftRight8:
      case Opcodes::ArithmeticShiftRight16:
      case Opcodes::ArithmeticShiftRight32:
      case Opcodes::ArithmeticShiftRight64:
      case Opcodes::And8:
      case Opcodes::And16:
      case Opcodes::And32:
      case Opcodes::And64:
      case Opcodes::AndBool:
      case Opcodes::Or8:
      case Opcodes::Or16:
      case Opcodes::Or32:
      case Opcodes::Or64:
      case Opcodes::OrBool:
      case Opcodes::Xor8:
      case Opcodes::Xor16:
      case Opcodes::Xor32:
      case Opcodes::Xor64:
      case Opcodes::AddInteger:
      case Opcodes::AddFloat32:
      case Opcodes::AddFloat64:
      case Opcodes::SubInteger8:
      case Opcodes::SubInteger16:
      case Opcodes::SubInteger32:
      case Opcodes::SubInteger64:
      case Opcodes::SubFloat32:
      case Opcodes::SubFloat64:
      case Opcodes::MultiplyI8:
      case Opcodes::MultiplyI16:
      case Opcodes::MultiplyI32:
      case Opcodes::MultiplyI64:
      case Opcodes::MultiplyF32:
      case Opcodes::MultiplyF64:
      case Opcodes::MultiplyU8:
      case Opcodes::MultiplyU16:
      case Opcodes::MultiplyU32:
      case Opcodes::MultiplyU64:
      case Opcodes::DivideI8:
      case Opcodes::DivideI16:
      case Opcodes::DivideI32:
      case Opcodes::DivideI64:
      case Opcodes::DivideU8:
      case Opcodes::DivideU16:
      case Opcodes::DivideU32:
      case Opcodes::DivideU64:
      case Opcodes::DivideF32:
      case Opcodes::DivideF64:
      case Opcodes::Test8:
      case Opcodes::Test16:
      case Opcodes::Test32:
      case Opcodes::Test64:
      case Opcodes::CompareEq8:
      case Opcodes::CompareEq16:
      case Opcodes::CompareEq32:
      case Opcodes::CompareEq64:
      case Opcodes::CompareEqF32:
      case Opcodes::CompareEqF64:
      case Opcodes::CompareEqBool:
      case Opcodes::CompareLtI8:
      case Opcodes::CompareLtI16:
      case Opcodes::CompareLtI32:
      case Opcodes::CompareLtI64:
      case Opcodes::CompareLtU8:
      case Opcodes::CompareLtU16:
      case Opcodes::CompareLtU32:
      case Opcodes::CompareLtU64:
      case Opcodes::CompareLtF32:
      case Opcodes::CompareLtF64:
      case Opcodes::CompareLteI8:
      case Opcodes::CompareLteI16:
      case Opcodes::CompareLteI32:
      case Opcodes::CompareLteI64:
      case Opcodes::CompareLteU8:
      case Opcodes::CompareLteU16:
      case Opcodes::CompareLteU32:
      case Opcodes::CompareLteU64:
      case Opcodes::CompareLteF32:
      case Opcodes::CompareLteF64: {
        ENCODE_R3C0(opcode, result, sources[0], sources[1]);
        break;
      }

      case Opcodes::BitSetClear8:
      case Opcodes::BitSetClear16:
      case Opcodes::BitSetClear32:
      case Opcodes::BitSetClear64: {
        ENCODE_R3C1(opcode, rtl.get_data().u8_value, result, sources[0], sources[1]);
        break;
      }

      case Opcodes::Select: {
        ENCODE_R4C0(opcode, result, sources[0], sources[1], sources[2]);
        break;
      }

      case Opcodes::HostVoidCall0: {
        ENCODE_R1C0(Opcodes::HostVoidCall0, sources[0]);
        break;
      }

      case Opcodes::HostCall0: {
        ENCODE_R2C0(Opcodes::HostCall0, result, sources[0]);
        break;
      }

      case Opcodes::HostCall1: {
        ENCODE_R3C0(Opcodes::HostCall1, result, sources[0], sources[1]);
        break;
      }

      case Opcodes::HostCall2: {
        ENCODE_R4C0(Opcodes::HostCall2, result, sources[0], sources[1], sources[2]);
        break;
      }

      default:
        assert(false && "Unhandled RTL opcode");
    }

    /* If the result was assigned to a spill location, move the scratch
     * register result into spill. */
    if (rtl.result_count == 1 && rtl.result(0).hw.is_spill()) {
      ENCODE_R1C2(Opcodes::StoreSpill, rtl.result(0).hw.spill_index(), result);
    }
  }
}

/* TODO Smarter allocation, re-use of constants when possible, reduce bitwidth
 *      for smaller values. */
jit::RtlRegister
Compiler::get_rtl_ssa(const ir::Operand operand)
{
  if (operand.is_register()) {
    assert(m_ir_to_rtl.size() > operand.register_index());
    assert(m_ir_to_rtl[operand.register_index()].valid());
    return m_ir_to_rtl[operand.register_index()];
  }

  jit::RtlRegister ssa_constant;
  jit::RtlInstructions &block = m_rtl.block(0); /* XXX */
  switch (operand.type()) {
    case ir::Type::Integer8: {
      const u64 value = operand.value().u8_value;
      ssa_constant = m_rtl.ssa_allocate(0);
      block.append(OPCODE(Constant8),
                   jit::Value { .u64_value = value },
                   { R_ANY(ssa_constant) },
                   {});
      break;
    }

    case ir::Type::Integer16: {
      const u64 value = operand.value().u16_value;
      ssa_constant = m_rtl.ssa_allocate(0);
      block.append(OPCODE(Constant16),
                   jit::Value { .u64_value = value },
                   { R_ANY(ssa_constant) },
                   {});
      break;
    }

    case ir::Type::Integer32:
    case ir::Type::Float32: {
      const u64 value = operand.value().u32_value;
      ssa_constant = m_rtl.ssa_allocate(0);
      block.append(OPCODE(Constant32),
                   jit::Value { .u64_value = value },
                   { R_ANY(ssa_constant) },
                   {});
      break;
    }

    case ir::Type::Integer64:
    case ir::Type::Float64: {
      const u64 value = operand.value().u64_value;
      ssa_constant = m_rtl.ssa_allocate(0);
      block.append(OPCODE(Constant64),
                   jit::Value { .u64_value = value },
                   { R_ANY(ssa_constant) },
                   {});
      break;
    }

    case ir::Type::HostAddress: {
      static_assert(sizeof(void *) == sizeof(u64));
      const u64 value = operand.value().u64_value;
      ssa_constant = m_rtl.ssa_allocate(0);
      block.append(OPCODE(Constant64),
                   jit::Value { .u64_value = value },
                   { R_ANY(ssa_constant) },
                   {});
      break;
    }

    case ir::Type::Bool: {
      const u64 value = operand.value().bool_value ? 1 : 0;
      ssa_constant = m_rtl.ssa_allocate(0);
      block.append(OPCODE(Constant8),
                   jit::Value { .u64_value = value },
                   { R_ANY(ssa_constant) },
                   {});
      break;
    }

    default:
      assert(false);
  }

  return ssa_constant;
}

jit::RtlRegister
Compiler::make_rtl_ssa(const ir::Operand operand)
{
  assert(operand.is_register());

  const unsigned index = operand.register_index();
  assert(index >= m_ir_to_rtl.size() || !m_ir_to_rtl[index].valid());
  if (index >= m_ir_to_rtl.size()) {
    m_ir_to_rtl.resize(index + 1u, jit::RtlRegister());
  }

  m_ir_to_rtl[index] = m_rtl.ssa_allocate(0);
  return m_ir_to_rtl[index];
}

const char *
Compiler::rtl_opcode_names(const u16 opcode)
{
  switch (Opcodes(opcode)) {
    case Opcodes::Constant8:
      return "imm8";
    case Opcodes::Constant16:
      return "imm16";
    case Opcodes::Constant32:
      return "imm32";
    case Opcodes::Constant64:
      return "imm64";
    case Opcodes::ExtendConstant8:
      return "imm8e";
    case Opcodes::ExtendConstant16:
      return "imm16e";
    case Opcodes::ExtendConstant32:
      return "imm32e";
    case Opcodes::ReadRegister8:
      return "readgr8";
    case Opcodes::ReadRegister16:
      return "readgr16";
    case Opcodes::ReadRegister32:
      return "readgr32";
    case Opcodes::ReadRegister64:
      return "readgr64";
    case Opcodes::WriteRegister8:
      return "writegr8";
    case Opcodes::WriteRegister16:
      return "writegr16";
    case Opcodes::WriteRegister32:
      return "writegr32";
    case Opcodes::WriteRegister64:
      return "writegr64";
    case Opcodes::Load8:
      return "load8";
    case Opcodes::Load16:
      return "load16";
    case Opcodes::Load32:
      return "load32";
    case Opcodes::Load64:
      return "load64";
    case Opcodes::Store8:
      return "store8";
    case Opcodes::Store16:
      return "store16";
    case Opcodes::Store32:
      return "store32";
    case Opcodes::Store64:
      return "store64";
    case Opcodes::RotateRight8:
      return "rotr8";
    case Opcodes::RotateRight16:
      return "rotr16";
    case Opcodes::RotateRight32:
      return "rotr32";
    case Opcodes::RotateRight64:
      return "rotr64";
    case Opcodes::RotateLeft8:
      return "rotl8";
    case Opcodes::RotateLeft16:
      return "rotl16";
    case Opcodes::RotateLeft32:
      return "rotl32";
    case Opcodes::RotateLeft64:
      return "rotl64";
    case Opcodes::ShiftRight8:
      return "shiftr8";
    case Opcodes::ShiftRight16:
      return "shiftr16";
    case Opcodes::ShiftRight32:
      return "shiftr32";
    case Opcodes::ShiftRight64:
      return "shiftr64";
    case Opcodes::ShiftLeft8:
      return "shiftl8";
    case Opcodes::ShiftLeft16:
      return "shiftl16";
    case Opcodes::ShiftLeft32:
      return "shiftl32";
    case Opcodes::ShiftLeft64:
      return "shiftl64";
    case Opcodes::And8:
      return "and8";
    case Opcodes::And16:
      return "and16";
    case Opcodes::And32:
      return "and32";
    case Opcodes::And64:
      return "and64";
    case Opcodes::AndBool:
      return "andb";
    case Opcodes::Or8:
      return "or8";
    case Opcodes::Or16:
      return "or16";
    case Opcodes::Or32:
      return "or32";
    case Opcodes::Or64:
      return "or64";
    case Opcodes::Xor8:
      return "xor8";
    case Opcodes::Xor16:
      return "xor16";
    case Opcodes::Xor32:
      return "xor32";
    case Opcodes::Xor64:
      return "xor64";
    case Opcodes::Not8:
      return "not8";
    case Opcodes::Not16:
      return "not16";
    case Opcodes::Not32:
      return "not32";
    case Opcodes::Not64:
      return "not64";
    case Opcodes::NotBool:
      return "notb";
    case Opcodes::BitSetClear8:
      return "bsc8";
    case Opcodes::BitSetClear16:
      return "bsc16";
    case Opcodes::BitSetClear32:
      return "bsc32";
    case Opcodes::BitSetClear64:
      return "bsc64";
    case Opcodes::AddInteger:
      return "add";
    case Opcodes::AddFloat32:
      return "addf32";
    case Opcodes::AddFloat64:
      return "addf64";
    case Opcodes::SubInteger8:
      return "sub8";
    case Opcodes::SubInteger16:
      return "sub16";
    case Opcodes::SubInteger32:
      return "sub32";
    case Opcodes::SubInteger64:
      return "sub64";
    case Opcodes::SubFloat32:
      return "subf32";
    case Opcodes::SubFloat64:
      return "subf64";
    case Opcodes::MultiplyI8:
      return "muls8";
    case Opcodes::MultiplyI16:
      return "muls16";
    case Opcodes::MultiplyI32:
      return "muls32";
    case Opcodes::MultiplyI64:
      return "muls64";
    case Opcodes::MultiplyF32:
      return "mulf32";
    case Opcodes::MultiplyF64:
      return "mulf64";
    case Opcodes::MultiplyU8:
      return "mulu8";
    case Opcodes::MultiplyU16:
      return "mulu16";
    case Opcodes::MultiplyU32:
      return "mulu32";
    case Opcodes::MultiplyU64:
      return "mulu64";
    case Opcodes::DivideI8:
      return "divs8";
    case Opcodes::DivideI16:
      return "divs16";
    case Opcodes::DivideI32:
      return "divs32";
    case Opcodes::DivideI64:
      return "divs64";
    case Opcodes::DivideU8:
      return "divu8";
    case Opcodes::DivideU16:
      return "divu16";
    case Opcodes::DivideU32:
      return "divu32";
    case Opcodes::DivideU64:
      return "divu64";
    case Opcodes::DivideF32:
      return "divf32";
    case Opcodes::DivideF64:
      return "divf64";
    case Opcodes::SquareRootF32:
      return "sqrtf32";
    case Opcodes::SquareRootF64:
      return "sqrtf64";
    case Opcodes::Extend8to16:
      return "se8to16";
    case Opcodes::Extend8to32:
      return "se8to32";
    case Opcodes::Extend8to64:
      return "se8to64";
    case Opcodes::Extend16to32:
      return "se16to32";
    case Opcodes::Extend16to64:
      return "se16to64";
    case Opcodes::Extend32to64:
      return "se32to64";
    case Opcodes::Float32to64:
      return "f32to64";
    case Opcodes::Float64to32:
      return "f64to32";
    case Opcodes::Cast8:
      return "cast8";
    case Opcodes::Cast16:
      return "cast16";
    case Opcodes::Cast32:
      return "cast32";
    case Opcodes::Cast64:
      return "cast64";
    case Opcodes::CastF32toI32:
      return "f32toi32";
    case Opcodes::CastF64toI32:
      return "f64toi32";
    case Opcodes::CastF32toI64:
      return "f32toi64";
    case Opcodes::CastF64toI64:
      return "f64toi64";
    case Opcodes::CastI32toF32:
      return "i32tof32";
    case Opcodes::CastI32toF64:
      return "i32tof64";
    case Opcodes::CastI64toF32:
      return "i64tof32";
    case Opcodes::CastI64toF64:
      return "i64tof64";
    case Opcodes::Test8:
      return "test8";
    case Opcodes::Test16:
      return "test16";
    case Opcodes::Test32:
      return "test32";
    case Opcodes::Test64:
      return "test64";
    case Opcodes::CompareEq8:
      return "cmpeq8";
    case Opcodes::CompareEq16:
      return "cmpeq16";
    case Opcodes::CompareEq32:
      return "cmpeq32";
    case Opcodes::CompareEq64:
      return "cmpeq64";
    case Opcodes::CompareEqF32:
      return "cmpeq32f";
    case Opcodes::CompareEqF64:
      return "cmpeq64f";
    case Opcodes::CompareEqBool:
      return "cmpeqb";
    case Opcodes::CompareLtI8:
      return "cmplt8s";
    case Opcodes::CompareLtI16:
      return "cmplt16s";
    case Opcodes::CompareLtI32:
      return "cmplt32s";
    case Opcodes::CompareLtI64:
      return "cmplt64s";
    case Opcodes::CompareLtU8:
      return "cmplt8u";
    case Opcodes::CompareLtU16:
      return "cmplt16u";
    case Opcodes::CompareLtU32:
      return "cmplt32u";
    case Opcodes::CompareLtU64:
      return "cmplt64u";
    case Opcodes::CompareLtF32:
      return "cmplt32f";
    case Opcodes::CompareLtF64:
      return "cmplt64f";
    case Opcodes::CompareLteI8:
      return "cmplte8s";
    case Opcodes::CompareLteI16:
      return "cmplte16s";
    case Opcodes::CompareLteI32:
      return "cmplte32s";
    case Opcodes::CompareLteI64:
      return "cmplte64s";
    case Opcodes::CompareLteU8:
      return "cmplte8u";
    case Opcodes::CompareLteU16:
      return "cmplte16u";
    case Opcodes::CompareLteU32:
      return "cmplte32u";
    case Opcodes::CompareLteU64:
      return "cmplte64u";
    case Opcodes::CompareLteF32:
      return "cmplte32f";
    case Opcodes::CompareLteF64:
      return "cmplte64f";
    case Opcodes::Select:
      return "select";
    case Opcodes::Exit:
      return "exit";
    case Opcodes::ExitIf:
      return "exitif";
    case Opcodes::HostVoidCall0:
      return "call";
    case Opcodes::HostCall0:
      return "call";
    case Opcodes::HostCall1:
      return "call";
    case Opcodes::HostCall2:
      return "call";
    case Opcodes::LoadSpill:
      return "rspill";
    case Opcodes::StoreSpill:
      return "wspill";
    default:
      return "OPCODE";
  }
}

}
}
