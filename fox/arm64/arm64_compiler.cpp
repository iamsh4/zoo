#include <fmt/core.h>
#include <unordered_map>
#include <set>
#include <cstring>
#include <sstream>

#include "fox/jit/linear_register_allocator.h"
#include "arm64/arm64_compiler.h"
#include "arm64/arm64_assembler.h"
#include "arm64/arm64_opcode.h"
#include "arm64/arm64_routine.h"
#include "arm64/arm64_logical_immediates.h"

namespace fox {
namespace codegen {
namespace arm64 {

LogicalImmediates logical_immediates;

static constexpr jit::HwRegister::Type SpillType  = jit::HwRegister::Type(0);
static constexpr jit::HwRegister::Type ScalarType = jit::HwRegister::Type(1);
static constexpr jit::HwRegister::Type VectorType = jit::HwRegister::Type(2);

// https://developer.apple.com/documentation/xcode/writing_arm64_code_for_apple_platforms

template<typename Result, typename... T>
constexpr Result
make_bits(T... indices)
{
  // TODO : assert all indices in range of number of bits of T
  return (Result(0) | ... | (Result(1) << indices));
}

static constexpr uint32_t abi_caller_saved =
  make_bits<uint32_t>(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17);
static constexpr uint32_t abi_callee_saved =
  make_bits<uint32_t>(19, 20, 21, 22, 23, 24, 25, 26, 27, 28);
// clang-format on

extern "C" {
void wrap_store(Guest *const guest,
                const u32 address,
                const size_t bytes,
                const ir::Constant value);

ir::Constant wrap_load(Guest *const guest, const u32 address, const size_t bytes);
}

template<typename T>
u64
make_constant(const T raw)
{
  static_assert(sizeof(T) <= sizeof(u64));
  u64 result = 0;
  memcpy(&result, &raw, sizeof(T));
  return result;
}

template<typename T>
T
get_constant(const u64 raw)
{
  static_assert(sizeof(T) <= sizeof(u64));
  T result;
  memcpy(&result, &raw, sizeof(T));
  return result;
}

u64
ir_type_to_bytes(ir::Type type)
{
  switch (type) {
    case ir::Type::Integer8:
      return 1;

    case ir::Type::Integer16:
      return 2;

    case ir::Type::Integer32:
    case ir::Type::Float32:
      return 4;

    case ir::Type::Integer64:
    case ir::Type::Float64:
      return 8;

    default:
      assert(false);
      return 0;
  }
}

#define HW_ANY(ssa)                                                                      \
  jit::RegisterAssignment                                                                \
  {                                                                                      \
    ssa, jit::HwRegister(ScalarType)                                                     \
  }
#define HW_AT(ssa, hw)                                                                   \
  jit::RegisterAssignment                                                                \
  {                                                                                      \
    ssa, jit::HwRegister(ScalarType, hw)                                                 \
  }
#define HW_X(hw)                                                                         \
  jit::RegisterAssignment                                                                \
  {                                                                                      \
    jit::RtlRegister(), jit::HwRegister(ScalarType, hw)                                  \
  }
#define VEC_ANY(ssa)                                                                     \
  jit::RegisterAssignment                                                                \
  {                                                                                      \
    ssa, jit::HwRegister(VectorType)                                                     \
  }

/*
 * RTL_ENCODE_{[0-3]}{R/N}:
 *     Encodes RTL with 0-3 parameters and either a result (R) or no result
 *     (N).
 */

#define RTL_ENCODE_0N(opcode, details)                                                   \
  do {                                                                                   \
    jit::RtlInstruction entry(0u, 0u);                                                   \
    entry.op   = (u16)opcode;                                                            \
    entry.data = details;                                                                \
    m_rtl.block(0).push_back(entry);                                                     \
  } while (0);

#define RTL_ENCODE_0R(opcode, details, out)                                              \
  do {                                                                                   \
    jit::RtlInstruction entry(0u, 1u);                                                   \
    entry.op        = (u16)opcode;                                                       \
    entry.data      = details;                                                           \
    entry.result(0) = out;                                                               \
    m_rtl.block(0).push_back(entry);                                                     \
  } while (0);

#define RTL_ENCODE_1N(opcode, details, in1)                                              \
  do {                                                                                   \
    jit::RtlInstruction entry(1u, 0u);                                                   \
    entry.op        = (u16)opcode;                                                       \
    entry.data      = details;                                                           \
    entry.source(0) = in1;                                                               \
    m_rtl.block(0).push_back(entry);                                                     \
  } while (0);

#define RTL_ENCODE_1R(opcode, details, out, in1)                                         \
  do {                                                                                   \
    jit::RtlInstruction entry(1u, 1u);                                                   \
    entry.op        = (u16)opcode;                                                       \
    entry.data      = details;                                                           \
    entry.result(0) = out;                                                               \
    entry.source(0) = in1;                                                               \
    m_rtl.block(0).push_back(entry);                                                     \
  } while (0);

#define RTL_ENCODE_1RS(opcode, details, out, in1)                                        \
  do {                                                                                   \
    jit::RtlInstruction entry(1u, 1u, { jit::RtlFlag::SaveState });                      \
    entry.op        = (u16)opcode;                                                       \
    entry.data      = details;                                                           \
    entry.result(0) = out;                                                               \
    entry.source(0) = in1;                                                               \
    m_rtl.block(0).push_back(entry);                                                     \
  } while (0);

#define RTL_ENCODE_2N(opcode, details, in1, in2)                                         \
  do {                                                                                   \
    jit::RtlInstruction entry(2u, 0u);                                                   \
    entry.op        = (u16)opcode;                                                       \
    entry.data      = details;                                                           \
    entry.source(0) = in1;                                                               \
    entry.source(1) = in2;                                                               \
    m_rtl.block(0).push_back(entry);                                                     \
  } while (0);

#define RTL_ENCODE_2R(opcode, details, out, in1, in2)                                    \
  do {                                                                                   \
    jit::RtlInstruction entry(2u, 1u, { jit::RtlFlag::Destructive });                    \
    entry.op        = (u16)opcode;                                                       \
    entry.data      = details;                                                           \
    entry.result(0) = out;                                                               \
    entry.source(0) = in1;                                                               \
    entry.source(1) = in2;                                                               \
    m_rtl.block(0).push_back(entry);                                                     \
  } while (0);

#define RTL_ENCODE_2RS(opcode, details, out, in1, in2)                                   \
  do {                                                                                   \
    jit::RtlInstruction entry(                                                           \
      2u, 1u, { jit::RtlFlag::Destructive, jit::RtlFlag::SaveState });                   \
    entry.op        = (u16)opcode;                                                       \
    entry.data      = details;                                                           \
    entry.result(0) = out;                                                               \
    entry.source(0) = in1;                                                               \
    entry.source(1) = in2;                                                               \
    m_rtl.block(0).push_back(entry);                                                     \
  } while (0);

#define RTL_ENCODE_3N(opcode, details, in1, in2, in3)                                    \
  do {                                                                                   \
    jit::RtlInstruction entry(3u, 0u);                                                   \
    entry.op        = (u16)opcode;                                                       \
    entry.data      = details;                                                           \
    entry.source(0) = in1;                                                               \
    entry.source(1) = in2;                                                               \
    entry.source(2) = in3;                                                               \
    m_rtl.block(0).push_back(entry);                                                     \
  } while (0);

#define RTL_ENCODE_3R(opcode, details, out, in1, in2, in3)                               \
  do {                                                                                   \
    jit::RtlInstruction entry(3u, 1u, { jit::RtlFlag::Destructive });                    \
    entry.op        = (u16)opcode;                                                       \
    entry.data      = details;                                                           \
    entry.result(0) = out;                                                               \
    entry.source(0) = in1;                                                               \
    entry.source(1) = in2;                                                               \
    entry.source(2) = in3;                                                               \
    m_rtl.block(0).push_back(entry);                                                     \
  } while (0);

#define RTL_ENCODE_3RS(opcode, details, out, in1, in2, in3)                              \
  do {                                                                                   \
    jit::RtlInstruction entry(                                                           \
      3u, 1u, { jit::RtlFlag::Destructive, jit::RtlFlag::SaveState });                   \
    entry.op        = (u16)opcode;                                                       \
    entry.data      = details;                                                           \
    entry.result(0) = out;                                                               \
    entry.source(0) = in1;                                                               \
    entry.source(1) = in2;                                                               \
    entry.source(2) = in3;                                                               \
    m_rtl.block(0).push_back(entry);                                                     \
  } while (0);

#define RTL_ENCODE_4NS(opcode, details, in1, in2, in3, in4)                              \
  do {                                                                                   \
    jit::RtlInstruction entry(4u, 0u, { jit::RtlFlag::SaveState });                      \
    entry.op        = (u16)opcode;                                                       \
    entry.data      = details;                                                           \
    entry.source(0) = in1;                                                               \
    entry.source(1) = in2;                                                               \
    entry.source(2) = in3;                                                               \
    entry.source(3) = in4;                                                               \
    m_rtl.block(0).push_back(entry);                                                     \
  } while (0);

#define RTL_ENCODE_4RS(opcode, details, out, in1, in2, in3, in4)                         \
  do {                                                                                   \
    jit::RtlInstruction entry(                                                           \
      4u, 1u, { jit::RtlFlag::Destructive, jit::RtlFlag::SaveState });                   \
    entry.op        = (u16)opcode;                                                       \
    entry.data      = details;                                                           \
    entry.result(0) = out;                                                               \
    entry.source(0) = in1;                                                               \
    entry.source(1) = in2;                                                               \
    entry.source(2) = in3;                                                               \
    entry.source(3) = in4;                                                               \
    m_rtl.block(0).push_back(entry);                                                     \
  } while (0);

std::unique_ptr<codegen::arm64::Routine>
Compiler::compile(ir::ExecutionUnit &&source)
{
  m_source = std::move(source);
  m_ir_to_rtl.clear();
  m_uses_memory = false;
  m_debug       = false;

#ifdef JIT_DEBUG
  printf("-------------------------\n");
  printf("%s\n", m_source.disassemble().c_str());
#endif

  generate_rtl();

  assign_registers();
  assemble();

  if (m_debug) {
    printf("====================================================\n");
    m_routine->debug_print();
    printf("====================================================\n");
  }

  return std::move(m_routine);
}

void
Compiler::assign_registers()
{
  // There are 32 total registers, x0-x31
  jit::RegisterSet scalar_set(ScalarType, 32);

  // x31 == SP and may not used.
  scalar_set.mark_allocated(jit::HwRegister(ScalarType, 31));

  // x30 is for tracking frame pointers. We respect this, and setup frame pointers
  scalar_set.mark_allocated(jit::HwRegister(ScalarType, 30));

  // There are a set of callee-saved registers. For simplicity, we won't allow the
  // allocator to touch them.
  for (u64 xi = 0; xi < 32; ++xi) {
    if ((abi_callee_saved >> xi) & 1) {
      scalar_set.mark_allocated(jit::HwRegister(ScalarType, xi));
    }
  }

  // x18 is platform specific and Apple says don't touch.
  // https://developer.apple.com/documentation/xcode/writing_arm64_code_for_apple_platforms
  scalar_set.mark_allocated(jit::HwRegister(ScalarType, 18));

  // x0-x2 are kept handy because their contents are frequently used (these all the
  // function arguments)
  scalar_set.mark_allocated(jit::HwRegister(ScalarType, 0));

  // NEW CHANGE, used to be 1-2, now it's 9,10
  scalar_set.mark_allocated(jit::HwRegister(ScalarType, 9));
  scalar_set.mark_allocated(jit::HwRegister(ScalarType, 10));

  // TODO : SIMD/FPU (V0-V31)
  jit::RegisterSet vector_set(VectorType, 32);

  jit::LinearAllocator allocator;
  allocator.define_register_type(scalar_set);
  allocator.define_register_type(vector_set);
  m_rtl = allocator.execute(std::move(m_rtl));
}

void
Compiler::assemble()
{
  std::set<u32> unhandled_rtl_opcodes;

  Assembler assembler;
  auto exit_label = assembler.create_label();

  auto X   = Registers::X;
  auto W   = Registers::W;
  auto S   = Registers::S;
  auto D   = Registers::D;
  auto SP  = X(31);
  auto Wzr = W(31);
  auto Xzr = X(31);

  std::unordered_map<u32, Assembler::Label> rtl_to_assembler_label;

  // auto guest_pointer_reg = X(0);

  // TODO : AMD64 does some stuff with moves based on what happened in the allocator

  // ------------------------------------------------
  // Function prologue

  auto guest_memory_base_pointer_reg    = X(9);
  auto guest_registers_base_pointer_reg = X(10);

  assembler.STP_pre(X(29), X(30), SP, -16); // Save FP+LR for stack unwinding
  assembler.ADD(guest_memory_base_pointer_reg, X(1), 0);
  assembler.ADD(guest_registers_base_pointer_reg, X(2), 0);

  // ------------------------------------------------
  // RTL -> Assembly with assigned registers

  // Set by opcodes to abort() when a routine is compiled which uses particular opcode(s)
  bool dump_and_die = false;

  for (const auto &rtl : m_rtl.block(0)) {
    for (int i = 0; i < rtl.result_count; ++i) {
      if (rtl.result(i).hw.type() == SpillType) {
        throw std::invalid_argument("Spill registers not implemented");
      }
    }

    for (int i = 0; i < rtl.source_count; ++i) {
      if (rtl.source(i).hw.type() == SpillType) {
        throw std::invalid_argument("Spill registers not implemented");
      }
    }

    // Special reg -> reg RTL Opcode emitted by the register allocator
    if (rtl.op & 0x8000u) {
      switch (jit::RtlOpcode(rtl.op)) {
        case jit::RtlOpcode::Move: {
          /* Move instructions can be inserted by the register allocator to
           * preserve constraints that hit conflicts. */
          assembler.ADD(X(rtl.result(0).hw.index()), X(rtl.source(0).hw.index()), 0);
          break;
        }

        case jit::RtlOpcode::None: {
          /* No-op */
          break;
        }

        default: {
          printf("Invalid jit RTL opcode: %u\n", (unsigned)rtl.op);
          assert(false);
        }
      }

      continue;
    }

    const Opcode opcode = (Opcode)rtl.op;
    switch (opcode) {
      case Opcode::PUSH_GPRS: {
        for (int xi = 0; xi <= 31; ++xi) {
          if ((rtl.data >> xi) & 1) {
            assembler.STR_pre(X(xi), SP, -16);
          }
        }
        break;
      }

      case Opcode::POP_GPRS: {
        for (int xi = 31; xi >= 0; --xi) {
          if ((rtl.data >> xi) & 1) {
            assembler.LDR_post(X(xi), SP, 16);
          }
        }
        break;
      }

      case Opcode::LABEL: {
        rtl_to_assembler_label[rtl.data] = assembler.create_label();
        break;
      }

      case Opcode::LOAD_IMM32: {
        assert(rtl.result(0).hw.assigned());
        assert(!(rtl.data & 0xFFFFFFFF00000000));

        if (rtl.result(0).hw.type() == ScalarType) {
          const auto dest = W(rtl.result(0).hw.index());
          assembler.MOVZ(dest, rtl.data & 0xFFFF, 0);
          if (rtl.data & (~0xFFFF)) {
            assembler.MOVK(dest, (rtl.data >> 16) & 0xFFFF, 16);
          }
        } else if (rtl.result(0).hw.type() == VectorType) {
          const auto Sdest          = S(rtl.result(0).hw.index());
          u32 constant_bits         = get_constant<u32>(rtl.data);
          const auto constant_label = assembler.create_constant(constant_bits);
          assembler.LDR(Sdest, constant_label);
          assert(false);
        } else {
          throw std::invalid_argument("Unhandled LOAD_IMM32 type");
        }
        break;
      }

      case Opcode::LOAD_IMM64: {
        assert(rtl.result(0).hw.assigned());

        if (rtl.result(0).hw.type() == ScalarType) {
          const auto dest = X(rtl.result(0).hw.index());
          assembler.MOVZ(dest, rtl.data & 0xFFFF, 0);
          if (rtl.data >= 0x10000) {
            assembler.MOVK(dest, (rtl.data >> 16) & 0xFFFF, 16);

            if (rtl.data >= 0x100000000) {
              assembler.MOVK(dest, (rtl.data >> 32) & 0xFFFF, 32);

              if (rtl.data >= 0x1000000000000) {
                assembler.MOVK(dest, (rtl.data >> 48) & 0xFFFF, 48);
              }
            }
          }
        } else if (rtl.result(0).hw.type() == VectorType) {
          throw std::invalid_argument("Unhandled LOAD_IMM64 float");
        } else {
          throw std::invalid_argument("Unhandled LOAD_IMM32 type");
        }
        break;
      }

      case Opcode::READ_GUEST_REGISTER32: {
        assert(rtl.result(0).hw.assigned());
        if (rtl.result(0).hw.type() == ScalarType) {
          auto Wdest   = W(rtl.result(0).hw.index());
          auto reg_src = m_register_address_cb(rtl.data & 0xFFFF);
          assembler.LDR(Wdest, guest_registers_base_pointer_reg, reg_src * 4);
        } else if (rtl.result(0).hw.type() == VectorType) {
          auto Sdest   = S(rtl.result(0).hw.index());
          auto reg_src = m_register_address_cb(rtl.data & 0xFFFF);
          assembler.LDR(Sdest, guest_registers_base_pointer_reg, reg_src * 4);
        } else {
          assert(false && "Impossible READ_GUEST_REGISTER32 type");
        }
        break;
      }

      case Opcode::READ_GUEST_REGISTER64: {
        assert(rtl.result(0).hw.assigned());
        // TODO : Are these memory offsets correct?
        if (rtl.result(0).hw.type() == ScalarType) {
          auto dest    = X(rtl.result(0).hw.index());
          auto reg_src = m_register_address_cb(rtl.data & 0xFFFF);
          assembler.LDR(dest, guest_registers_base_pointer_reg, reg_src * 4);
        } else if (rtl.result(0).hw.type() == VectorType) {
          auto dest    = D(rtl.result(0).hw.index());
          auto reg_src = m_register_address_cb(rtl.data & 0xFFFF);
          assembler.LDR(dest, guest_registers_base_pointer_reg, reg_src * 4);
        } else {
          assert(false && "Impossible READ_GUEST_REGISTER64 type");
        }
        break;
      }

      case Opcode::WRITE_GUEST_REGISTER32: {
        assert(rtl.source(0).hw.assigned());
        if (rtl.source(0).hw.type() == ScalarType) {
          auto Wsrc    = W(rtl.source(0).hw.index());
          auto reg_dst = m_register_address_cb(rtl.data & 0xFFFF);
          assembler.STR(Wsrc, guest_registers_base_pointer_reg, reg_dst * 4);
        } else if (rtl.source(0).hw.type() == VectorType) {
          auto Ssrc    = S(rtl.source(0).hw.index());
          auto reg_dst = m_register_address_cb(rtl.data & 0xFFFF);
          assembler.STR(Ssrc, guest_registers_base_pointer_reg, reg_dst * 4);
        } else {
          assert(false && "Impossible WRITE_GUEST_REGISTER32 type");
        }
        break;
      }

      case Opcode::WRITE_GUEST_REGISTER64: {
        assert(rtl.source(0).hw.assigned());
        if (rtl.source(0).hw.type() == ScalarType) {
          auto src     = X(rtl.source(0).hw.index());
          auto reg_dst = m_register_address_cb(rtl.data & 0xFFFF);
          assembler.STR(src, guest_registers_base_pointer_reg, reg_dst * 4);
        } else if (rtl.source(0).hw.type() == VectorType) {
          auto src     = D(rtl.source(0).hw.index());
          auto reg_dst = m_register_address_cb(rtl.data & 0xFFFF);
          assembler.STR(src, guest_registers_base_pointer_reg, reg_dst * 4);
        } else {
          assert(false && "Impossible WRITE_GUEST_REGISTER64 type");
        }
        break;
      }

      case Opcode::FMOV32: {
        if ((rtl.result(0).hw.type() == ScalarType) &&
            (rtl.source(0).hw.type() == VectorType)) {
          const auto Wdest = W(rtl.result(0).hw.index());
          const auto Ssrc  = S(rtl.source(0).hw.index());
          assembler.FMOV(Wdest, Ssrc);
        } else if ((rtl.result(0).hw.type() == VectorType) &&
                   (rtl.source(0).hw.type() == ScalarType)) {
          const auto Sdest = S(rtl.result(0).hw.index());
          const auto Wsrc  = W(rtl.source(0).hw.index());
          assembler.FMOV(Sdest, Wsrc);
        } else {
          throw std::invalid_argument("FMOV32, invalid register type pair.");
        }
        break;
      }

      case Opcode::FMOV64: {
        if ((rtl.result(0).hw.type() == ScalarType) &&
            (rtl.source(0).hw.type() == VectorType)) {
          const auto Xdest = X(rtl.result(0).hw.index());
          const auto Dsrc  = D(rtl.source(0).hw.index());
          assembler.FMOV(Xdest, Dsrc);
        } else if ((rtl.result(0).hw.type() == VectorType) &&
                   (rtl.source(0).hw.type() == ScalarType)) {
          const auto Ddest = D(rtl.result(0).hw.index());
          const auto Xsrc  = X(rtl.source(0).hw.index());
          assembler.FMOV(Ddest, Xsrc);
        } else {
          assert(false);
        }
        break;
      }

      case Opcode::ADD_32: {
        assert(rtl.source(0).hw.assigned());
        assert(rtl.source(1).hw.assigned());
        assert(rtl.result(0).hw.assigned());

        if (rtl.result(0).hw.type() == ScalarType) {
          auto Wdst  = W(rtl.result(0).hw.index());
          auto Wsrc1 = W(rtl.source(0).hw.index());
          auto Wsrc2 = W(rtl.source(1).hw.index());
          assembler.ADD(Wdst, Wsrc1, Wsrc2);
        } else if (rtl.result(0).hw.type() == VectorType) {
          auto Sdst  = S(rtl.result(0).hw.index());
          auto Ssrc1 = S(rtl.source(0).hw.index());
          auto Ssrc2 = S(rtl.source(1).hw.index());
          assembler.FADD(Sdst, Ssrc1, Ssrc2);
        }
        break;
      }

      case Opcode::ADD_64: {
        assert(rtl.source(0).hw.assigned());
        assert(rtl.source(1).hw.assigned());
        assert(rtl.result(0).hw.assigned());

        if (rtl.result(0).hw.type() == ScalarType) {
          auto Xdst  = X(rtl.result(0).hw.index());
          auto Xsrc1 = X(rtl.source(0).hw.index());
          auto Xsrc2 = X(rtl.source(1).hw.index());
          assembler.ADD(Xdst, Xsrc1, Xsrc2);
        } else if (rtl.result(0).hw.type() == VectorType) {
          auto Ddst  = D(rtl.result(0).hw.index());
          auto Dsrc1 = D(rtl.source(0).hw.index());
          auto Dsrc2 = D(rtl.source(1).hw.index());
          assembler.FADD(Ddst, Dsrc1, Dsrc2);
        }
        break;
      }

      case Opcode::SUB_32: {
        assert(rtl.source(0).hw.assigned());
        assert(rtl.source(1).hw.assigned());
        assert(rtl.result(0).hw.assigned());

        if (rtl.result(0).hw.type() == ScalarType) {
          auto Wdst  = W(rtl.result(0).hw.index());
          auto Wsrc1 = W(rtl.source(0).hw.index());
          auto Wsrc2 = W(rtl.source(1).hw.index());
          assembler.SUB(Wdst, Wsrc1, Wsrc2);
        } else if (rtl.result(0).hw.type() == VectorType) {
          auto Sdst  = S(rtl.result(0).hw.index());
          auto Ssrc1 = S(rtl.source(0).hw.index());
          auto Ssrc2 = S(rtl.source(1).hw.index());
          assembler.FSUB(Sdst, Ssrc1, Ssrc2);
        }
        break;
      }

      case Opcode::SUB_64: {
        assert(rtl.source(0).hw.assigned());
        assert(rtl.source(1).hw.assigned());
        assert(rtl.result(0).hw.assigned());

        if (rtl.result(0).hw.type() == ScalarType) {
          auto Xdst  = X(rtl.result(0).hw.index());
          auto Xsrc1 = X(rtl.source(0).hw.index());
          auto Xsrc2 = X(rtl.source(1).hw.index());
          assembler.SUB(Xdst, Xsrc1, Xsrc2);
        } else if (rtl.result(0).hw.type() == VectorType) {
          auto Ddst  = D(rtl.result(0).hw.index());
          auto Dsrc1 = D(rtl.source(0).hw.index());
          auto Dsrc2 = D(rtl.source(1).hw.index());
          assembler.FSUB(Ddst, Dsrc1, Dsrc2);
        }
        break;
      }

      case Opcode::UMUL_32: {
        assert(rtl.source(0).hw.assigned());
        assert(rtl.source(1).hw.assigned());
        assert(rtl.result(0).hw.assigned());

        if (rtl.result(0).hw.type() == ScalarType) {
          auto Xdst  = X(rtl.result(0).hw.index());
          auto Wsrc1 = W(rtl.source(0).hw.index());
          auto Wsrc2 = W(rtl.source(1).hw.index());
          assembler.UMADDL(Xdst, Wsrc1, Wsrc2, Xzr);
        } else if (rtl.result(0).hw.type() == VectorType) {
          throw std::invalid_argument("umul32 type unhandled");
        }
        break;
      }

      case Opcode::MUL_32: {
        assert(rtl.source(0).hw.assigned());
        assert(rtl.source(1).hw.assigned());
        assert(rtl.result(0).hw.assigned());

        if (rtl.result(0).hw.type() == ScalarType) {
          auto Xdst  = X(rtl.result(0).hw.index());
          auto Wsrc1 = W(rtl.source(0).hw.index());
          auto Wsrc2 = W(rtl.source(1).hw.index());
          assembler.SMADDL(Xdst, Wsrc1, Wsrc2, Xzr);
        } else if (rtl.result(0).hw.type() == VectorType) {
          auto Sdst  = S(rtl.result(0).hw.index());
          auto Ssrc1 = S(rtl.source(0).hw.index());
          auto Ssrc2 = S(rtl.source(1).hw.index());
          assembler.FMUL(Sdst, Ssrc1, Ssrc2);
        }
        break;
      }

      case Opcode::DIV_32: {
        assert(rtl.source(0).hw.assigned());
        assert(rtl.source(1).hw.assigned());
        assert(rtl.result(0).hw.assigned());

        if (rtl.result(0).hw.type() == VectorType) {
          auto dest = S(rtl.result(0).hw.index());
          auto src1 = S(rtl.source(0).hw.index());
          auto src2 = S(rtl.source(1).hw.index());
          assembler.FDIV(dest, src1, src2);
        } else {
          throw std::invalid_argument("div32 type unhandled");
        }
        break;
      }

      case Opcode::SQRT_32: {
        assert(rtl.source(0).hw.assigned());
        assert(rtl.result(0).hw.assigned());

        if (rtl.result(0).hw.type() == VectorType) {
          auto dest = S(rtl.result(0).hw.index());
          auto src1 = S(rtl.source(0).hw.index());
          assembler.FSQRT(dest, src1);
        } else {
          throw std::invalid_argument("sqrt_32 type unhandled");
        }
        break;
      }

      case Opcode::OR_32: {
        assert(rtl.source(0).hw.assigned());
        assert(rtl.source(1).hw.assigned());
        assert(rtl.result(0).hw.assigned());

        auto Wdst  = W(rtl.result(0).hw.index());
        auto Wsrc1 = W(rtl.source(0).hw.index());
        auto Wsrc2 = W(rtl.source(1).hw.index());
        assembler.ORR(Wdst, Wsrc1, Wsrc2);
        break;
      }

      case Opcode::OR_32_IMM: {
        assert(rtl.source(0).hw.assigned());
        assert(rtl.result(0).hw.assigned());

        auto Wdst  = W(rtl.result(0).hw.index());
        auto Wsrc1 = W(rtl.source(0).hw.index());
        assembler.ORR(Wdst, Wsrc1, rtl.data);
        break;
      }

      case Opcode::AND_32: {
        assert(rtl.source(0).hw.assigned());
        assert(rtl.source(1).hw.assigned());
        assert(rtl.result(0).hw.assigned());

        auto Wdst  = W(rtl.result(0).hw.index());
        auto Wsrc1 = W(rtl.source(0).hw.index());
        auto Wsrc2 = W(rtl.source(1).hw.index());
        assembler.AND(Wdst, Wsrc1, Wsrc2);
        break;
      }

      case Opcode::AND_64: {
        assert(rtl.source(0).hw.assigned());
        assert(rtl.source(1).hw.assigned());
        assert(rtl.result(0).hw.assigned());

        auto Xdst  = X(rtl.result(0).hw.index());
        auto Xsrc1 = X(rtl.source(0).hw.index());
        auto Xsrc2 = X(rtl.source(1).hw.index());
        assembler.AND(Xdst, Xsrc1, Xsrc2);
        break;
      }

      case Opcode::AND_32_IMM: {
        assert(rtl.source(0).hw.assigned());
        assert(rtl.result(0).hw.assigned());

        auto Wdst  = W(rtl.result(0).hw.index());
        auto Wsrc1 = W(rtl.source(0).hw.index());
        assembler.AND(Wdst, Wsrc1, rtl.data);
        break;
      }

      case Opcode::AND_64_IMM: {
        assert(rtl.source(0).hw.assigned());
        assert(rtl.result(0).hw.assigned());

        auto Xdst  = X(rtl.result(0).hw.index());
        auto Xsrc1 = X(rtl.source(0).hw.index());
        assembler.AND(Xdst, Xsrc1, rtl.data);
        break;
      }

      case Opcode::XOR_32: {
        assert(rtl.source(0).hw.assigned());
        assert(rtl.source(1).hw.assigned());
        assert(rtl.result(0).hw.assigned());

        auto Wdst  = W(rtl.result(0).hw.index());
        auto Wsrc1 = W(rtl.source(0).hw.index());
        auto Wsrc2 = W(rtl.source(1).hw.index());
        assembler.EOR(Wdst, Wsrc1, Wsrc2);
        break;
      }

      case Opcode::XOR_32_IMM: {
        assert(rtl.source(0).hw.assigned());
        assert(rtl.result(0).hw.assigned());

        auto Wdst  = W(rtl.result(0).hw.index());
        auto Wsrc1 = W(rtl.source(0).hw.index());
        assembler.EOR(Wdst, Wsrc1, rtl.data);
        break;
      }

      case Opcode::EXTEND32_BYTE: {
        auto Wdst  = W(rtl.result(0).hw.index());
        auto Wsrc  = W(rtl.source(0).hw.index());
        auto Wzero = W(rtl.source(1).hw.index());
        assembler.ADD(Wdst, Wzero, Wsrc, Extension::SXTB, 0);
        break;
      }

      case Opcode::EXTEND32_WORD: {
        auto Wdst  = W(rtl.result(0).hw.index());
        auto Wsrc  = W(rtl.source(0).hw.index());
        auto Wzero = W(rtl.source(1).hw.index());
        assembler.ADD(Wdst, Wzero, Wsrc, Extension::SXTH, 0);
        break;
      }

      case Opcode::TEST_32: {
        assert(rtl.source(0).hw.assigned());
        assert(rtl.result(0).hw.assigned());

        auto Wdst  = W(rtl.result(0).hw.index());
        auto Wsrc1 = W(rtl.source(0).hw.index());

        // Wdst = (src1 & src2) ? (0+1) : (0);
        // Note: ANDS with Rd=0b11111 is an alias for TST. This is effectively CSET.
        assembler.ANDS(Wzr, Wsrc1, Wsrc1);
        assembler.CSINC(Wdst, Wzr, Wzr, Condition::NotEqual);
      } break;

      case Opcode::COND_SELECT_32: {
        assert(rtl.source(0).hw.assigned());
        assert(rtl.source(1).hw.assigned());
        assert(rtl.result(0).hw.assigned());

        auto Wdst      = W(rtl.result(0).hw.index());
        auto Wdecision = W(rtl.source(0).hw.index());
        auto Wfalse    = W(rtl.source(1).hw.index());
        auto Wtrue     = W(rtl.source(2).hw.index());
        assembler.SUBS(W(31), Wdecision, W(31));
        assembler.CSEL(Wdst, Wfalse, Wtrue, Condition::Code::Equal);
        break;
      }

      case Opcode::CMP: {
        assert(rtl.result(0).hw.assigned());
        assert(rtl.source(0).hw.assigned());
        assert(rtl.source(1).hw.assigned());

        auto Wdst  = W(rtl.result(0).hw.index());
        auto Wsrc0 = W(rtl.source(0).hw.index());
        auto Wsrc1 = W(rtl.source(1).hw.index());
        auto cond  = (Condition::Code)rtl.data;

        // result <- 0 .. Compare src0, src1 ..
        assembler.SUBS(Wzr, Wsrc0, Wsrc1);
        assembler.CSINC(Wdst, Wzr, Wzr, cond);
        break;
      }

      case Opcode::SHIFTL_32_IMM: {
        assert(rtl.source(0).hw.assigned());
        assert(rtl.result(0).hw.assigned());

        auto Wdst = W(rtl.result(0).hw.index());
        auto Wsrc = W(rtl.source(0).hw.index());
        assembler.ADD(Wdst, W(31), Wsrc, RegisterShift::LSL, rtl.data);
        break;
      }

      case Opcode::SHIFTL_32: {
        assert(rtl.source(0).hw.assigned());
        assert(rtl.source(1).hw.assigned());
        assert(rtl.result(0).hw.assigned());

        auto Wdst = W(rtl.result(0).hw.index());
        auto Wsrc = W(rtl.source(0).hw.index());
        auto Wamt = W(rtl.source(1).hw.index());
        assembler.LSLV(Wdst, Wsrc, Wamt);
        break;
      }

      case Opcode::SHIFTR_32_IMM: {
        assert(rtl.source(0).hw.assigned());
        assert(rtl.result(0).hw.assigned());

        auto Wdst = W(rtl.result(0).hw.index());
        auto Wsrc = W(rtl.source(0).hw.index());
        assembler.ADD(Wdst, W(31), Wsrc, RegisterShift::LSR, rtl.data);
        break;
      }

      case Opcode::SHIFTR_32: {
        assert(rtl.source(0).hw.assigned());
        assert(rtl.source(1).hw.assigned());
        assert(rtl.result(0).hw.assigned());

        auto Wdst = W(rtl.result(0).hw.index());
        auto Wsrc = W(rtl.source(0).hw.index());
        auto Wamt = W(rtl.source(1).hw.index());
        assembler.LSRV(Wdst, Wsrc, Wamt);
        break;
      }

        // case Opcode::ASHIFTR_32_IMM: {
        //   assert(rtl.source(0).hw.assigned());
        //   assert(rtl.result(0).hw.assigned());

        //   auto Wdst = W(rtl.result(0).hw.index());
        //   auto Wsrc = W(rtl.source(0).hw.index());
        //   assembler.ASRV(Wdst, W(31), Wsrc, RegisterShift::LSR, rtl.data);
        //   break;
        // }

      case Opcode::ASHIFTR_32: {
        assert(rtl.source(0).hw.assigned());
        assert(rtl.source(1).hw.assigned());
        assert(rtl.result(0).hw.assigned());

        auto Wdst = W(rtl.result(0).hw.index());
        auto Wsrc = W(rtl.source(0).hw.index());
        auto Wamt = W(rtl.source(1).hw.index());
        assembler.ASRV(Wdst, Wsrc, Wamt);
        break;
      }

      case Opcode::LOAD_GUEST_MEMORY: {
        assert(rtl.result(0).hw.assigned());
        assert(rtl.source(0).hw.assigned());

        const auto Wdst  = W(rtl.result(0).hw.index());
        const auto Waddr = W(rtl.source(0).hw.index());
        const u64 bytes  = rtl.data;

        if (bytes == 4) {
          assembler.LDR(Wdst, guest_memory_base_pointer_reg, Waddr);
        } else if (bytes == 2) {
          assembler.LDRH(Wdst, guest_memory_base_pointer_reg, Waddr);
        } else if (bytes == 1) {
          assembler.LDRB(Wdst, guest_memory_base_pointer_reg, Waddr);
        } else {
          fmt::print("Unhandled bytes = {}\n", bytes);
          throw std::invalid_argument("Unhandled load bytes");
        }
        break;
      }

      case Opcode::CALL_FRAMED: {
        // Form is {result = } source[0](guest_ptr, source[1], source[2], {source[3]});
        // (result and sources 1..3 are optional)

        // assert(rtl.result_count == 1);
        const bool has_result = rtl.result_count > 0;

        assert(rtl.source(0).hw.assigned());
        const auto &saved_state = rtl.saved_state();

        const auto call_address = rtl.source(0).hw.index();
        // const auto func_arg1 = rtl.source(1).hw.index();
        // const auto func_arg2 = rtl.source(2).hw.index();

        const jit::RegisterSet gpr_state    = saved_state[size_t(ScalarType)];
        const jit::RegisterSet vector_state = saved_state[size_t(VectorType)];

        std::vector<u32> gpr_save_set;
        for (u32 i = 0; i < 32; ++i) {
          const bool is_allocated    = !gpr_state.is_free(jit::HwRegister(ScalarType, i));
          const bool is_caller_saved = (abi_caller_saved >> i) & 1;
          // Don't save the result register. We're going to actually overwrite it.
          // Even in the case result is 'allocated', it was allocated for a result.
          if (has_result && rtl.result(0).hw.index() == i) {
            continue;
          }

          if (is_allocated && is_caller_saved) {
            gpr_save_set.push_back(i);
          }
        }

        // Save vector registers.
        std::vector<u32> vector_save_set;
        for (u32 i = 0; i < 32; ++i) {
          const bool is_allocated = !vector_state.is_free(jit::HwRegister(VectorType, i));
          if (is_allocated) {
            vector_save_set.push_back(i);
          }
        }

        // Save pairs of registers, and then if there was an odd number, push the last one
        // to the stack while maintaining 16 byte alignment.
        u32 i = 0;
        for (; i < gpr_save_set.size() - 1; i += 2) {
          assembler.STP_pre(X(gpr_save_set[i]), X(gpr_save_set[i + 1]), SP, -16);
        }

        if (gpr_save_set.size() % 2) {
          assembler.STR_pre(X(gpr_save_set[i++]), SP, -16);
        }

        // TODO : This is not super efficient, assuming every saved FPU register is double
        // to simplify.
        for (u32 j = 0; j < vector_save_set.size(); ++j) {
          assembler.STR_pre(D(vector_save_set[j]), SP, -16);
        }

        // Do the call
        // X0 always initially holds guest pointer.
        // assembler.ADD(X(1), X(func_arg1), 0);
        // assembler.ADD(X(2), X(func_arg2), 0);
        // if (has_third_arg) {
        //   assembler.ADD(X(3), X(rtl.source(3).hw.index()), 0);
        // }

        assembler.BLR(X(call_address));

        // If there was a result, it's now in X0, move it to the destination register.
        if (has_result) {
          assembler.ADD(X(rtl.result(0).hw.index()), X(0), 0);
        }

        // Restore saved registers
        for (i32 j = vector_save_set.size() - 1; j >= 0; --j) {
          assembler.LDR_post(D(vector_save_set[j]), SP, 16);
        }

        if (gpr_save_set.size() % 2) {
          assembler.LDR_post(X(gpr_save_set[--i]), SP, 16);
        }

        for (; i > 0; i -= 2) {
          assembler.LDP_post(X(gpr_save_set[i - 2]), X(gpr_save_set[i - 1]), SP, 16);
        }

        // TODO : Restore vector registers.
        break;
      }

        // TODO : These RET opcodes should really jump to an RTL label, but this works.
      case Opcode::RET: {
        assembler.MOV(X(0), rtl.data);
        assembler.B(exit_label);
        break;
      }

      case Opcode::COND_RET: {
        const auto exit_condition = rtl.source(0).hw.index();
        const auto jump_over_exit = assembler.create_label();

        assembler.SUBS(X(31), X(31), X(exit_condition));
        assembler.B(Condition::Equal, jump_over_exit);
        assembler.MOV(X(0), rtl.data);
        assembler.B(exit_label);
        assembler.push_label(jump_over_exit);
        break;
      }

      default: {
        unhandled_rtl_opcodes.insert(rtl.op);
        break;
      }
    }
  }

  // ------------------------------------------------
  // Function epilogue
  assembler.push_label(exit_label);
  assembler.LDP_post(X(29), X(30), SP, 16); // Restore FP+LR
  assembler.RET(X(30));

  // ------------------------------------------------
  // Temporary: If there are unhandled RLT opcodes, throw with the list
  if (!unhandled_rtl_opcodes.empty()) {
    std::stringstream what;
    what << "While assembling A64, unhandled RTL opcodes :";
    for (u32 rtl_op : unhandled_rtl_opcodes) {
      what << rtl_op << ", ";
    }

    throw std::invalid_argument(what.str());
  } else {
#ifdef JIT_DEBUG
    printf(" !!! All RTL -> assembly succeeded!\n");
#endif
  }

  // ------------------------------------------------
  // ------------------------------------------------
  // Finalize assembly, resolving labels, packing constant data, etc.
  const std::vector<u32> instructions = assembler.assemble();

  if (1) {
    FILE *f = fopen("/tmp/arm64.bin", "w");
    fwrite(&instructions[0], 4, instructions.size(), f);
    fclose(f);
  }

  if (dump_and_die) {
    assert(false && "Dump-and-die hit in ARM64 compiler");
  }

  u8 *const data = (u8 *)&instructions[0];
  codegen::arm64::Routine *const result =
    new codegen::arm64::Routine(data, 4 * instructions.size());
  m_routine = std::unique_ptr<codegen::arm64::Routine>(result);
}

void
Compiler::generate_rtl()
{
  std::set<u32> unhandled_ir_opcodes;

  /* Reset all state generated by this method. */
  m_rtl.clear();

  /* Allocate the single EBB used for all generated instructions. */
  /* TODO Support control flow once required logic is available in RTL. */
  jit::RtlProgram::BlockHandle block_handle = m_rtl.allocate_block("arm64_entry");
  assert(block_handle == 0);
  (void)block_handle;

  // Signature being generated for...
  // JIT = void fn(Guest *guest, void *memory_base, void*register_base);
  // X0 = Guest*, X1 = memory_base, X2 = register_base

  // u16 exit_label = allocate_label();

  // Decode all IR Opcodes -> RTL instructions (but no assigned registers yet)
  for (const ir::Instruction &current : m_source.instructions()) {
    try {
      switch (current.opcode()) {
        /* Read from a guest register in host memory. */
        case ir::Opcode::ReadGuest: {
          const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
          const u16 reg_num                 = current.source(0).value().u16_value;
          switch (current.result(0).type()) {
            case ir::Type::Integer32:
              RTL_ENCODE_0R(Opcode::READ_GUEST_REGISTER32, reg_num, HW_ANY(ssa_result));
              break;
            case ir::Type::Float32:
              RTL_ENCODE_0R(Opcode::READ_GUEST_REGISTER32, reg_num, VEC_ANY(ssa_result));
              break;
            case ir::Type::Float64:
              RTL_ENCODE_0R(Opcode::READ_GUEST_REGISTER64, reg_num, VEC_ANY(ssa_result));
              break;
            default: {
              assert(false);
            }
          }
          break;
        }

        /* Write to a guest register in host memory. */
        case ir::Opcode::WriteGuest: {
          const jit::RtlRegister ssa_value = get_rtl_ssa(current.source(1));
          const u16 reg_num                = current.source(0).value().u16_value;
          switch (current.source(1).type()) {
            case ir::Type::Integer32:
              RTL_ENCODE_1N(Opcode::WRITE_GUEST_REGISTER32, reg_num, HW_ANY(ssa_value));
              break;
            case ir::Type::Integer64:
              RTL_ENCODE_1N(Opcode::WRITE_GUEST_REGISTER64, reg_num, HW_ANY(ssa_value));
              break;
            case ir::Type::Float32:
              RTL_ENCODE_1N(Opcode::WRITE_GUEST_REGISTER32, reg_num, VEC_ANY(ssa_value));
              break;
            case ir::Type::Float64:
              RTL_ENCODE_1N(Opcode::WRITE_GUEST_REGISTER64, reg_num, VEC_ANY(ssa_value));
              break;
            default:
              throw std::invalid_argument("WriteGuest unhandled case");
          }
          break;
        }

        case ir::Opcode::Load: {
          m_uses_memory = true;
          u64 bytes     = ir_type_to_bytes(current.result(0).type());
          bool is_float = (current.result(0).type() == ir::Type::Float32) ||
                          (current.result(0).type() == ir::Type::Float64);

          const jit::RtlRegister ssa_call_target = m_rtl.ssa_allocate(0);
          const jit::RtlRegister ssa_result      = make_rtl_ssa(current.result(0));
          const jit::RtlRegister ssa_address     = get_rtl_ssa(current.source(0));

          if (!is_float && m_use_fastmem) {
            RTL_ENCODE_1R(Opcode::LOAD_GUEST_MEMORY,
                          make_constant(bytes),
                          HW_ANY(ssa_result),
                          HW_ANY(ssa_address));
          } else {

            const jit::RtlRegister ssa_bytes = m_rtl.ssa_allocate(0);

            RTL_ENCODE_0R(Opcode::LOAD_IMM64,
                          make_constant(ir_type_to_bytes(current.type())),
                          HW_ANY(ssa_bytes));

            // Function pointer to invoke
            RTL_ENCODE_0R(Opcode::LOAD_IMM64,
                          make_constant(u64(&wrap_load)),
                          HW_ANY(ssa_call_target));

            // Note: We need the guest pointer to be in X0 when we call, but it's already
            // there by convention.

            jit::RtlRegister load_destination;
            if (is_float) {
              jit::RtlRegister ssa_load = m_rtl.ssa_allocate(0);
              RTL_ENCODE_3RS(Opcode::CALL_FRAMED,
                             0,
                             HW_ANY(ssa_load),
                             HW_ANY(ssa_call_target),
                             HW_AT(ssa_address, 1),
                             HW_AT(ssa_bytes, 2));

              if (current.result(0).type() == ir::Type::Float32) {
                RTL_ENCODE_1R(Opcode::FMOV32, 0, VEC_ANY(ssa_result), HW_ANY(ssa_load));
              } else if (current.result(0).type() == ir::Type::Float64) {
                RTL_ENCODE_1R(Opcode::FMOV64, 0, VEC_ANY(ssa_result), HW_ANY(ssa_load));
              } else
                assert(false);

            } else {
              RTL_ENCODE_3RS(Opcode::CALL_FRAMED,
                             0,
                             HW_ANY(ssa_result),
                             HW_ANY(ssa_call_target),
                             HW_AT(ssa_address, 1),
                             HW_AT(ssa_bytes, 2));
            }
          }

        } break;

        case ir::Opcode::Call: {
          assert(current.source(0).is_constant());
          assert(current.source(0).type() == ir::Type::HostAddress);

          const jit::RtlRegister ssa_call_target = m_rtl.ssa_allocate(0);
          RTL_ENCODE_0R(Opcode::LOAD_IMM64,
                        make_constant(u64(current.source(0).value().hostptr_value)),
                        HW_ANY(ssa_call_target));

          /* The first argument (argument 0) is implicit. The register used for
           * passing argument 0 on amd64 is always set to the guest instance.
           * The return value is assumed but potentially unused / throwaway. */
          const bool has_result = current.result_count() > 0;
          assert(current.result_count() <= 1);

          const jit::RtlRegister ssa_result =
            has_result ? make_rtl_ssa(current.result(0)) : m_rtl.ssa_allocate(0);

          /* The argument count does not affect code generation, since the RTL
           * register assignments are responsible for handling argument setup. */
          if (current.source_count() == 1) {
            RTL_ENCODE_1RS(
              Opcode::CALL_FRAMED, 0, HW_ANY(ssa_result), HW_ANY(ssa_call_target));
          } else if (current.source_count() == 2) {
            const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(1));
            RTL_ENCODE_2RS(Opcode::CALL_FRAMED,
                           0,
                           HW_ANY(ssa_result),
                           HW_ANY(ssa_call_target),
                           HW_AT(ssa_arg1, 1));
          } else if (current.source_count() == 3) {
            const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(1));
            const jit::RtlRegister ssa_arg2 = get_rtl_ssa(current.source(2));
            RTL_ENCODE_3RS(Opcode::CALL_FRAMED,
                           0,
                           HW_ANY(ssa_result),
                           HW_ANY(ssa_call_target),
                           HW_AT(ssa_arg1, 1),
                           HW_AT(ssa_arg2, 2));
          } else {
            assert(false);
          }
          break;
        }

        case ir::Opcode::Store: {
          m_uses_memory = true;

          const jit::RtlRegister ssa_call_target = m_rtl.ssa_allocate(0);
          const jit::RtlRegister ssa_address     = get_rtl_ssa(current.source(0));
          const jit::RtlRegister ssa_value       = get_rtl_ssa(current.source(1));
          const jit::RtlRegister ssa_bytes       = m_rtl.ssa_allocate(0);

          const bool is_float = (current.source(1).type() == ir::Type::Float32) ||
                                (current.source(1).type() == ir::Type::Float64);

          // Size of store
          RTL_ENCODE_0R(Opcode::LOAD_IMM64,
                        make_constant(ir_type_to_bytes(current.type())),
                        HW_ANY(ssa_bytes));

          // Function pointer to invoke
          RTL_ENCODE_0R(
            Opcode::LOAD_IMM64, make_constant(u64(&wrap_store)), HW_ANY(ssa_call_target));

          // Note: We need the guest pointer to be in X0 when we call, but it's already
          // there by convention.

          jit::RtlRegister ssa_temp;
          if (is_float) {
            ssa_temp            = m_rtl.ssa_allocate(0);
            const Opcode opcode = (current.source(1).type() == ir::Type::Float32)
                                    ? Opcode::FMOV32
                                    : Opcode::FMOV64;
            RTL_ENCODE_1R(opcode, 0, HW_ANY(ssa_temp), VEC_ANY(ssa_value));
          }

          RTL_ENCODE_4NS(Opcode::CALL_FRAMED,
                         0,
                         HW_ANY(ssa_call_target),
                         HW_AT(ssa_address, 1),
                         HW_AT(ssa_bytes, 2),
                         HW_AT((is_float ? ssa_temp : ssa_value), 3));

          break;
        }

        case ir::Opcode::Compare_lt:
        case ir::Opcode::Compare_lte:
        case ir::Opcode::Compare_ult:
        case ir::Opcode::Compare_ulte:
        case ir::Opcode::Compare_eq: {
          if (current.type() != ir::Type::Integer32)
            throw std::invalid_argument("Unhandled comparison type");

          const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
          jit::RtlRegister sources[2];
          for (u8 i = 0; i < 2; ++i) {
            // If an arg is constant, move to reg
            if (current.source(i).is_constant()) {
              sources[i]         = m_rtl.ssa_allocate(0);
              const u32 constant = current.source(i).value().u32_value;
              RTL_ENCODE_0R(
                Opcode::LOAD_IMM32, make_constant(constant), HW_ANY(sources[i]));
            } else {
              sources[i] = get_rtl_ssa(current.source(i));
            }
          }

#define CASE(_case, _asm_cond)                                                           \
  case _case:                                                                            \
    RTL_ENCODE_2R(Opcode::CMP,                                                           \
                  make_constant(_asm_cond),                                              \
                  HW_ANY(ssa_result),                                                    \
                  HW_ANY(sources[0]),                                                    \
                  HW_ANY(sources[1]));                                                   \
    break;
          switch (current.opcode()) {
            CASE(ir::Opcode::Compare_eq, Condition::Equal)
            CASE(ir::Opcode::Compare_ulte, Condition::UnsignedLessThanOrEqual)
            CASE(ir::Opcode::Compare_lte, Condition::SignedLessThanOrEqual)
            CASE(ir::Opcode::Compare_ult, Condition::CarryClear)
            CASE(ir::Opcode::Compare_lt, Condition::Negative)
            default:
              throw std::invalid_argument("Unhandled comparison case");
          }
#undef CASE
        } break;

        case ir::Opcode::BitSetClear: {
          assert(current.source(2).is_constant());
          const jit::RtlRegister ssa_result  = make_rtl_ssa(current.result(0));
          const jit::RtlRegister ssa_input   = get_rtl_ssa(current.source(0));
          const jit::RtlRegister ssa_control = get_rtl_ssa(current.source(1));
          const u8 position                  = current.source(2).value().u8_value;
          switch (current.type()) {
            case ir::Type::Integer32: {
              const jit::RtlRegister ssa_masked = m_rtl.ssa_allocate(0);
              const jit::RtlRegister ssa_bit    = m_rtl.ssa_allocate(0);
              RTL_ENCODE_1R(Opcode::AND_32_IMM,
                            make_constant(~u32(1u << position)),
                            HW_ANY(ssa_masked),
                            HW_ANY(ssa_input));

              RTL_ENCODE_1R(Opcode::SHIFTL_32_IMM,
                            make_constant(u8(position)),
                            HW_ANY(ssa_bit),
                            HW_ANY(ssa_control));

              RTL_ENCODE_2R(Opcode::OR_32,
                            0,
                            HW_ANY(ssa_result),
                            HW_ANY(ssa_masked),
                            HW_ANY(ssa_bit));
              break;
            }
            default:
              assert(false);
          }
        } break;

        case ir::Opcode::LogicalShiftLeft: {
          const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
          const jit::RtlRegister ssa_arg1   = get_rtl_ssa(current.source(0));

          if (current.source(1).is_constant()) {
            switch (current.type()) {
              case ir::Type::Integer32: {
                const u8 constant = current.source(1).value().u8_value;
                RTL_ENCODE_1R(Opcode::SHIFTL_32_IMM,
                              make_constant(constant),
                              HW_ANY(ssa_result),
                              HW_ANY(ssa_arg1));
                continue;
              }
              default:
                break;
            }
          }

          const jit::RtlRegister ssa_arg2 = get_rtl_ssa(current.source(1));
          RTL_ENCODE_2R(
            Opcode::SHIFTL_32, 0, HW_ANY(ssa_result), HW_ANY(ssa_arg1), HW_ANY(ssa_arg2));

        } break;

        case ir::Opcode::LogicalShiftRight: {
          const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
          const jit::RtlRegister ssa_arg1   = get_rtl_ssa(current.source(0));

          if (current.source(1).is_constant()) {
            switch (current.type()) {
              case ir::Type::Integer32: {
                const u8 constant = current.source(1).value().u8_value;
                RTL_ENCODE_1R(Opcode::SHIFTR_32_IMM,
                              make_constant(constant),
                              HW_ANY(ssa_result),
                              HW_ANY(ssa_arg1));
                continue;
              }
              default:
                break;
            }
          }

          const jit::RtlRegister ssa_arg2 = get_rtl_ssa(current.source(1));
          RTL_ENCODE_2R(
            Opcode::SHIFTR_32, 0, HW_ANY(ssa_result), HW_ANY(ssa_arg1), HW_ANY(ssa_arg2));

        } break;

          ///////////////////////////////////////////////////////////////////
        case ir::Opcode::ArithmeticShiftRight: {
          const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
          const jit::RtlRegister ssa_arg1   = get_rtl_ssa(current.source(0));

          // if (current.source(1).is_constant()) {
          //   switch (current.type()) {
          //     case ir::Type::Integer32: {
          //       const u8 constant =
          //       current.source(1).value().u8_value;
          //       RTL_ENCODE_1R(Opcode::ASHIFTR_32_IMM,
          //                     make_constant(constant),
          //                     HW_ANY(ssa_result),
          //                     HW_ANY(ssa_arg1));
          //       continue;
          //     }
          //     default:
          //       break;
          //   }
          // }

          const jit::RtlRegister ssa_arg2 = get_rtl_ssa(current.source(1));
          RTL_ENCODE_2R(Opcode::ASHIFTR_32,
                        0,
                        HW_ANY(ssa_result),
                        HW_ANY(ssa_arg1),
                        HW_ANY(ssa_arg2));

        } break;

        case ir::Opcode::Extend32: {
          assert(current.source(0).is_register());

          const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
          const jit::RtlRegister ssa_arg    = get_rtl_ssa(current.source(0));
          auto ssa_temp                     = m_rtl.ssa_allocate(0);

          RTL_ENCODE_0R(Opcode::LOAD_IMM32, make_constant(0), HW_ANY(ssa_temp));

          switch (current.type()) {
            case ir::Type::Integer8:
              RTL_ENCODE_2R(Opcode::EXTEND32_BYTE,
                            0,
                            HW_ANY(ssa_result),
                            HW_ANY(ssa_arg),
                            HW_ANY(ssa_temp));
              break;
            case ir::Type::Integer16:
              RTL_ENCODE_2R(Opcode::EXTEND32_WORD,
                            0,
                            HW_ANY(ssa_result),
                            HW_ANY(ssa_arg),
                            HW_ANY(ssa_temp));
              break;
            default:
              assert(false);
          }
        } break;

        case ir::Opcode::Add: {
          const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
          const jit::RtlRegister ssa_arg1   = get_rtl_ssa(current.source(0));

          if (current.source(1).is_constant()) {
            switch (current.type()) {
              case ir::Type::Integer8:
              case ir::Type::Integer16:
              case ir::Type::Integer32: {
                auto ssa_temp            = m_rtl.ssa_allocate(0);
                const u32 constant_value = current.source(1).value().u32_value;
                RTL_ENCODE_0R(Opcode::LOAD_IMM32, constant_value, HW_ANY(ssa_temp));
                RTL_ENCODE_2R(Opcode::ADD_32,
                              0,
                              HW_ANY(ssa_result),
                              HW_ANY(ssa_arg1),
                              HW_ANY(ssa_temp));
                continue;
              }
              default:
                throw std::invalid_argument("Unhandled ir::Opcode::Add constant size");
                break;
            }
          }

          const jit::RtlRegister ssa_arg2 = get_rtl_ssa(current.source(1));
          switch (current.type()) {
            case ir::Type::Integer32:
              RTL_ENCODE_2R(Opcode::ADD_32,
                            0,
                            HW_ANY(ssa_result),
                            HW_ANY(ssa_arg1),
                            HW_ANY(ssa_arg2));
              break;
            case ir::Type::Float32:
              RTL_ENCODE_2R(Opcode::ADD_32,
                            0,
                            VEC_ANY(ssa_result),
                            VEC_ANY(ssa_arg1),
                            VEC_ANY(ssa_arg2));
              break;
            case ir::Type::Float64:
              RTL_ENCODE_2R(Opcode::ADD_64,
                            0,
                            VEC_ANY(ssa_result),
                            VEC_ANY(ssa_arg1),
                            VEC_ANY(ssa_arg2));
              break;
            default:
              throw std::invalid_argument("Unhandled ir::Opcode::Add arg2 type");
              break;
          }

          break;
        }

        case ir::Opcode::Subtract: {
          const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
          const jit::RtlRegister ssa_arg1   = get_rtl_ssa(current.source(0));
          const jit::RtlRegister ssa_arg2   = get_rtl_ssa(current.source(1));
          switch (current.result(0).type()) {
            // Currently causes a routine to calculate a bad offset during boot animation.
            case ir::Type::Integer32:
              RTL_ENCODE_2R(Opcode::SUB_32,
                            0,
                            HW_ANY(ssa_result),
                            HW_ANY(ssa_arg1),
                            HW_ANY(ssa_arg2));
              break;
            case ir::Type::Float32:
              RTL_ENCODE_2R(Opcode::SUB_32,
                            0,
                            VEC_ANY(ssa_result),
                            VEC_ANY(ssa_arg1),
                            VEC_ANY(ssa_arg2));
              break;
            case ir::Type::Float64:
              RTL_ENCODE_2R(Opcode::SUB_64,
                            0,
                            VEC_ANY(ssa_result),
                            VEC_ANY(ssa_arg1),
                            VEC_ANY(ssa_arg2));
              break;
            default:
              throw std::invalid_argument("Unhandled ir::Opcode::Sub arg2 type");
              break;
          }
        } break;

        case ir::Opcode::Multiply_u: {
          const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
          const jit::RtlRegister ssa_arg1   = get_rtl_ssa(current.source(0));
          const jit::RtlRegister ssa_arg2   = get_rtl_ssa(current.source(1));

          switch (current.type()) {
            case ir::Type::Integer32:
              RTL_ENCODE_2R(Opcode::UMUL_32,
                            0,
                            HW_ANY(ssa_result),
                            HW_ANY(ssa_arg1),
                            HW_ANY(ssa_arg2));
              break;
            default:
              throw std::invalid_argument("Unhandled ir::Opcode::Multiply_u arg2 type");
              break;
          }

          break;
        }

        case ir::Opcode::Multiply: {
          const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
          const jit::RtlRegister ssa_arg1   = get_rtl_ssa(current.source(0));
          const jit::RtlRegister ssa_arg2   = get_rtl_ssa(current.source(1));

          switch (current.type()) {
            case ir::Type::Integer32:
              RTL_ENCODE_2R(Opcode::MUL_32,
                            0,
                            HW_ANY(ssa_result),
                            HW_ANY(ssa_arg1),
                            HW_ANY(ssa_arg2));
              break;
            case ir::Type::Float32:
              RTL_ENCODE_2R(Opcode::MUL_32,
                            0,
                            VEC_ANY(ssa_result),
                            VEC_ANY(ssa_arg1),
                            VEC_ANY(ssa_arg2));
              break;
            default:
              throw std::invalid_argument("Unhandled ir::Opcode::Multiply arg2 type");
              break;
          }

          break;
        }

        case ir::Opcode::Divide: {
          const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
          const jit::RtlRegister ssa_arg1   = get_rtl_ssa(current.source(0));
          const jit::RtlRegister ssa_arg2   = get_rtl_ssa(current.source(1));
          switch (current.result(0).type()) {
            case ir::Type::Float32:
              RTL_ENCODE_2R(Opcode::DIV_32,
                            0,
                            VEC_ANY(ssa_result),
                            VEC_ANY(ssa_arg1),
                            VEC_ANY(ssa_arg2));
              break;
            default:
              throw std::invalid_argument("Unhandled ir::Opcode::Sub arg2 type");
              break;
          }
        } break;

        case ir::Opcode::SquareRoot: {
          const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
          const jit::RtlRegister ssa_arg1   = get_rtl_ssa(current.source(0));
          switch (current.result(0).type()) {
            case ir::Type::Float32:
              RTL_ENCODE_1R(Opcode::SQRT_32, 0, VEC_ANY(ssa_result), VEC_ANY(ssa_arg1));
              break;
            default:
              throw std::invalid_argument("Unhandled ir::Opcode::SquareRoot result type");
              break;
          }
        } break;

        case ir::Opcode::Or: {
          const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
          const jit::RtlRegister ssa_arg1   = get_rtl_ssa(current.source(0));

          if (current.source(1).is_constant()) {
            switch (current.type()) {
              case ir::Type::Integer8:
              case ir::Type::Integer16:
              case ir::Type::Integer32: {

                const u32 constant_value = current.source(1).value().u32_value;

                if (logical_immediates.has_imm32(constant_value)) {
                  RTL_ENCODE_1R(Opcode::OR_32_IMM,
                                constant_value,
                                HW_ANY(ssa_result),
                                HW_ANY(ssa_arg1));
                } else {
                  // This constant unfortunately cannot be encoded in arm64, so we're
                  // going to need to load it into a register first.
                  auto ssa_temp = m_rtl.ssa_allocate(0);
                  RTL_ENCODE_0R(Opcode::LOAD_IMM32, constant_value, HW_ANY(ssa_temp));
                  RTL_ENCODE_2R(Opcode::OR_32,
                                0,
                                HW_ANY(ssa_result),
                                HW_ANY(ssa_arg1),
                                HW_ANY(ssa_temp));
                }
                continue;
              }
              default:
                throw std::invalid_argument("Unhandled ir::Opcode::Or constant size");
                break;
            }
          }

          const jit::RtlRegister ssa_arg2 = get_rtl_ssa(current.source(1));
          switch (current.type()) {
            case ir::Type::Integer32:
              RTL_ENCODE_2R(
                Opcode::OR_32, 0, HW_ANY(ssa_result), HW_ANY(ssa_arg1), HW_ANY(ssa_arg2));
              break;
            default:
              throw std::invalid_argument("Unhandled ir::Opcode::Or arg2 type");
              break;
          }

        } break;

        case ir::Opcode::And: {
          const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
          const jit::RtlRegister ssa_arg1   = get_rtl_ssa(current.source(0));

          // Constant argument?
          if (current.source(1).is_constant()) {
            switch (current.type()) {
              case ir::Type::Integer8:
              case ir::Type::Integer16:
              case ir::Type::Integer32: {

                const u32 constant_value = current.source(1).value().u32_value;

                if (logical_immediates.has_imm32(constant_value)) {
                  RTL_ENCODE_1R(Opcode::AND_32_IMM,
                                constant_value,
                                HW_ANY(ssa_result),
                                HW_ANY(ssa_arg1));
                } else {
                  auto ssa_temp = m_rtl.ssa_allocate(0);
                  RTL_ENCODE_0R(Opcode::LOAD_IMM32, constant_value, HW_ANY(ssa_temp));
                  RTL_ENCODE_2R(Opcode::AND_32,
                                0,
                                HW_ANY(ssa_result),
                                HW_ANY(ssa_arg1),
                                HW_ANY(ssa_temp));
                }

                continue;
              }
              default:
                throw std::invalid_argument("Unhandled ir::Opcode::And constant size");
                break;
            }
          }

          const jit::RtlRegister ssa_arg2 = get_rtl_ssa(current.source(1));
          switch (current.type()) {
            case ir::Type::Integer32:
              RTL_ENCODE_2R(Opcode::AND_32,
                            0,
                            HW_ANY(ssa_result),
                            HW_ANY(ssa_arg1),
                            HW_ANY(ssa_arg2));
              break;
            case ir::Type::Bool:
              ////////////////////////////////// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
              RTL_ENCODE_2R(Opcode::AND_32,
                            0,
                            HW_ANY(ssa_result),
                            HW_ANY(ssa_arg1),
                            HW_ANY(ssa_arg2));
              break;
            default:
              throw std::invalid_argument("Unhandled ir::Opcode::And arg2 type");
              break;
          }

        } break;

        case ir::Opcode::Test: {
          const jit::RtlRegister ssa_and_result = m_rtl.ssa_allocate(0);
          const jit::RtlRegister ssa_arg1       = get_rtl_ssa(current.source(0));

          if (current.source(1).is_constant()) {
            switch (current.type()) {
              case ir::Type::Integer8:
              case ir::Type::Integer16:
              case ir::Type::Integer32: {

                const u32 constant_value = current.source(1).value().u32_value;
                if (logical_immediates.has_imm32(constant_value)) {
                  RTL_ENCODE_1R(Opcode::AND_32_IMM,
                                constant_value,
                                HW_ANY(ssa_and_result),
                                HW_ANY(ssa_arg1));
                } else {
                  auto ssa_temp = m_rtl.ssa_allocate(0);
                  RTL_ENCODE_0R(Opcode::LOAD_IMM32, constant_value, HW_ANY(ssa_temp));
                  RTL_ENCODE_2R(Opcode::AND_32,
                                0,
                                HW_ANY(ssa_and_result),
                                HW_ANY(ssa_arg1),
                                HW_ANY(ssa_temp));
                }

                const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
                RTL_ENCODE_1R(
                  Opcode::TEST_32, 0, HW_ANY(ssa_result), HW_ANY(ssa_and_result));
                continue;
              }
              default:
                throw std::invalid_argument("Unhandled ir::Opcode::Test constant size");
                break;
            }
          }

          const jit::RtlRegister ssa_arg2 = get_rtl_ssa(current.source(1));
          switch (current.type()) {
            case ir::Type::Integer32:
              RTL_ENCODE_2R(Opcode::AND_32,
                            0,
                            HW_ANY(ssa_and_result),
                            HW_ANY(ssa_arg1),
                            HW_ANY(ssa_arg2));
              break;
            case ir::Type::Integer64:
              RTL_ENCODE_2R(Opcode::AND_64,
                            0,
                            HW_ANY(ssa_and_result),
                            HW_ANY(ssa_arg1),
                            HW_ANY(ssa_arg2));
              break;
            default:
              throw std::invalid_argument("Unhandled ir::Opcode::Test arg2 type");
              break;
          }

          const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
          RTL_ENCODE_1R(Opcode::TEST_32, 0, HW_ANY(ssa_result), HW_ANY(ssa_and_result));

        } break;

        case ir::Opcode::ExclusiveOr: {
          const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
          const jit::RtlRegister ssa_arg1   = get_rtl_ssa(current.source(0));

          if (current.source(1).is_constant()) {
            switch (current.type()) {
              case ir::Type::Integer8:
              case ir::Type::Integer16:
              case ir::Type::Integer32: {
                const u32 constant_value = current.source(1).value().u32_value;
                if (logical_immediates.has_imm32(constant_value)) {
                  RTL_ENCODE_1R(Opcode::XOR_32_IMM,
                                constant_value,
                                HW_ANY(ssa_result),
                                HW_ANY(ssa_arg1));
                  continue;
                }
                // Fall through to the general case if the constant was encode-able
              }
              default:
                throw std::invalid_argument("Unhandled ir::Opcode::And constant size");
                break;
            }
          }

          const jit::RtlRegister ssa_arg2 = get_rtl_ssa(current.source(1));
          switch (current.type()) {
            case ir::Type::Integer32:
              RTL_ENCODE_2R(Opcode::XOR_32,
                            0,
                            HW_ANY(ssa_result),
                            HW_ANY(ssa_arg1),
                            HW_ANY(ssa_arg2));
              break;
            default:
              throw std::invalid_argument("Unhandled ir::Opcode::And arg2 type");
              break;
          }

        } break;

        case ir::Opcode::BitCast: {
          const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
          const jit::RtlRegister ssa_arg1   = get_rtl_ssa(current.source(0));

          const ir::Type from_type = current.source(0).type();
          const ir::Type to_type   = current.type();

          switch (to_type) {
            case ir::Type::Integer8: {
              if (from_type == ir::Type::Integer32 || from_type == ir::Type::Integer16) {
                RTL_ENCODE_1R(
                  Opcode::AND_32_IMM, 0xFF, HW_ANY(ssa_result), HW_ANY(ssa_arg1));
              } else {
                throw std::invalid_argument("Unhandled bitcast.i8");
              }
            } break;

            case ir::Type::Integer16: {
              if (from_type == ir::Type::Integer32) {
                RTL_ENCODE_1R(
                  Opcode::AND_32_IMM, 0xFFFF, HW_ANY(ssa_result), HW_ANY(ssa_arg1));
              } else {
                throw std::invalid_argument("Unhandled bitcast.i16");
              }
            } break;

            case ir::Type::Integer32: {
              if (from_type == ir::Type::Integer8) {
                RTL_ENCODE_1R(
                  Opcode::AND_32_IMM, 0xFF, HW_ANY(ssa_result), HW_ANY(ssa_arg1));
              } else if (from_type == ir::Type::Integer16) {
                RTL_ENCODE_1R(
                  Opcode::AND_32_IMM, 0xFFFF, HW_ANY(ssa_result), HW_ANY(ssa_arg1));
              }
#if 0 // Shouldn't need this.
              else if (from_type == ir::Type::Integer32) {
                RTL_ENCODE_1R(
                  Opcode::AND_32_IMM, 0xFFFFFFFF, HW_ANY(ssa_result), HW_ANY(ssa_arg1));
              }
#endif
              else if (from_type == ir::Type::Integer64) {
                RTL_ENCODE_1R(
                  Opcode::AND_64_IMM, 0xFFFFFFFF, HW_ANY(ssa_result), HW_ANY(ssa_arg1));
              } else if (from_type == ir::Type::Float32) {
                RTL_ENCODE_1R(Opcode::FMOV32, 0, HW_ANY(ssa_result), VEC_ANY(ssa_arg1));
              } else {
                throw std::invalid_argument("Unhandled bitcast.i32");
              }
            } break;

            case ir::Type::Integer64: {
              if (from_type == ir::Type::Integer32) {
                // Pretty sure this is do nothing. MOVE

                RTL_ENCODE_1R(
                  Opcode::AND_32_IMM, 0xFFFF, HW_ANY(ssa_result), HW_ANY(ssa_arg1));
              } else {
                throw std::invalid_argument("Unhandled bitcast.i64");
              }
            } break;

            case ir::Type::Float32: {
              if (from_type == ir::Type::Integer32) {
                RTL_ENCODE_1R(Opcode::FMOV32, 0, VEC_ANY(ssa_result), HW_ANY(ssa_arg1));
              } else {
                throw std::invalid_argument("Unhandled bitcast.f32");
              }
            } break;

            default:
              throw std::invalid_argument("Unhandled ir::Opcode::And arg2 type");
              break;
          }
        } break;

        case ir::Opcode::Not: {
          const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
          const jit::RtlRegister ssa_arg1   = get_rtl_ssa(current.source(0));

          switch (current.result(0).type()) {
            case ir::Type::Integer8: {
              const jit::RtlRegister ssa_temp = m_rtl.ssa_allocate(0);
              RTL_ENCODE_0R(Opcode::LOAD_IMM32, 0xFF, HW_ANY(ssa_temp));
              RTL_ENCODE_2R(Opcode::XOR_32,
                            0,
                            HW_ANY(ssa_result),
                            HW_ANY(ssa_arg1),
                            HW_ANY(ssa_temp));
            } break;

            case ir::Type::Integer16: {
              const jit::RtlRegister ssa_temp = m_rtl.ssa_allocate(0);
              RTL_ENCODE_0R(Opcode::LOAD_IMM32, 0xFFFF, HW_ANY(ssa_temp));
              RTL_ENCODE_2R(Opcode::XOR_32,
                            0,
                            HW_ANY(ssa_result),
                            HW_ANY(ssa_arg1),
                            HW_ANY(ssa_temp));
            } break;

            case ir::Type::Integer32: {
              const jit::RtlRegister ssa_temp = m_rtl.ssa_allocate(0);
              RTL_ENCODE_0R(Opcode::LOAD_IMM32, 0xFFFFFFFF, HW_ANY(ssa_temp));
              RTL_ENCODE_2R(Opcode::XOR_32,
                            0,
                            HW_ANY(ssa_result),
                            HW_ANY(ssa_arg1),
                            HW_ANY(ssa_temp));
            } break;

            case ir::Type::Bool: {
              RTL_ENCODE_1R(Opcode::XOR_32_IMM, 1, HW_ANY(ssa_result), HW_ANY(ssa_arg1));
              break;
            }
            default:
              throw std::invalid_argument("Unhandled ir:Not type");
          }
        } break;

        case ir::Opcode::Select: {
          const jit::RtlRegister ssa_result   = make_rtl_ssa(current.result(0));
          const jit::RtlRegister ssa_decision = get_rtl_ssa(current.source(0));
          const jit::RtlRegister ssa_false    = get_rtl_ssa(current.source(1));
          const jit::RtlRegister ssa_true     = get_rtl_ssa(current.source(2));

          RTL_ENCODE_3R(Opcode::COND_SELECT_32,
                        0,
                        HW_ANY(ssa_result),
                        HW_ANY(ssa_decision),
                        HW_ANY(ssa_false),
                        HW_ANY(ssa_true));
        } break;

        case ir::Opcode::Exit: {
          // If source bool is true, exit and return i64 to caller.
          assert(current.source(1).is_constant());
          if (current.source(0).is_constant()) {
            if (current.source(1).value().u32_value) {
              RTL_ENCODE_0N(Opcode::RET, current.source(1).value().u32_value);
            }
          } else {
            const jit::RtlRegister ssa0 = get_rtl_ssa(current.source(0));
            RTL_ENCODE_1N(
              Opcode::COND_RET, current.source(1).value().u32_value, HW_ANY(ssa0));
          }
          break;
        }

        default:
          unhandled_ir_opcodes.insert((u32)current.opcode());
          break;
      }
    } catch (std::exception &e) {
      // printf("Caught exception during RTL generation for ir::Opcode = %u\n",
      //        (u32)current.opcode());
      // printf("  - '%s'\n", e.what());
      unhandled_ir_opcodes.insert((u32)current.opcode());
    }
  }

  // ------------------------------------------------
  // Temporary: If there are unhandled IR opcodes, throw with the list
  if (!unhandled_ir_opcodes.empty()) {
    std::stringstream what;
    what << "While generating RTL, unhandled ir::Opcodes: ";
    for (u32 rtl_op : unhandled_ir_opcodes)
      what << rtl_op << ", ";
    throw std::invalid_argument(what.str());
  } else {
#ifdef JIT_DEBUG
    printf(" !!!!!! All ir::Opcodes handled!\n");
#endif
  }
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

jit::RtlRegister
Compiler::get_rtl_ssa(const ir::Operand operand)
{
  if (operand.is_register()) {
    if (m_ir_to_rtl.size() <= operand.register_index()) {
      std::stringstream what;
      what << "m_ir_to_rtl size is " << m_ir_to_rtl.size()
           << ", but operand.register_index = " << operand.register_index();
      throw std::invalid_argument(what.str());
    }
    if (!m_ir_to_rtl[operand.register_index()].valid()) {
      std::stringstream what;
      what << "m_ir_to_rtl[operand.register_index() = " << operand.register_index()
           << "] is not valid()";
      throw std::invalid_argument(what.str());
    }
    return m_ir_to_rtl[operand.register_index()];
  }

  /* TODO optimize. */
  const jit::RtlRegister ssa_constant = m_rtl.ssa_allocate(0);
  switch (operand.type()) {
    case ir::Type::Integer8: {
      const u32 value    = operand.value().u8_value;
      const u64 constant = make_constant<u32>(value);
      RTL_ENCODE_0R(Opcode::LOAD_IMM32, constant, HW_ANY(ssa_constant));
      break;
    }

    case ir::Type::Integer16: {
      const u32 value    = operand.value().u16_value;
      const u64 constant = make_constant<u32>(value);
      RTL_ENCODE_0R(Opcode::LOAD_IMM32, constant, HW_ANY(ssa_constant));
      break;
    }

    case ir::Type::Integer32: {
      const u32 value    = operand.value().u32_value;
      const u64 constant = make_constant<u32>(value);
      RTL_ENCODE_0R(Opcode::LOAD_IMM32, constant, HW_ANY(ssa_constant));
      break;
    }

    case ir::Type::Integer64: {
      const u64 value    = operand.value().u64_value;
      const u64 constant = make_constant<u64>(value);
      RTL_ENCODE_0R(Opcode::LOAD_IMM64, constant, HW_ANY(ssa_constant));
      break;
    }

    case ir::Type::Float32: {
      const f32 value                 = operand.value().f32_value;
      const u64 constant              = make_constant<f32>(value);
      const jit::RtlRegister ssa_temp = m_rtl.ssa_allocate(0);
      RTL_ENCODE_0R(Opcode::LOAD_IMM32, constant, HW_ANY(ssa_temp));
      RTL_ENCODE_1R(Opcode::FMOV32, 0, VEC_ANY(ssa_constant), HW_ANY(ssa_temp));
      break;
    }

    case ir::Type::Float64: {
      const f32 value                 = operand.value().f64_value;
      const u64 constant              = make_constant<f64>(value);
      const jit::RtlRegister ssa_temp = m_rtl.ssa_allocate(0);
      RTL_ENCODE_0R(Opcode::LOAD_IMM64, constant, HW_ANY(ssa_temp));
      RTL_ENCODE_1R(Opcode::FMOV64, 0, VEC_ANY(ssa_constant), HW_ANY(ssa_temp));
      break;
    }

    default: {
      std::stringstream what;
      what << "In get_rtl_ssa, unhandled ir::Type #" << (u32)operand.type();
      throw std::invalid_argument(what.str());
    }
  }

  return ssa_constant;
}

extern "C" {

// Used by the compilation logic to store to guest memory. We need this because some
// writes trigger logic in MMIO, texture, invalidation, etc.
// TODO : Re-work in a new tracing JIT system where we might be able to directly write
// to some places.
void
wrap_store(Guest *const guest,
           const u32 address,
           const size_t bytes,
           const ir::Constant value)
{
  guest->guest_store(address, bytes, value);
}

ir::Constant
wrap_load(Guest *const guest, const u32 address, const size_t bytes)
{
  return guest->guest_load(address, bytes);
}
}

}
}
}
