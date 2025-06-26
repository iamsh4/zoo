#include <iterator>
#include <functional>

#include "fox/jit/linear_register_allocator.h"
#include "amd64/amd64_opcodes.h"
#include "amd64/amd64_compiler.h"
#include "amd64/amd64_compiler.hh"

namespace fox {
namespace codegen {
namespace amd64 {

extern "C" {
static ir::Constant wrap_load(Guest *guest, u32 address, size_t bytes);
static void wrap_store(Guest *guest, u32 address, size_t bytes, ir::Constant value);
}

/*
 * Note:
 *
 * Boolean values are stored in registers / memory as 8-bit values. They're
 * treated as false if 0, any non-zero value is true. There's no eliding of
 * duplicate comparison operations (comparison produces bool, then compare
 * bool again before branching/moving).
 */

/*
 * Linux and Mac OS X both use the Sys V calling ABI, which means:
 *
 * Caller-saved: RAX, RCX, RDX, RSI, RDI, R8, R9, R10, R11
 * Callee-saved: RBX, RBP, R12, R13, R14, R15
 * Arguments: RDI, RSI, RDX, RCX, R8, R9
 * Return (64 bit): RAX
 */
static constexpr uint32_t abi_caller_saved =
  (1lu << (RAX)) | (1lu << (RCX)) | (1lu << (RDX)) | (1lu << (RSI)) | (1lu << (RDI)) |
  (1lu << (R8)) | (1lu << (R9)) | (1lu << (R10)) | (1lu << (R11));
static constexpr uint32_t abi_callee_saved =
  (1lu << (RBX)) | (1lu << (RBP)) | (1lu << (R12)) | (1lu << (R13)) | (1lu << (R14)) |
  (1lu << (R15));

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

jit::RegisterSet
_make_vector_set()
{
  jit::RegisterSet vector_set(VectorType, 16);
  vector_set.mark_allocated(jit::HwRegister(VectorType, Compiler::vec_scratch));

  /* Enable to test test under heavy register pressure. */
#if 0
  vector_set.mark_allocated(jit::HwRegister(VectorType, XMM0));
  vector_set.mark_allocated(jit::HwRegister(VectorType, XMM1));
  vector_set.mark_allocated(jit::HwRegister(VectorType, XMM2));
  vector_set.mark_allocated(jit::HwRegister(VectorType, XMM3));
  vector_set.mark_allocated(jit::HwRegister(VectorType, XMM4));
  vector_set.mark_allocated(jit::HwRegister(VectorType, XMM5));
  vector_set.mark_allocated(jit::HwRegister(VectorType, XMM6));
  vector_set.mark_allocated(jit::HwRegister(VectorType, XMM7));
  vector_set.mark_allocated(jit::HwRegister(VectorType, XMM8));
  vector_set.mark_allocated(jit::HwRegister(VectorType, XMM9));
#endif

  return vector_set;
}

static RegisterSize
ir_to_amd64_type(const ir::Type type)
{
  switch (type) {
    case ir::Type::Integer8:    return BYTE;
    case ir::Type::Integer16:   return WORD;
    case ir::Type::Integer32:   return DWORD;
    case ir::Type::Integer64:   return QWORD;
    case ir::Type::Float32:     return VECSS;
    case ir::Type::Float64:     return VECSD;
    case ir::Type::Bool:        return BYTE;
    case ir::Type::BranchLabel: return DWORD;
    case ir::Type::HostAddress: return QWORD;
    default: assert(false);
  }
}

static const jit::RegisterSet vector_set = _make_vector_set();

std::unique_ptr<Routine>
Compiler::compile(ir::ExecutionUnit &&source)
{
  m_source = std::move(source);
  m_ir_to_rtl.clear();
  m_labels.clear();
  m_uses_memory = false;
  m_debug = false;

  assert(m_register_address_cb);

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

/* Vector RTL register that may have any assignment. */
#define VEC_ANY(ssa)                                                                     \
  jit::RegisterAssignment                                                                \
  {                                                                                      \
    ssa, jit::HwRegister(VectorType)                                                     \
  }

/* Scalar RTL register that may have any assignment. */
#define HW_ANY(ssa)                                                                      \
  jit::RegisterAssignment                                                                \
  {                                                                                      \
    ssa, jit::HwRegister(ScalarType)                                                     \
  }

/* Scalar RTL register that must have a fixed assignment. */
#define HW_AT(ssa, hw)                                                                   \
  jit::RegisterAssignment                                                                \
  {                                                                                      \
    ssa, jit::HwRegister(ScalarType, hw)                                                 \
  }

/* Scalar RTL register that should be ignored by the register allocator and
 * has a fixed assignment. */
#define HW_X(hw)                                                                         \
  jit::RegisterAssignment                                                                \
  {                                                                                      \
    jit::RtlRegister(), jit::HwRegister(ScalarType, hw)                                  \
  }

/*
 * RTL_ENCODE_{[0-3]}{R/N}:
 *     Encodes RTL with 0-3 parameters and either a result (R) or no result
 *     (N). Variants with an (S) request a register allocation snapshot.
 *
 *     Most x86 instructions with 2 sources use the same register for the first
 *     source and the output. All variations with a result and at least 2
 *     sources are marked with the destructive flag.
 */

#define RTL_ENCODE_0N(opcode, details)                                                   \
  do {                                                                                   \
    jit::RtlInstruction entry(0u, 0u);                                                   \
    entry.op = (u16)opcode;                                                              \
    entry.data = details;                                                                \
    m_rtl.block(0).push_back(entry);                                                     \
  } while (0);

#define RTL_ENCODE_0R(opcode, details, out)                                              \
  do {                                                                                   \
    jit::RtlInstruction entry(0u, 1u);                                                   \
    entry.op = (u16)opcode;                                                              \
    entry.data = details;                                                                \
    entry.result(0) = out;                                                               \
    m_rtl.block(0).push_back(entry);                                                     \
  } while (0);

#define RTL_ENCODE_1N(opcode, details, in1)                                              \
  do {                                                                                   \
    jit::RtlInstruction entry(1u, 0u);                                                   \
    entry.op = (u16)opcode;                                                              \
    entry.data = details;                                                                \
    entry.source(0) = in1;                                                               \
    m_rtl.block(0).push_back(entry);                                                     \
  } while (0);

#define RTL_ENCODE_1R(opcode, details, out, in1)                                         \
  do {                                                                                   \
    jit::RtlInstruction entry(1u, 1u);                                                   \
    entry.op = (u16)opcode;                                                              \
    entry.data = details;                                                                \
    entry.result(0) = out;                                                               \
    entry.source(0) = in1;                                                               \
    m_rtl.block(0).push_back(entry);                                                     \
  } while (0);

#define RTL_ENCODE_1RS(opcode, details, out, in1)                                        \
  do {                                                                                   \
    jit::RtlInstruction entry(1u, 1u, { jit::RtlFlag::SaveState });                      \
    entry.op = (u16)opcode;                                                              \
    entry.data = details;                                                                \
    entry.result(0) = out;                                                               \
    entry.source(0) = in1;                                                               \
    m_rtl.block(0).push_back(entry);                                                     \
  } while (0);

#define RTL_ENCODE_2N(opcode, details, in1, in2)                                         \
  do {                                                                                   \
    jit::RtlInstruction entry(2u, 0u);                                                   \
    entry.op = (u16)opcode;                                                              \
    entry.data = details;                                                                \
    entry.source(0) = in1;                                                               \
    entry.source(1) = in2;                                                               \
    m_rtl.block(0).push_back(entry);                                                     \
  } while (0);

#define RTL_ENCODE_2R(opcode, details, out, in1, in2)                                    \
  do {                                                                                   \
    jit::RtlInstruction entry(2u, 1u, { jit::RtlFlag::Destructive });                    \
    entry.op = (u16)opcode;                                                              \
    entry.data = details;                                                                \
    entry.result(0) = out;                                                               \
    entry.source(0) = in1;                                                               \
    entry.source(1) = in2;                                                               \
    m_rtl.block(0).push_back(entry);                                                     \
  } while (0);

#define RTL_ENCODE_2RR(opcode, details, out1, out2, in1, in2)                            \
  do {                                                                                   \
    jit::RtlInstruction entry(2u, 2u, { jit::RtlFlag::Destructive });                    \
    entry.op = (u16)opcode;                                                              \
    entry.data = details;                                                                \
    entry.result(0) = out1;                                                              \
    entry.result(1) = out2;                                                              \
    entry.source(0) = in1;                                                               \
    entry.source(1) = in2;                                                               \
    m_rtl.block(0).push_back(entry);                                                     \
  } while (0);

#define RTL_ENCODE_2RS(opcode, details, out, in1, in2)                                   \
  do {                                                                                   \
    jit::RtlInstruction entry(                                                           \
      2u, 1u, { jit::RtlFlag::SaveState, jit::RtlFlag::Destructive });                   \
    entry.op = (u16)opcode;                                                              \
    entry.data = details;                                                                \
    entry.result(0) = out;                                                               \
    entry.source(0) = in1;                                                               \
    entry.source(1) = in2;                                                               \
    m_rtl.block(0).push_back(entry);                                                     \
  } while (0);

#define RTL_ENCODE_3N(opcode, details, in1, in2, in3)                                    \
  do {                                                                                   \
    jit::RtlInstruction entry(3u, 0u);                                                   \
    entry.op = (u16)opcode;                                                              \
    entry.data = details;                                                                \
    entry.source(0) = in1;                                                               \
    entry.source(1) = in2;                                                               \
    entry.source(2) = in3;                                                               \
    m_rtl.block(0).push_back(entry);                                                     \
  } while (0);

#define RTL_ENCODE_3R(opcode, details, out, in1, in2, in3)                               \
  do {                                                                                   \
    jit::RtlInstruction entry(3u, 1u, { jit::RtlFlag::Destructive });                    \
    entry.op = (u16)opcode;                                                              \
    entry.data = details;                                                                \
    entry.result(0) = out;                                                               \
    entry.source(0) = in1;                                                               \
    entry.source(1) = in2;                                                               \
    entry.source(2) = in3;                                                               \
    m_rtl.block(0).push_back(entry);                                                     \
  } while (0);

#define RTL_ENCODE_3RS(opcode, details, out, in1, in2, in3)                              \
  do {                                                                                   \
    jit::RtlInstruction entry(                                                           \
      3u, 1u, { jit::RtlFlag::SaveState, jit::RtlFlag::Destructive });                   \
    entry.op = (u16)opcode;                                                              \
    entry.data = details;                                                                \
    entry.result(0) = out;                                                               \
    entry.source(0) = in1;                                                               \
    entry.source(1) = in2;                                                               \
    entry.source(2) = in3;                                                               \
    m_rtl.block(0).push_back(entry);                                                     \
  } while (0);

#define RTL_ENCODE_4NS(opcode, details, in1, in2, in3, in4)                              \
  do {                                                                                   \
    jit::RtlInstruction entry(4u, 0u, { jit::RtlFlag::SaveState });                      \
    entry.op = (u16)opcode;                                                              \
    entry.data = details;                                                                \
    entry.source(0) = in1;                                                               \
    entry.source(1) = in2;                                                               \
    entry.source(2) = in3;                                                               \
    entry.source(3) = in4;                                                               \
    m_rtl.block(0).push_back(entry);                                                     \
  } while (0);

#define RTL_ENCODE_4RS(opcode, details, out, in1, in2, in3, in4)                         \
  do {                                                                                   \
    jit::RtlInstruction entry(                                                           \
      4u, 1u, { jit::RtlFlag::SaveState, jit::RtlFlag::Destructive });                   \
    entry.op = (u16)opcode;                                                              \
    entry.data = details;                                                                \
    entry.result(0) = out;                                                               \
    entry.source(0) = in1;                                                               \
    entry.source(1) = in2;                                                               \
    entry.source(2) = in3;                                                               \
    entry.source(3) = in4;                                                               \
    m_rtl.block(0).push_back(entry);                                                     \
  } while (0);

#define OPCODE(x) u16(Opcode::x)

void
Compiler::generate_rtl()
{
  /* Reset all state generated by this method. */
  m_rtl.clear();
  m_labels.clear();
  m_ir_to_rtl.clear();

  /* Allocate a label that will be placed directly before the restore + return
   * to caller. "exitif" IR instructions will target this. */
  const LabelId exit_label = allocate_label("exit");

  /* Allocate the single EBB used for all generated instructions. */
  /* TODO Support control flow once required logic is available in RTL. */
  jit::RtlProgram::BlockHandle block_handle = m_rtl.allocate_block("amd64_entry");
  jit::RtlInstructions &block = m_rtl.block(block_handle);
  assert(block_handle == 0);

  /*
   * The "signature" of the function we're generating looks like this:
   *     void fn(Guest *guest, void *memory_base, void *register_base);
   *
   * These are passed into the registers RDI, RSI, RDX. We want to move these
   * into their dedicated registers. Other than that, just ensure all registers
   * are available without corrupting our caller state. One more register is
   * reserved for scratch access and the rest go to the allocator.
   *
   * TODO If the routine doesn't need all registers, avoid saving the ones we
   *      didn't use. Extra credit: Make the registers that don't need saving
   *      the highest priority for the allocator.
   */

  block.append(OPCODE(PUSH_REGISTERS),
               jit::Value{ .u64_value = abi_callee_saved },
               {}, {});
  block.append(OPCODE(ALLOCATE_SPILL), {}, {});
  block.append(OPCODE(MOV_QWORD), { HW_X(gpr_guest_memory) }, { HW_X(RSI) });
  block.append(OPCODE(MOV_QWORD), { HW_X(gpr_guest_registers) }, { HW_X(RDX) });

  for (const ir::Instruction &current : m_source.instructions()) {
    switch (current.opcode()) {
      /* Read from a guest register in host memory. */
      case ir::Opcode::ReadGuest: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        switch (current.result(0).type()) {
          case ir::Type::Integer32:
            block.append(OPCODE(READ_GUEST_REGISTER32),
                         current.source(0).value(),
                         { HW_ANY(ssa_result) }, {});
            break;
          case ir::Type::Float32:
            block.append(OPCODE(READ_GUEST_REGISTER32),
                         current.source(0).value(),
                         { VEC_ANY(ssa_result) }, {});
            break;
          case ir::Type::Integer64:
            block.append(OPCODE(READ_GUEST_REGISTER64),
                         current.source(0).value(),
                         { HW_ANY(ssa_result) }, {});
            break;
          case ir::Type::Float64:
            block.append(OPCODE(READ_GUEST_REGISTER64),
                         current.source(0).value(),
                         { VEC_ANY(ssa_result) }, {});
            break;
          default:
            assert(false);
        }
        break;
      }

      /* Write to a guest register in host memory. */
      case ir::Opcode::WriteGuest: {
        const jit::RtlRegister ssa_value = get_rtl_ssa(current.source(1));
        switch (current.source(1).type()) {
          case ir::Type::Integer32:
            block.append(OPCODE(WRITE_GUEST_REGISTER32),
                         current.source(0).value(),
                         {}, { HW_ANY(ssa_value) });
            break;
          case ir::Type::Float32:
            block.append(OPCODE(WRITE_GUEST_REGISTER32),
                         current.source(0).value(),
                         {}, { VEC_ANY(ssa_value) });
            break;
          case ir::Type::Integer64:
            block.append(OPCODE(WRITE_GUEST_REGISTER64),
                         current.source(0).value(),
                         {}, { HW_ANY(ssa_value) });
            break;
          case ir::Type::Float64:
            block.append(OPCODE(WRITE_GUEST_REGISTER64),
                         current.source(0).value(),
                         {}, { VEC_ANY(ssa_value) });
            break;
          default:
            assert(false);
        }
        break;
      }

      /* Load a value from guest memory. This is done by making a function call
       * to one of our wrapper methods. */
      case ir::Opcode::Load: {
        m_uses_memory = true;

        unsigned bytes;
        bool is_float = false;
        switch (current.result(0).type()) {
          case ir::Type::Integer8:
            bytes = 1;
            break;
          case ir::Type::Integer16:
            bytes = 2;
            break;
          case ir::Type::Integer32:
            bytes = 4;
            break;
          case ir::Type::Integer64:
            bytes = 8;
            break;
          case ir::Type::Float32:
            is_float = true;
            bytes = 4;
            break;
          case ir::Type::Float64:
            is_float = true;
            bytes = 8;
            break;
          default:
            assert(false);
        }

        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_address = get_rtl_ssa(current.source(0));
        if (!is_float) {
          if (m_load_emitter) {
            block.append(OPCODE(LOAD_GUEST_MEMORY),
                         jit::Value{ .u64_value = bytes },
                         { HW_ANY(ssa_result) }, { HW_ANY(ssa_address) });
          } else {
            /* Result of load method call in RAX. */
            block.append(OPCODE(LOAD_GUEST_MEMORY),
                         jit::Value{ .u64_value = bytes },
                         { HW_AT(ssa_result, RAX) }, { HW_ANY(ssa_address) },
                         { jit::RtlFlag::SaveState });
          }
        } else {
          const jit::RtlRegister ssa_temp = m_rtl.ssa_allocate(ssa_result.type());
          if (m_load_emitter) {
            block.append(OPCODE(LOAD_GUEST_MEMORY),
                         jit::Value{ .u64_value = bytes },
                         { HW_ANY(ssa_temp) }, { HW_ANY(ssa_address) });
          } else {
            /* Result of load method call in RAX. */
            block.append(OPCODE(LOAD_GUEST_MEMORY),
                         jit::Value{ .u64_value = bytes },
                         { HW_AT(ssa_temp, RAX) }, { HW_ANY(ssa_address) },
                         { jit::RtlFlag::SaveState });
          }

          if (bytes == 4) {
            block.append(OPCODE(MOVD_DWORD),
                         { VEC_ANY(ssa_result) }, { HW_ANY(ssa_temp) });
          } else {
            block.append(OPCODE(MOVD_QWORD),
                         { VEC_ANY(ssa_result) }, { HW_ANY(ssa_temp) });
          }
        }
        break;
      }

      /* Store a value to guest memory. This is done by making a function call
       * to one of our wrapper methods. */
      /* TODO Expose an interface for guest CPUs to provide an optimized copies
       *      of this, e.g. inline a memory write. */
      case ir::Opcode::Store: {
        m_uses_memory = true;

        const jit::RtlRegister ssa_call_target = m_rtl.ssa_allocate(QWORD);
        const jit::RtlRegister ssa_address = get_rtl_ssa(current.source(0));
        const jit::RtlRegister ssa_value = get_rtl_ssa(current.source(1));
        const jit::RtlRegister ssa_bytes = m_rtl.ssa_allocate(QWORD);
        jit::RtlRegister ssa_temp;
        bool is_float = false;
        switch (current.type()) {
          case ir::Type::Integer8:
            RTL_ENCODE_0R(
              Opcode::LOAD_QWORD_IMM32, make_constant(1lu), HW_ANY(ssa_bytes));
            break;
          case ir::Type::Integer16:
            RTL_ENCODE_0R(
              Opcode::LOAD_QWORD_IMM32, make_constant(2lu), HW_ANY(ssa_bytes));
            break;
          case ir::Type::Integer32:
            RTL_ENCODE_0R(
              Opcode::LOAD_QWORD_IMM32, make_constant(4lu), HW_ANY(ssa_bytes));
            break;
          case ir::Type::Integer64:
            RTL_ENCODE_0R(
              Opcode::LOAD_QWORD_IMM32, make_constant(8lu), HW_ANY(ssa_bytes));
            break;
          case ir::Type::Float32:
            is_float = true;
            ssa_temp = m_rtl.ssa_allocate(DWORD);
            RTL_ENCODE_0R(
              Opcode::LOAD_QWORD_IMM64, make_constant(4lu), HW_ANY(ssa_bytes));
            RTL_ENCODE_1R(Opcode::MOVD_DWORD, 0, HW_ANY(ssa_temp), VEC_ANY(ssa_value));
            break;
          case ir::Type::Float64:
            is_float = true;
            ssa_temp = m_rtl.ssa_allocate(QWORD);
            RTL_ENCODE_0R(
              Opcode::LOAD_QWORD_IMM64, make_constant(8lu), HW_ANY(ssa_bytes));
            RTL_ENCODE_1R(Opcode::MOVD_QWORD, 0, HW_ANY(ssa_temp), VEC_ANY(ssa_value));
            break;
          default:
            assert(false);
        }

        /* During emit only the first source is used. The others are only
         * included as constraints to the register allocator. We can ignore
         * the destructive flag that gets added, since the address and result
         * are both in RAX anyway. */
        RTL_ENCODE_0R(Opcode::LOAD_QWORD_IMM64,
                      make_constant(u64(&wrap_store)),
                      HW_ANY(ssa_call_target));
        RTL_ENCODE_4NS(Opcode::CALL_FRAMED,
                       0,
                       HW_AT(ssa_call_target, RAX),
                       HW_AT(ssa_address, RSI),
                       HW_AT(ssa_bytes, RDX),
                       HW_AT(is_float ? ssa_temp : ssa_value, RCX));
        break;
      }

      case ir::Opcode::LogicalShiftRight: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(0));
        if (current.source(1).is_constant()) {
          switch (current.type()) {
            case ir::Type::Integer32: {
              const u8 constant = current.source(1).value().u8_value;
              RTL_ENCODE_1R(Opcode::SHIFTR_DWORD_IMM8,
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
        switch (current.type()) {
          case ir::Type::Integer8:
            RTL_ENCODE_2R(Opcode::SHIFTR_BYTE,
                          0,
                          HW_ANY(ssa_result),
                          HW_ANY(ssa_arg1),
                          HW_AT(ssa_arg2, RCX));
            break;
          case ir::Type::Integer16:
            RTL_ENCODE_2R(Opcode::SHIFTR_WORD,
                          0,
                          HW_ANY(ssa_result),
                          HW_ANY(ssa_arg1),
                          HW_AT(ssa_arg2, RCX));
            break;
          case ir::Type::Integer32:
            RTL_ENCODE_2R(Opcode::SHIFTR_DWORD,
                          0,
                          HW_ANY(ssa_result),
                          HW_ANY(ssa_arg1),
                          HW_AT(ssa_arg2, RCX));
            break;
          case ir::Type::Integer64:
            RTL_ENCODE_2R(Opcode::SHIFTR_QWORD,
                          0,
                          HW_ANY(ssa_result),
                          HW_ANY(ssa_arg1),
                          HW_AT(ssa_arg2, RCX));
            break;
          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::LogicalShiftLeft: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(0));
        if (current.source(1).is_constant()) {
          switch (current.type()) {
            case ir::Type::Integer32: {
              const u8 constant = current.source(1).value().u8_value;
              RTL_ENCODE_1R(Opcode::SHIFTL_DWORD_IMM8,
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
        switch (current.type()) {
          case ir::Type::Integer8:
            RTL_ENCODE_2R(Opcode::SHIFTL_BYTE,
                          0,
                          HW_ANY(ssa_result),
                          HW_ANY(ssa_arg1),
                          HW_AT(ssa_arg2, RCX));
            break;
          case ir::Type::Integer16:
            RTL_ENCODE_2R(Opcode::SHIFTL_WORD,
                          0,
                          HW_ANY(ssa_result),
                          HW_ANY(ssa_arg1),
                          HW_AT(ssa_arg2, RCX));
            break;
          case ir::Type::Integer32:
            RTL_ENCODE_2R(Opcode::SHIFTL_DWORD,
                          0,
                          HW_ANY(ssa_result),
                          HW_ANY(ssa_arg1),
                          HW_AT(ssa_arg2, RCX));
            break;
          case ir::Type::Integer64:
            RTL_ENCODE_2R(Opcode::SHIFTL_QWORD,
                          0,
                          HW_ANY(ssa_result),
                          HW_ANY(ssa_arg1),
                          HW_AT(ssa_arg2, RCX));
            break;
          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::ArithmeticShiftRight: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(0));
        if (current.source(1).is_constant()) {
          switch (current.type()) {
            case ir::Type::Integer32: {
              const u8 constant = current.source(1).value().u8_value;
              RTL_ENCODE_1R(Opcode::ASHIFTR_DWORD_IMM8,
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
        switch (current.type()) {
          case ir::Type::Integer8:
            RTL_ENCODE_2R(Opcode::ASHIFTR_BYTE,
                          0,
                          HW_ANY(ssa_result),
                          HW_ANY(ssa_arg1),
                          HW_AT(ssa_arg2, RCX));
            break;
          case ir::Type::Integer16:
            RTL_ENCODE_2R(Opcode::ASHIFTR_WORD,
                          0,
                          HW_ANY(ssa_result),
                          HW_ANY(ssa_arg1),
                          HW_AT(ssa_arg2, RCX));
            break;
          case ir::Type::Integer32:
            RTL_ENCODE_2R(Opcode::ASHIFTR_DWORD,
                          0,
                          HW_ANY(ssa_result),
                          HW_ANY(ssa_arg1),
                          HW_AT(ssa_arg2, RCX));
            break;
          case ir::Type::Integer64:
            RTL_ENCODE_2R(Opcode::ASHIFTR_QWORD,
                          0,
                          HW_ANY(ssa_result),
                          HW_ANY(ssa_arg1),
                          HW_AT(ssa_arg2, RCX));
            break;
          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::RotateRight: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(0));
        const ir::Operand count = current.source(1);
        if (count.is_constant() && count.value().u64_value == 1lu) {
          switch (current.type()) {
            case ir::Type::Integer8:
              RTL_ENCODE_1R(Opcode::ROR1_BYTE, 0, HW_ANY(ssa_result), HW_ANY(ssa_arg1));
              break;
            case ir::Type::Integer16:
              RTL_ENCODE_1R(Opcode::ROR1_WORD, 0, HW_ANY(ssa_result), HW_ANY(ssa_arg1));
              break;
            case ir::Type::Integer32:
              RTL_ENCODE_1R(Opcode::ROR1_DWORD, 0, HW_ANY(ssa_result), HW_ANY(ssa_arg1));
              break;
            case ir::Type::Integer64:
              RTL_ENCODE_1R(Opcode::ROR1_QWORD, 0, HW_ANY(ssa_result), HW_ANY(ssa_arg1));
              break;
            default:
              assert(false);
          }
        } else {
          const jit::RtlRegister ssa_arg2 = get_rtl_ssa(count);
          switch (current.type()) {
            case ir::Type::Integer8:
              RTL_ENCODE_2R(Opcode::ROR_BYTE,
                            0,
                            HW_ANY(ssa_result),
                            HW_ANY(ssa_arg1),
                            HW_AT(ssa_arg2, RCX));
              break;
            case ir::Type::Integer16:
              RTL_ENCODE_2R(Opcode::ROR_WORD,
                            0,
                            HW_ANY(ssa_result),
                            HW_ANY(ssa_arg1),
                            HW_AT(ssa_arg2, RCX));
              break;
            case ir::Type::Integer32:
              RTL_ENCODE_2R(Opcode::ROR_DWORD,
                            0,
                            HW_ANY(ssa_result),
                            HW_ANY(ssa_arg1),
                            HW_AT(ssa_arg2, RCX));
              break;
            case ir::Type::Integer64:
              RTL_ENCODE_2R(Opcode::ROR_QWORD,
                            0,
                            HW_ANY(ssa_result),
                            HW_ANY(ssa_arg1),
                            HW_AT(ssa_arg2, RCX));
              break;
            default:
              assert(false);
          }
        }
        break;
      }

      case ir::Opcode::RotateLeft: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(0));
        const ir::Operand count = current.source(1);
        if (count.is_constant() && count.value().u64_value == 1lu) {
          switch (current.type()) {
            case ir::Type::Integer8:
              RTL_ENCODE_1R(Opcode::ROL1_BYTE, 0, HW_ANY(ssa_result), HW_ANY(ssa_arg1));
              break;
            case ir::Type::Integer16:
              RTL_ENCODE_1R(Opcode::ROL1_WORD, 0, HW_ANY(ssa_result), HW_ANY(ssa_arg1));
              break;
            case ir::Type::Integer32:
              RTL_ENCODE_1R(Opcode::ROL1_DWORD, 0, HW_ANY(ssa_result), HW_ANY(ssa_arg1));
              break;
            case ir::Type::Integer64:
              RTL_ENCODE_1R(Opcode::ROL1_QWORD, 0, HW_ANY(ssa_result), HW_ANY(ssa_arg1));
              break;
            default:
              assert(false);
          }
        } else {
          const jit::RtlRegister ssa_arg2 = get_rtl_ssa(count);
          switch (current.type()) {
            case ir::Type::Integer8:
              RTL_ENCODE_2R(Opcode::ROL_BYTE,
                            0,
                            HW_ANY(ssa_result),
                            HW_ANY(ssa_arg1),
                            HW_AT(ssa_arg2, RCX));
              break;
            case ir::Type::Integer16:
              RTL_ENCODE_2R(Opcode::ROL_WORD,
                            0,
                            HW_ANY(ssa_result),
                            HW_ANY(ssa_arg1),
                            HW_AT(ssa_arg2, RCX));
              break;
            case ir::Type::Integer32:
              RTL_ENCODE_2R(Opcode::ROL_DWORD,
                            0,
                            HW_ANY(ssa_result),
                            HW_ANY(ssa_arg1),
                            HW_AT(ssa_arg2, RCX));
              break;
            case ir::Type::Integer64:
              RTL_ENCODE_2R(Opcode::ROL_QWORD,
                            0,
                            HW_ANY(ssa_result),
                            HW_ANY(ssa_arg1),
                            HW_AT(ssa_arg2, RCX));
              break;
            default:
              assert(false);
          }
        }
        break;
      }

      case ir::Opcode::And: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(0));
        if (current.source(1).is_constant()) {
          switch (current.type()) {
            case ir::Type::Integer32: {
              const u32 constant = current.source(1).value().u32_value;
              RTL_ENCODE_1R(Opcode::AND_DWORD_IMM32,
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
        switch (current.type()) {
          case ir::Type::Bool:
          case ir::Type::Integer8:
            RTL_ENCODE_2R(Opcode::AND_BYTE,
                          0,
                          HW_ANY(ssa_result),
                          HW_ANY(ssa_arg1),
                          HW_ANY(ssa_arg2));
            break;
          case ir::Type::Integer16:
            RTL_ENCODE_2R(Opcode::AND_WORD,
                          0,
                          HW_ANY(ssa_result),
                          HW_ANY(ssa_arg1),
                          HW_ANY(ssa_arg2));
            break;
          case ir::Type::Integer32:
            RTL_ENCODE_2R(Opcode::AND_DWORD,
                          0,
                          HW_ANY(ssa_result),
                          HW_ANY(ssa_arg1),
                          HW_ANY(ssa_arg2));
            break;
          case ir::Type::Integer64:
            RTL_ENCODE_2R(Opcode::AND_QWORD,
                          0,
                          HW_ANY(ssa_result),
                          HW_ANY(ssa_arg1),
                          HW_ANY(ssa_arg2));
            break;
          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::Or: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(0));
        if (current.source(1).is_constant()) {
          switch (current.type()) {
            case ir::Type::Integer32: {
              const u32 constant = current.source(1).value().u32_value;
              RTL_ENCODE_1R(Opcode::OR_DWORD_IMM32,
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
        switch (current.type()) {
          case ir::Type::Bool:
          case ir::Type::Integer8:
            RTL_ENCODE_2R(
              Opcode::OR_BYTE, 0, HW_ANY(ssa_result), HW_ANY(ssa_arg1), HW_ANY(ssa_arg2));
            break;
          case ir::Type::Integer16:
            RTL_ENCODE_2R(
              Opcode::OR_WORD, 0, HW_ANY(ssa_result), HW_ANY(ssa_arg1), HW_ANY(ssa_arg2));
            break;
          case ir::Type::Integer32:
            RTL_ENCODE_2R(Opcode::OR_DWORD,
                          0,
                          HW_ANY(ssa_result),
                          HW_ANY(ssa_arg1),
                          HW_ANY(ssa_arg2));
            break;
          case ir::Type::Integer64:
            RTL_ENCODE_2R(Opcode::OR_QWORD,
                          0,
                          HW_ANY(ssa_result),
                          HW_ANY(ssa_arg1),
                          HW_ANY(ssa_arg2));
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
        switch (current.type()) {
          case ir::Type::Bool:
          case ir::Type::Integer8:
            RTL_ENCODE_2R(Opcode::XOR_BYTE,
                          0,
                          HW_ANY(ssa_result),
                          HW_ANY(ssa_arg1),
                          HW_ANY(ssa_arg2));
            break;
          case ir::Type::Integer16:
            RTL_ENCODE_2R(Opcode::XOR_WORD,
                          0,
                          HW_ANY(ssa_result),
                          HW_ANY(ssa_arg1),
                          HW_ANY(ssa_arg2));
            break;
          case ir::Type::Integer32:
            RTL_ENCODE_2R(Opcode::XOR_DWORD,
                          0,
                          HW_ANY(ssa_result),
                          HW_ANY(ssa_arg1),
                          HW_ANY(ssa_arg2));
            break;
          case ir::Type::Integer64:
            RTL_ENCODE_2R(Opcode::XOR_QWORD,
                          0,
                          HW_ANY(ssa_result),
                          HW_ANY(ssa_arg1),
                          HW_ANY(ssa_arg2));
            break;
          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::Not: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(0));
        switch (current.type()) {
          case ir::Type::Integer8:
            RTL_ENCODE_1R(Opcode::NOT_BYTE, 0, HW_ANY(ssa_result), HW_ANY(ssa_arg1));
            break;
          case ir::Type::Integer16:
            RTL_ENCODE_1R(Opcode::NOT_WORD, 0, HW_ANY(ssa_result), HW_ANY(ssa_arg1));
            break;
          case ir::Type::Integer32:
            RTL_ENCODE_1R(Opcode::NOT_DWORD, 0, HW_ANY(ssa_result), HW_ANY(ssa_arg1));
            break;
          case ir::Type::Integer64:
            RTL_ENCODE_1R(Opcode::NOT_QWORD, 0, HW_ANY(ssa_result), HW_ANY(ssa_arg1));
            break;
          case ir::Type::Bool: {
            RTL_ENCODE_1R(Opcode::XOR_BYTE_IMM8,
                          make_constant(u8(1u)),
                          HW_ANY(ssa_result),
                          HW_ANY(ssa_arg1));
            break;
          }
          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::BitSetClear: {
        assert(current.source(2).is_constant());
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_input = get_rtl_ssa(current.source(0));
        const jit::RtlRegister ssa_control = get_rtl_ssa(current.source(1));
        const u8 position = current.source(2).value().u8_value;
        switch (current.type()) {
          case ir::Type::Integer32: {
            const jit::RtlRegister ssa_masked = m_rtl.ssa_allocate(DWORD);
            const jit::RtlRegister ssa_bit = m_rtl.ssa_allocate(DWORD);
            RTL_ENCODE_1R(Opcode::AND_DWORD_IMM32,
                          make_constant(~u32(1u << position)),
                          HW_ANY(ssa_masked),
                          HW_ANY(ssa_input));
            if (position != 0) {
              const jit::RtlRegister ssa_bit_temp = m_rtl.ssa_allocate(DWORD);
              RTL_ENCODE_1R(
                Opcode::ZEXTEND32_BYTE, 0, HW_ANY(ssa_bit_temp), HW_ANY(ssa_control));
              RTL_ENCODE_1R(Opcode::SHIFTL_DWORD_IMM8,
                            make_constant(u8(position)),
                            HW_ANY(ssa_bit),
                            HW_ANY(ssa_bit_temp));
            } else {
              RTL_ENCODE_1R(
                Opcode::ZEXTEND32_BYTE, 0, HW_ANY(ssa_bit), HW_ANY(ssa_control));
            }
            RTL_ENCODE_2R(Opcode::OR_DWORD,
                          0,
                          HW_ANY(ssa_result),
                          HW_ANY(ssa_masked),
                          HW_ANY(ssa_bit));
            break;
          }
          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::Add: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(0));
        if (current.source(1).is_constant()) {
          switch (current.type()) {
            case ir::Type::Integer32: {
              const u32 constant = current.source(1).value().u32_value;
              RTL_ENCODE_1R(Opcode::ADD_DWORD_IMM32,
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
        switch (current.type()) {
          case ir::Type::Integer8:
            RTL_ENCODE_2R(Opcode::ADD_BYTE,
                          0,
                          HW_ANY(ssa_result),
                          HW_ANY(ssa_arg1),
                          HW_ANY(ssa_arg2));
            break;
          case ir::Type::Integer16:
            RTL_ENCODE_2R(Opcode::ADD_WORD,
                          0,
                          HW_ANY(ssa_result),
                          HW_ANY(ssa_arg1),
                          HW_ANY(ssa_arg2));
            break;
          case ir::Type::Integer32:
            RTL_ENCODE_2R(Opcode::ADD_DWORD,
                          0,
                          HW_ANY(ssa_result),
                          HW_ANY(ssa_arg1),
                          HW_ANY(ssa_arg2));
            break;
          case ir::Type::Integer64:
            RTL_ENCODE_2R(Opcode::ADD_QWORD,
                          0,
                          HW_ANY(ssa_result),
                          HW_ANY(ssa_arg1),
                          HW_ANY(ssa_arg2));
            break;
          case ir::Type::Float32:
            RTL_ENCODE_2R(Opcode::ADD_VECSS,
                          0,
                          VEC_ANY(ssa_result),
                          VEC_ANY(ssa_arg1),
                          VEC_ANY(ssa_arg2));
            break;
          case ir::Type::Float64:
            RTL_ENCODE_2R(Opcode::ADD_VECSD,
                          0,
                          VEC_ANY(ssa_result),
                          VEC_ANY(ssa_arg1),
                          VEC_ANY(ssa_arg2));
            break;
          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::Subtract: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(0));

        bool constant_encoded = false;
        if (current.source(1).is_constant()) {
          switch (current.type()) {
            case ir::Type::Integer32: {
              const u32 constant = current.source(1).value().u32_value;
              RTL_ENCODE_1R(Opcode::SUB_DWORD_IMM32,
                            make_constant(constant),
                            HW_ANY(ssa_result),
                            HW_ANY(ssa_arg1));
              constant_encoded = true;
            }
            default:
              break;
          }
        }

        if (!constant_encoded) {
          const jit::RtlRegister ssa_arg2 = get_rtl_ssa(current.source(1));
          switch (current.type()) {
            case ir::Type::Integer8:
              RTL_ENCODE_2R(Opcode::SUB_BYTE,
                            0,
                            HW_ANY(ssa_result),
                            HW_ANY(ssa_arg1),
                            HW_ANY(ssa_arg2));
              break;
            case ir::Type::Integer16:
              RTL_ENCODE_2R(Opcode::SUB_WORD,
                            0,
                            HW_ANY(ssa_result),
                            HW_ANY(ssa_arg1),
                            HW_ANY(ssa_arg2));
              break;
            case ir::Type::Integer32:
              RTL_ENCODE_2R(Opcode::SUB_DWORD,
                            0,
                            HW_ANY(ssa_result),
                            HW_ANY(ssa_arg1),
                            HW_ANY(ssa_arg2));
              break;
            case ir::Type::Integer64:
              RTL_ENCODE_2R(Opcode::SUB_QWORD,
                            0,
                            HW_ANY(ssa_result),
                            HW_ANY(ssa_arg1),
                            HW_ANY(ssa_arg2));
              break;
            case ir::Type::Float32:
              RTL_ENCODE_2R(Opcode::SUB_VECSS,
                            0,
                            VEC_ANY(ssa_result),
                            VEC_ANY(ssa_arg1),
                            VEC_ANY(ssa_arg2));
              break;
            case ir::Type::Float64:
              RTL_ENCODE_2R(Opcode::SUB_VECSD,
                            0,
                            VEC_ANY(ssa_result),
                            VEC_ANY(ssa_arg1),
                            VEC_ANY(ssa_arg2));
              break;
            default:
              assert(false);
          }
        }
        break;
      }

      case ir::Opcode::Multiply: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(0));
        const jit::RtlRegister ssa_arg2 = get_rtl_ssa(current.source(1));
        switch (current.type()) {
          case ir::Type::Integer8:
            assert(false); /* There's no 'dst, src' encoding for IMUL_BYTE */
            RTL_ENCODE_2R(Opcode::IMUL_BYTE,
                          0,
                          HW_ANY(ssa_result),
                          HW_ANY(ssa_arg1),
                          HW_ANY(ssa_arg2));
            break;
          case ir::Type::Integer16:
            RTL_ENCODE_2R(Opcode::IMUL_WORD,
                          0,
                          HW_ANY(ssa_result),
                          HW_ANY(ssa_arg1),
                          HW_ANY(ssa_arg2));
            break;
          case ir::Type::Integer32:
            RTL_ENCODE_2R(Opcode::IMUL_DWORD,
                          0,
                          HW_ANY(ssa_result),
                          HW_ANY(ssa_arg1),
                          HW_ANY(ssa_arg2));
            break;
          case ir::Type::Integer64:
            RTL_ENCODE_2R(Opcode::IMUL_QWORD,
                          0,
                          HW_ANY(ssa_result),
                          HW_ANY(ssa_arg1),
                          HW_ANY(ssa_arg2));
            break;
          case ir::Type::Float32:
            RTL_ENCODE_2R(Opcode::MUL_VECSS,
                          0,
                          VEC_ANY(ssa_result),
                          VEC_ANY(ssa_arg1),
                          VEC_ANY(ssa_arg2));
            break;
          case ir::Type::Float64:
            RTL_ENCODE_2R(Opcode::MUL_VECSD,
                          0,
                          VEC_ANY(ssa_result),
                          VEC_ANY(ssa_arg1),
                          VEC_ANY(ssa_arg2));
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
        switch (current.type()) {
          case ir::Type::Integer8:
            RTL_ENCODE_2R(Opcode::MUL_BYTE,
                          0,
                          HW_AT(ssa_result, RAX),
                          HW_ANY(ssa_arg1),
                          HW_AT(ssa_arg2, RAX));
            break;
          case ir::Type::Integer16:
            RTL_ENCODE_2RR(Opcode::MUL_WORD,
                           0,
                           HW_AT(ssa_result, RAX),
                           HW_AT(m_rtl.ssa_allocate(WORD), RDX),
                           HW_ANY(ssa_arg1),
                           HW_AT(ssa_arg2, RAX));
            break;
          case ir::Type::Integer32:
            RTL_ENCODE_2RR(Opcode::MUL_DWORD,
                           0,
                           HW_AT(ssa_result, RAX),
                           HW_AT(m_rtl.ssa_allocate(DWORD), RDX),
                           HW_ANY(ssa_arg1),
                           HW_AT(ssa_arg2, RAX));
            break;
          case ir::Type::Integer64:
            RTL_ENCODE_2RR(Opcode::MUL_QWORD,
                           0,
                           HW_AT(ssa_result, RAX),
                           HW_AT(m_rtl.ssa_allocate(QWORD), RDX),
                           HW_ANY(ssa_arg1),
                           HW_AT(ssa_arg2, RAX));
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
        switch (current.type()) {
          case ir::Type::Float32:
            RTL_ENCODE_2R(Opcode::DIV_VECSS,
                          0,
                          VEC_ANY(ssa_result),
                          VEC_ANY(ssa_arg1),
                          VEC_ANY(ssa_arg2));
            break;
          case ir::Type::Float64:
            RTL_ENCODE_2R(Opcode::DIV_VECSD,
                          0,
                          VEC_ANY(ssa_result),
                          VEC_ANY(ssa_arg1),
                          VEC_ANY(ssa_arg2));
            break;
          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::SquareRoot: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(0));
        switch (current.type()) {
          case ir::Type::Float32:
            RTL_ENCODE_1R(Opcode::SQRT_VECSS, 0, VEC_ANY(ssa_result), VEC_ANY(ssa_arg1));
            break;
          case ir::Type::Float64:
            RTL_ENCODE_1R(Opcode::SQRT_VECSD, 0, VEC_ANY(ssa_result), VEC_ANY(ssa_arg1));
            break;
          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::Call: {
        assert(current.source(0).is_constant());
        assert(current.source(0).type() == ir::Type::HostAddress);

        const jit::RtlRegister ssa_call_target = m_rtl.ssa_allocate(QWORD);
        RTL_ENCODE_0R(Opcode::LOAD_QWORD_IMM64,
                      make_constant(u64(current.source(0).value().hostptr_value)),
                      HW_ANY(ssa_call_target));

        /* The first argument (argument 0) is implicit. The register used for
         * passing argument 0 on amd64 is always set to the guest instance.
         * The return value is assumed but potentially unused / throwaway. */
        const bool has_result = current.result_count() > 0;
        assert(current.result_count() <= 1);

        const jit::RtlRegister ssa_result =
          has_result ? make_rtl_ssa(current.result(0)) : m_rtl.ssa_allocate(QWORD);

        /* The argument count does not affect code generation, since the RTL
         * register assignments are responsible for handling argument setup. */
        if (current.source_count() == 1) {
          RTL_ENCODE_1RS(Opcode::CALL_FRAMED,
                         0,
                         HW_AT(ssa_result, RAX),
                         HW_AT(ssa_call_target, RAX));
        } else if (current.source_count() == 2) {
          const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(1));
          RTL_ENCODE_2RS(Opcode::CALL_FRAMED,
                         0,
                         HW_AT(ssa_result, RAX),
                         HW_AT(ssa_call_target, RAX),
                         HW_AT(ssa_arg1, RSI));
        } else if (current.source_count() == 3) {
          const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(1));
          const jit::RtlRegister ssa_arg2 = get_rtl_ssa(current.source(2));
          RTL_ENCODE_3RS(Opcode::CALL_FRAMED,
                         0,
                         HW_AT(ssa_result, RAX),
                         HW_AT(ssa_call_target, RAX),
                         HW_AT(ssa_arg1, RSI),
                         HW_AT(ssa_arg2, RDX));
        } else {
          assert(false);
        }
        break;
      }

      case ir::Opcode::Extend32: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg = get_rtl_ssa(current.source(0));
        switch (current.type()) {
          case ir::Type::Integer8:
            RTL_ENCODE_1R(Opcode::EXTEND32_BYTE, 0, HW_ANY(ssa_result), HW_ANY(ssa_arg));
            break;
          case ir::Type::Integer16:
            RTL_ENCODE_1R(Opcode::EXTEND32_WORD, 0, HW_ANY(ssa_result), HW_ANY(ssa_arg));
            break;
          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::Extend64: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg = get_rtl_ssa(current.source(0));
        switch (current.type()) {
          case ir::Type::Integer8:
            RTL_ENCODE_1R(Opcode::EXTEND64_BYTE, 0, HW_ANY(ssa_result), HW_ANY(ssa_arg));
            break;
          case ir::Type::Integer16:
            RTL_ENCODE_1R(Opcode::EXTEND64_WORD, 0, HW_ANY(ssa_result), HW_ANY(ssa_arg));
            break;
          case ir::Type::Integer32:
            RTL_ENCODE_1R(Opcode::EXTEND64_DWORD, 0, HW_ANY(ssa_result), HW_ANY(ssa_arg));
            break;
          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::BitCast: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg = get_rtl_ssa(current.source(0));
        const ir::Type from = current.source(0).type();
        switch (current.type()) {
          case ir::Type::Integer8:
            RTL_ENCODE_1R(Opcode::ZEXTEND32_BYTE, 0, HW_ANY(ssa_result), HW_ANY(ssa_arg));
            break;
          case ir::Type::Integer16:
            if (from == ir::Type::Integer8) {
              RTL_ENCODE_1R(
                Opcode::ZEXTEND32_BYTE, 0, HW_ANY(ssa_result), HW_ANY(ssa_arg));
            } else if (from == ir::Type::Integer16) {
              /* No-op */
            } else if (from == ir::Type::Integer32) {
              RTL_ENCODE_1R(
                Opcode::ZEXTEND32_WORD, 0, HW_ANY(ssa_result), HW_ANY(ssa_arg));
            } else {
              assert(false);
            }
            break;
          case ir::Type::Integer32:
            if (from == ir::Type::Integer8) {
              RTL_ENCODE_1R(
                Opcode::ZEXTEND32_BYTE, 0, HW_ANY(ssa_result), HW_ANY(ssa_arg));
            } else if (from == ir::Type::Integer16) {
              RTL_ENCODE_1R(
                Opcode::ZEXTEND32_WORD, 0, HW_ANY(ssa_result), HW_ANY(ssa_arg));
            } else if (from == ir::Type::Integer32) {
              /* No-op */
            } else if (from == ir::Type::Integer64) {
              RTL_ENCODE_1R(
                Opcode::ZEXTEND64_DWORD, 0, HW_ANY(ssa_result), HW_ANY(ssa_arg));
            } else if (from == ir::Type::Float32) {
              RTL_ENCODE_1R(Opcode::MOVD_DWORD, 0, HW_ANY(ssa_result), VEC_ANY(ssa_arg));
            } else {
              assert(false);
            }
            break;
          case ir::Type::Integer64:
            if (from == ir::Type::Integer8) {
              RTL_ENCODE_1R(
                Opcode::ZEXTEND64_BYTE, 0, HW_ANY(ssa_result), HW_ANY(ssa_arg));
            } else if (from == ir::Type::Integer16) {
              RTL_ENCODE_1R(
                Opcode::ZEXTEND64_WORD, 0, HW_ANY(ssa_result), HW_ANY(ssa_arg));
            } else if (from == ir::Type::Integer32) {
              RTL_ENCODE_1R(
                Opcode::ZEXTEND64_DWORD, 0, HW_ANY(ssa_result), HW_ANY(ssa_arg));
            } else if (from == ir::Type::Integer64) {
              /* No-op */
            } else if (from == ir::Type::Float64) {
              RTL_ENCODE_1R(Opcode::MOVD_QWORD, 0, HW_ANY(ssa_result), VEC_ANY(ssa_arg));
            } else {
              assert(false);
            }
            break;
          case ir::Type::Float32:
            if (from == ir::Type::Integer32) {
              RTL_ENCODE_1R(Opcode::MOVD_DWORD, 0, VEC_ANY(ssa_result), HW_ANY(ssa_arg));
            } else if (from == ir::Type::Float32) {
              /* No-op */
            } else {
              assert(false);
            }
            break;
          case ir::Type::Float64:
            if (from == ir::Type::Integer64) {
              RTL_ENCODE_1R(Opcode::MOVD_QWORD, 0, VEC_ANY(ssa_result), HW_ANY(ssa_arg));
            } else if (from == ir::Type::Float64) {
              /* No-op */
            } else {
              assert(false);
            }
            break;
          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::CastFloatInt: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg = get_rtl_ssa(current.source(0));
        if (current.source(0).type() == ir::Type::Float32) {
          switch (current.result(0).type()) {
            case ir::Type::Integer32:
              RTL_ENCODE_1R(Opcode::CVT_VECSS_I32, 0, HW_ANY(ssa_result), VEC_ANY(ssa_arg));
              break;
            case ir::Type::Integer64:
              RTL_ENCODE_1R(Opcode::CVT_VECSS_I64, 0, HW_ANY(ssa_result), VEC_ANY(ssa_arg));
              break;
            default:
              assert(false);
          }
        } else {
          assert(current.source(0).type() == ir::Type::Float64);
          switch (current.result(0).type()) {
            case ir::Type::Integer32:
              RTL_ENCODE_1R(Opcode::CVT_VECSD_I32, 0, HW_ANY(ssa_result), VEC_ANY(ssa_arg));
              break;
            case ir::Type::Integer64:
              RTL_ENCODE_1R(Opcode::CVT_VECSD_I64, 0, HW_ANY(ssa_result), VEC_ANY(ssa_arg));
              break;
            default:
              assert(false);
          }
        }
        break;
      }

      case ir::Opcode::Test: {
        /* TODO test(x, const) and test(const, x) are both possible. We don't
         *      optimize for the second case yet as it is less common. */
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(0));
        bool constant_encoded = false;
        if (current.source(1).is_constant()) {
          switch (current.type()) {
            case ir::Type::Integer32: {
              const u32 constant = current.source(1).value().u32_value;
              RTL_ENCODE_1N(
                Opcode::TEST_DWORD_IMM32, make_constant(constant), HW_ANY(ssa_arg1));
              constant_encoded = true;
              break;
            }
            default:
              break;
          }
        }

        if (!constant_encoded) {
          const jit::RtlRegister ssa_arg2 = get_rtl_ssa(current.source(1));
          switch (current.type()) {
            case ir::Type::Integer8:
              RTL_ENCODE_2N(Opcode::TEST_BYTE, 0, HW_ANY(ssa_arg1), HW_ANY(ssa_arg2));
              break;
            case ir::Type::Integer16:
              RTL_ENCODE_2N(Opcode::TEST_WORD, 0, HW_ANY(ssa_arg1), HW_ANY(ssa_arg2));
              break;
            case ir::Type::Integer32:
              RTL_ENCODE_2N(Opcode::TEST_DWORD, 0, HW_ANY(ssa_arg1), HW_ANY(ssa_arg2));
              break;
            case ir::Type::Integer64:
              RTL_ENCODE_2N(Opcode::TEST_QWORD, 0, HW_ANY(ssa_arg1), HW_ANY(ssa_arg2));
              break;
            default:
              assert(false);
          }
        }

        RTL_ENCODE_0R(Opcode::SETNZ, 0, HW_ANY(ssa_result));
        break;
      }

      case ir::Opcode::Compare_eq:
      case ir::Opcode::Compare_lt:
      case ir::Opcode::Compare_lte:
      case ir::Opcode::Compare_ult:
      case ir::Opcode::Compare_ulte: {
        const bool source0_is_constant = current.source(0).is_constant();
        const bool source1_is_constant = current.source(1).is_constant();
        bool constant_encoded = false;
        if (source0_is_constant ^ source1_is_constant) {
          switch (current.type()) {
            case ir::Type::Integer32: {
              if (source0_is_constant) {
                /* TODO Needs its own opcode. */
              } else {
                const jit::RtlRegister ssa_arg = get_rtl_ssa(current.source(0));
                const u32 constant =
                  current.source(1).value().u32_value;
                RTL_ENCODE_1N(
                  Opcode::CMP_DWORD_IMM32, make_constant(constant), HW_ANY(ssa_arg));
                constant_encoded = true;
              }
            }
            default:
              break;
          }
        }

        if (!constant_encoded) {
          const jit::RtlRegister ssa_arg1 = get_rtl_ssa(current.source(0));
          const jit::RtlRegister ssa_arg2 = get_rtl_ssa(current.source(1));
          switch (current.type()) {
            case ir::Type::Bool:
              assert(current.opcode() == ir::Opcode::Compare_eq);
            case ir::Type::Integer8:
              RTL_ENCODE_2N(Opcode::CMP_BYTE, 0, HW_ANY(ssa_arg1), HW_ANY(ssa_arg2));
              break;
            case ir::Type::Integer16:
              RTL_ENCODE_2N(Opcode::CMP_WORD, 0, HW_ANY(ssa_arg1), HW_ANY(ssa_arg2));
              break;
            case ir::Type::Integer32:
              RTL_ENCODE_2N(Opcode::CMP_DWORD, 0, HW_ANY(ssa_arg1), HW_ANY(ssa_arg2));
              break;
            case ir::Type::Integer64:
              RTL_ENCODE_2N(Opcode::CMP_QWORD, 0, HW_ANY(ssa_arg1), HW_ANY(ssa_arg2));
              break;
            default:
              m_source.debug_print();
              assert(false); /* Float not implemented */
          }
        }

        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        switch (current.opcode()) {
          case ir::Opcode::Compare_eq:
            RTL_ENCODE_0R(Opcode::SETZ, 0, HW_ANY(ssa_result));
            break;
          case ir::Opcode::Compare_lt:
            RTL_ENCODE_0R(Opcode::SETL, 0, HW_ANY(ssa_result));
            break;
          case ir::Opcode::Compare_lte:
            RTL_ENCODE_0R(Opcode::SETLE, 0, HW_ANY(ssa_result));
            break;
          case ir::Opcode::Compare_ult:
            RTL_ENCODE_0R(Opcode::SETB, 0, HW_ANY(ssa_result));
            break;
          case ir::Opcode::Compare_ulte:
            RTL_ENCODE_0R(Opcode::SETBE, 0, HW_ANY(ssa_result));
            break;
          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::Select: {
        const jit::RtlRegister ssa_result = make_rtl_ssa(current.result(0));
        const jit::RtlRegister ssa_decision = get_rtl_ssa(current.source(0));
        const bool constant_results =
          current.source(1).is_constant() && current.source(2).is_constant();
        if (constant_results && current.type() == ir::Type::Integer32 /* XXX */) {
          /* If the output is a 0/1, the boolean encoding can be direclty moved
           * to the result. */
          const u32 false_value = current.source(1).value().u32_value;
          const u32 true_value = current.source(2).value().u32_value;
          if (false_value == 0u && true_value == 1u) {
            RTL_ENCODE_1R(
              Opcode::ZEXTEND32_BYTE, 0, HW_ANY(ssa_result), HW_ANY(ssa_decision));
            continue;
          }
        }

        const jit::RtlRegister ssa_false = get_rtl_ssa(current.source(1));
        const jit::RtlRegister ssa_true = get_rtl_ssa(current.source(2));
        RTL_ENCODE_2N(Opcode::TEST_BYTE, 0, HW_ANY(ssa_decision), HW_ANY(ssa_decision));
        switch (current.type()) {
          case ir::Type::Integer8:
            assert(false); /* Not implemented. */
            break;
          case ir::Type::Integer16:
            RTL_ENCODE_2R(Opcode::CMOVNZ_WORD,
                          0,
                          HW_ANY(ssa_result),
                          HW_ANY(ssa_false),
                          HW_ANY(ssa_true));
            break;
          case ir::Type::Integer32:
            RTL_ENCODE_2R(Opcode::CMOVNZ_DWORD,
                          0,
                          HW_ANY(ssa_result),
                          HW_ANY(ssa_false),
                          HW_ANY(ssa_true));
            break;
          case ir::Type::Integer64:
            RTL_ENCODE_2R(Opcode::CMOVNZ_QWORD,
                          0,
                          HW_ANY(ssa_result),
                          HW_ANY(ssa_false),
                          HW_ANY(ssa_true));
            break;
          default:
            assert(false);
        }
        break;
      }

      case ir::Opcode::Exit: {
        const ir::Operand decision = current.source(0);
        const ir::Operand returned = current.source(1);
        const u64 returned_value = returned.value().u64_value;
        assert(returned_value < (1lu << 32u)); /* XXX */
        if (decision.is_constant() && decision.value().bool_value) {
          RTL_ENCODE_0R(
            Opcode::LOAD_QWORD_IMM32, make_constant(returned_value), HW_X(gpr_scratch));
          RTL_ENCODE_0N(Opcode::JMP, exit_label);
        } else {
          const jit::RtlRegister ssa_bool = get_rtl_ssa(decision);
          RTL_ENCODE_2N(Opcode::TEST_BYTE, 0, HW_ANY(ssa_bool), HW_ANY(ssa_bool));
          RTL_ENCODE_0R(
            Opcode::LOAD_QWORD_IMM32, make_constant(returned_value), HW_X(gpr_scratch));
          RTL_ENCODE_0N(Opcode::JNZ, exit_label);
        }
        break;
      }

      default:
        /* Not implemented.. */
        printf("IR opcode not implemented: %u\n", (unsigned)current.opcode());
        assert(false);
        break;
    }
  }

  RTL_ENCODE_0N(Opcode::LABEL, exit_label);
  RTL_ENCODE_0N(Opcode::FREE_SPILL, 0);
  RTL_ENCODE_1R(Opcode::MOV_QWORD, 0, HW_X(RAX), HW_X(gpr_scratch));
  RTL_ENCODE_0N(Opcode::POP_REGISTERS, abi_callee_saved);
  RTL_ENCODE_0N(Opcode::RET, 0);
}

void
Compiler::assign_registers()
{
  jit::RegisterSet scalar_set(ScalarType, 16);
  scalar_set.mark_allocated(jit::HwRegister(ScalarType, Compiler::gpr_guest));
  scalar_set.mark_allocated(jit::HwRegister(ScalarType, Compiler::gpr_guest_registers));
  scalar_set.mark_allocated(jit::HwRegister(ScalarType, RBP));
  scalar_set.mark_allocated(jit::HwRegister(ScalarType, RSP));
  scalar_set.mark_allocated(jit::HwRegister(ScalarType, Compiler::gpr_scratch));
  if (m_uses_memory) {
    scalar_set.mark_allocated(jit::HwRegister(ScalarType, Compiler::gpr_guest_memory));
  }

  /* Enable to test test under heavy register pressure. */
#if 0
  scalar_set.mark_allocated(jit::HwRegister(ScalarType, R9));
  scalar_set.mark_allocated(jit::HwRegister(ScalarType, R10));
  scalar_set.mark_allocated(jit::HwRegister(ScalarType, R11));
  scalar_set.mark_allocated(jit::HwRegister(ScalarType, R13));
  scalar_set.mark_allocated(jit::HwRegister(ScalarType, R14));
  scalar_set.mark_allocated(jit::HwRegister(ScalarType, R15));
#endif

  jit::LinearAllocator allocator;
  allocator.define_register_type(scalar_set);
  allocator.define_register_type(vector_set);
  m_rtl = allocator.execute(std::move(m_rtl));

#if 0
  //m_rtl.debug_print();
#endif
}

/*!
 * @brief Helper to create a RegMem<> representing an RTL hardware assignment.
 *        The assignment may be a register or memory spill location.
 */
template<RegisterSize s>
static RegMem<s>
assignment(jit::HwRegister hw)
{
  if (hw.is_spill()) {
    return Address<s>(RBP, hw.spill_index() * sizeof(u64));
  } else {
    return Register<s>(hw);
  }
}

/*!
 * @brief Helper to create a RegMemVector<> representing an RTL hardware
 *        assignment. The assignment may be a register or memory spill location.
 */
template<RegisterSize s>
static RegMemVector<s>
assignmentvec(jit::HwRegister hw)
{
  if (hw.is_spill()) {
    return Address<s>(RBP, hw.spill_index() * sizeof(u64));
  } else {
    return Vector<s>(hw);
  }
}

/*
 * Emit a non-destructive operation. The result must be a register and the
 * source can be a register or in memory.
 */
template<RegisterSize s, RegisterSize n = s>
void
emitN(Assembler *const out,
      void (Assembler::*emitter)(Register<s>, RegMem<n>),
      const jit::RtlInstruction &rtl)
{
  assert(rtl.source_count == 1);
  assert(rtl.result_count == 1);

  /* If the first operand is a register, run operation against it directly. */
  const RegMem<n> operand1 = assignment<n>(rtl.source(0).hw);
  const jit::HwRegister hw0 = rtl.result(0).hw;
  if (!hw0.is_spill()) {
    std::invoke(emitter, out, Register<s>(hw0), operand1);
    return;
  }

  /* First operand is spilled. Operate on scratch register then write to
   * memory. */
  std::invoke(emitter, out, Register<s>(Compiler::gpr_scratch), operand1);
  out->mov(assignment<s>(hw0), Register<s>(Compiler::gpr_scratch));
}

/*
 * Destructive operation where the second operand of the emitted instruction is
 * a fixed register, specified by F.
 */
template<RegisterSize s, typename F>
void
emitF(Assembler *const out,
      void (Assembler::*emitter)(RegMem<s>, F),
      const jit::RtlInstruction &rtl)
{
  assert(rtl.source_count == 2);
  assert(rtl.source(1).hw.index() == F().encoding());
  assert(rtl.result_count == 1);

  /* If source / destination weren't merged, either move the source to the
   * destination now or perform the operation in a scratch register before
   * before moving it to the final destination. A scratch register is used if
   * the destination aliases the other source value or is a memory spill. */
  bool use_scratch = false;
  if (rtl.source(0).hw != rtl.result(0).hw) {
    if (!rtl.result(0).hw.is_spill() && rtl.result(0).hw != rtl.source(1).hw) {
      out->mov(Register<s>(rtl.result(0).hw), assignment<s>(rtl.source(0).hw));
    } else {
      out->mov(Register<s>(Compiler::gpr_scratch), assignment<s>(rtl.source(0).hw));
      use_scratch = true;
    }
  }

  const RegMem<s> operand0 = assignment<s>(rtl.result(0).hw);
  if (use_scratch) {
    std::invoke(emitter, out, Register<s>(Compiler::gpr_scratch), F());
    out->mov(operand0, Register<s>(Compiler::gpr_scratch));
  } else {
    std::invoke(emitter, out, operand0, F());
  }
}

/*
 * Destructive operation with only one operand, which may be a memory location.
 */
template<RegisterSize s>
void
emitS(Assembler *const out,
      void (Assembler::*emitter)(RegMem<s>),
      const jit::RtlInstruction &rtl)
{
  assert(rtl.source_count == 1);
  assert(rtl.result_count == 1);

  /* If the register allocator could not merge the source and destination,
   * manually copy the source to the result before the target instruction. */
  if (rtl.result(0).hw != rtl.source(0).hw) {
    if (rtl.result(0).hw.is_spill() && rtl.source(0).hw.is_spill()) {
      out->mov(Register<s>(Compiler::gpr_scratch), assignment<s>(rtl.source(0).hw));
      out->mov(assignment<s>(rtl.result(0).hw), Register<s>(Compiler::gpr_scratch));
    } else if (rtl.result(0).hw.is_spill()) {
      out->mov(assignment<s>(rtl.result(0).hw), Register<s>(rtl.source(0).hw));
    } else if (rtl.source(0).hw.is_spill()) {
      out->mov(Register<s>(rtl.result(0).hw), assignment<s>(rtl.source(0).hw));
    } else {
      out->mov(Register<s>(rtl.result(0).hw), Register<s>(rtl.source(0).hw));
    }
  }

  std::invoke(emitter, out, assignment<s>(rtl.result(0).hw));
}

static void
movT(Assembler *const out,
     const RegisterSize size,
     const jit::HwRegister operand0,
     const jit::HwRegister operand1)
{
  switch (size) {
    case BYTE:
      out->mov(assignment<BYTE>(operand0), assignment<BYTE>(operand1));
      break;
    case WORD:
      out->mov(assignment<WORD>(operand0), assignment<WORD>(operand1));
      break;
    case DWORD:
      out->mov(assignment<DWORD>(operand0), assignment<DWORD>(operand1));
      break;
    case QWORD:
      out->mov(assignment<QWORD>(operand0), assignment<QWORD>(operand1));
      break;
    default:
      assert(false);
  }
}

static void
movdT(Assembler *const out,
      const RegisterSize size,
      const jit::HwRegister operand0,
      const jit::HwRegister operand1)
{
  if (operand0.is_spill()) {
    switch (size) {
      case VECPS:
      case VECSS:
        out->movups(assignmentvec<XMM>(operand0), Vector<XMM>(operand1));
        break;
      case VECPD:
      case VECSD:
        out->movupd(assignmentvec<XMM>(operand0), Vector<XMM>(operand1));
        break;
      default:
        assert(false);
    }
  } else {
    switch (size) {
      case VECPS:
      case VECSS:
        out->movups(Vector<XMM>(operand0), assignmentvec<XMM>(operand1));
        break;
      case VECPD:
      case VECSD:
        out->movupd(Vector<XMM>(operand0), assignmentvec<XMM>(operand1));
        break;
      default:
        assert(false);
    }
  }
}

template<RegisterSize s>
static void
emitT(Assembler *const out,
      void (Assembler::*emitter)(RegMem<s>, RegMem<s>),
      const jit::HwRegister operand0,
      const jit::HwRegister operand1)
{
  std::invoke(emitter, out, assignment<s>(operand0), assignment<s>(operand1));
}

template<RegisterSize s>
static void
emitvecT(Assembler *const out,
         void (Assembler::*emitter)(Vector<s>, RegMemVector<s>),
         const jit::HwRegister operand0,
         const jit::HwRegister operand1)
{
  std::invoke(emitter, out, Vector<s>(operand0), assignmentvec<s>(operand1));
}

/**
 * Only for use with instructions that have one INOUT register and a constant
 * for the second operand.
 */
template<RegisterSize s>
static void
fix_result_source0_mismatch(Assembler *const out,
                            const jit::RtlInstruction &rtl,
                            const GeneralRegister &gpr_scratch)
{
  if (rtl.result(0).hw != rtl.source(0).hw) {
    if (rtl.result(0).hw.is_spill()) {
      out->mov(Register<DWORD>(gpr_scratch), assignment<DWORD>(rtl.source(0).hw));
      out->mov(assignment<DWORD>(rtl.result(0).hw), Register<DWORD>(gpr_scratch));
    } else {
      out->mov(assignment<DWORD>(rtl.result(0).hw), assignment<DWORD>(rtl.source(0).hw));
    }
  }
}

/*
 * Generic emit method for one-operand instructions. Uses the table of backend
 * opcode to emit method / constraints in amd64_compiler.hh.
 */
static void
emit1(Assembler *const out, const jit::RtlInstruction &rtl)
{
  const auto emit = emit_table[rtl.op];

  /* TODO Non-destructive operations need to interpret source indexes in a
   *      different way than we do now. */
  /* TODO Support instructions that have no direct result (e.g. compare). */
  assert(emit.first_output);
  assert((rtl.source_count == 1 && emit.first_input) ||
         (rtl.source_count == 0 && !emit.first_input));
  assert(rtl.result_count == 1);

  /* If the operation uses a destructive input register and RTL assignments for
   * the source and destination were not merged, initialize the destination with
   * the source value. */
  if (emit.first_input && rtl.result(0).hw != rtl.source(0).hw) {
    if (rtl.result(0).hw.is_spill() && rtl.source(0).hw.is_spill()) {
      movT(out,
           emit.size,
           jit::HwRegister(ScalarType, Compiler::gpr_scratch),
           rtl.source(0).hw);
      movT(out,
           emit.size,
           rtl.result(0).hw,
           jit::HwRegister(ScalarType, Compiler::gpr_scratch));
    } else {
      movT(out, emit.size, rtl.result(0).hw, rtl.source(0).hw);
    }
  }

  switch (emit.size) {
    case BYTE:
      std::invoke(emit.byte1, out, assignment<BYTE>(rtl.result(0).hw));
      break;
    case WORD:
      std::invoke(emit.word1, out, assignment<WORD>(rtl.result(0).hw));
      break;
    case DWORD:
      std::invoke(emit.dword1, out, assignment<DWORD>(rtl.result(0).hw));
      break;
    case QWORD:
      std::invoke(emit.qword1, out, assignment<QWORD>(rtl.result(0).hw));
      break;
    default:
      assert(false);
  }
}

/*
 * Generic emit method for two-operand GPR instructions. Uses the table of
 * backend opcode to emit method / constraints in amd64_compiler.hh.
 */
static void
emit2(Assembler *const out, const jit::RtlInstruction &rtl)
{
  const auto emit = emit_table[rtl.op];

  /* TODO Non-destructive operations need to interpret source indexes in a
   *      different way than we do now. */
  assert(emit.first_input || emit.first_output);
  assert((rtl.result_count == 1 && emit.first_output) || !emit.first_output);

  /* Check whether we need to use scratch for the first operand. Scratch is
   * used for the first operand in the following scenarios:
   *
   *     (1) The result location is a memory spill, but the instruction does
   *         not allow the first operand to be memory.
   *     (2) The input and outputs are shared in the instruction but not merged
   *         by the RTL, and either:
   *         (a) Both are memory locations
   *         (b) The second operand is the same as the result operand
   *
   * Note: If both operands are in memory, we prefer to keep the first operand
   *       in memory to avoid an extra move of the final result.
   */
  bool first_scratch = false;
  bool first_memory;
  if (emit.first_input) {
    assert(rtl.source_count == 2);
    first_memory =
      emit.first_output ? rtl.result(0).hw.is_spill() : rtl.source(0).hw.is_spill();
    if (first_memory && !emit.first_memory) {
      first_scratch = true;
      first_memory = false;
    } else if (emit.first_output) {
      if (rtl.result(0).hw != rtl.source(0).hw) {
        if (rtl.result(0).hw == rtl.source(1).hw) {
          first_scratch = true;
          first_memory = false;
        } else if (rtl.result(0).hw.is_spill() && rtl.source(0).hw.is_spill()) {
          first_scratch = true;
          first_memory = false;
        }
      }
    }
  } else {
    assert(rtl.source_count == 1);
    assert(!rtl.result(0).hw.is_spill() || emit.first_memory); /* XXX */
    first_memory = rtl.result(0).hw.is_spill();
  }

  /* Check whether we need to use scratch for the second operand. Scratch is
   * used for the second operand in the following scenarios:
   *
   *     (1) The location is a memory spill, but the instruction does not allow
   *         the second operand to be memory.
   *     (2) Both the input and the output are memory locations.
   */
  bool second_scratch = false;
  if (emit.first_input) {
    if (rtl.source(1).hw.is_spill() && !emit.second_memory) {
      second_scratch = true;
    } else if (first_memory && rtl.source(1).hw.is_spill()) {
      second_scratch = true;
    }
  } else {
    if (rtl.source(0).hw.is_spill() && !emit.second_memory) {
      second_scratch = true;
    } else if (first_memory && rtl.source(0).hw.is_spill()) {
      second_scratch = true;
    }
  }

  /* XXX */
  assert(!(first_scratch && second_scratch));

  /* Prepare the first operand storage if it's used as an input. */
  if (emit.first_input) {
    if (first_scratch) {
      movT(out,
           emit.size,
           jit::HwRegister(ScalarType, Compiler::gpr_scratch),
           rtl.source(0).hw);
    } else if (emit.first_output && rtl.result(0).hw != rtl.source(0).hw) {
      movT(out, emit.size, rtl.result(0).hw, rtl.source(0).hw);
    }
  }

  /* Prepare the second operand storage. */
  if (second_scratch) {
    if (emit.first_input) {
      movT(out,
           emit.size,
           jit::HwRegister(ScalarType, Compiler::gpr_scratch),
           rtl.source(1).hw);
    } else {
      movT(out,
           emit.size,
           jit::HwRegister(ScalarType, Compiler::gpr_scratch),
           rtl.source(0).hw);
    }
  }

  jit::HwRegister operand0;
  jit::HwRegister operand1;
  if (emit.first_input) {
    operand0 = first_scratch ? jit::HwRegister(ScalarType, Compiler::gpr_scratch)
                             : (emit.first_output ? rtl.result(0).hw : rtl.source(0).hw);
    operand1 = second_scratch ? jit::HwRegister(ScalarType, Compiler::gpr_scratch)
                              : rtl.source(1).hw;
  } else {
    assert(!first_scratch);
    operand0 = rtl.result(0).hw;
    operand1 = second_scratch ? jit::HwRegister(ScalarType, Compiler::gpr_scratch)
                              : rtl.source(0).hw;
  }

  switch (emit.size) {
    case BYTE:
      emitT<BYTE>(out, emit.byte, operand0, operand1);
      break;
    case WORD:
      emitT<WORD>(out, emit.word, operand0, operand1);
      break;
    case DWORD:
      emitT<DWORD>(out, emit.dword, operand0, operand1);
      break;
    case QWORD:
      emitT<QWORD>(out, emit.qword, operand0, operand1);
      break;
    default:
      assert(false);
  }

  /* Save the result, if the operation was not done in-place. */
  if (emit.first_output && first_scratch) {
    movT(
      out, emit.size, rtl.result(0).hw, jit::HwRegister(ScalarType, Compiler::gpr_scratch));
  }
}

/*
 * Generic emit method for two-operand vector instructions. Uses the table of
 * backend opcode to emit method / constraints in amd64_compiler.hh.
 */
static void
emitvec2(Assembler *const out, const jit::RtlInstruction &rtl)
{
  const auto emit = emit_table[rtl.op];

  /* TODO Non-destructive operations need to interpret source indexes in a
   *      different way than we do now. */
  assert(emit.first_input || emit.first_output);
  assert((rtl.result_count == 1 && emit.first_output) || !emit.first_output);

  /* TODO All implemented SSE instructions currently support the second
   *      argument being a memory location, so we don't have logic to support
   *      other cases right now. */
  assert(emit.second_memory);
  assert(emit.first_output);

  /* Check whether we need to use scratch for the first operand. Scratch is
   * used for the first operand in the following scenarios:
   *
   *     (1) The result location is a memory spill. All SSE instructions require
   *         the result to be a normal register.
   *     (2) The input and outputs are shared in the instruction but not merged
   *         by the RTL (destructive) and the second operand is the same as the
   *         result operand.
   */
  bool first_scratch = false;
  if (rtl.result(0).hw.is_spill()) {
    first_scratch = true;
  } else if (emit.first_input) {
    assert(rtl.source_count == 2);
    if (rtl.result(0).hw != rtl.source(0).hw && rtl.result(0).hw == rtl.source(1).hw) {
      first_scratch = true;
    }
  } else {
    assert(rtl.source_count == 1);

    if (rtl.result(0).hw.is_spill()) {
      first_scratch = true;
    }
  }

  /* Prepare the first operand storage if it's used as an input. */
  if (emit.first_input) {
    if (first_scratch) {
      movdT(out,
            emit.size,
            jit::HwRegister(VectorType, Compiler::vec_scratch),
            rtl.source(0).hw);
    } else if (emit.first_output && rtl.result(0).hw != rtl.source(0).hw) {
      movdT(out, emit.size, rtl.result(0).hw, rtl.source(0).hw);
    }
  }

  jit::HwRegister operand0;
  operand0 =
    first_scratch ? jit::HwRegister(VectorType, Compiler::vec_scratch) : rtl.result(0).hw;

  jit::HwRegister operand1;
  if (emit.first_input) {
    operand1 = rtl.source(1).hw;
  } else {
    operand1 = rtl.source(0).hw;
  }

  switch (emit.size) {
    case VECSS:
      emitvecT<DWORD>(out, emit.vecss, operand0, operand1);
      break;
    case VECSD:
      emitvecT<QWORD>(out, emit.vecsd, operand0, operand1);
      break;
    default:
      assert(false);
  }

  /* Save the result, if the operation was not done in-place. */
  if (emit.first_output && first_scratch) {
    movdT(
      out, emit.size, rtl.result(0).hw, jit::HwRegister(VectorType, Compiler::vec_scratch));
  }
}

/*
 * Generic emit method all instructions. Routes to a more specific emit method
 * based on the number and type of operands.
 */
static void
emit(Assembler *const out, const jit::RtlInstruction &rtl)
{
  const auto emit = emit_table[rtl.op];
  assert(unsigned(emit.opcode) == rtl.op);

  switch (emit.size) {
    /* Instructions operating on general purpose registers. */
    case BYTE:
    case WORD:
    case DWORD:
    case QWORD:
      switch (emit.operands) {
        case 0:
          std::invoke(emit.none, out);
          break;
        case 1:
          emit1(out, rtl);
          break;
        case 2:
          emit2(out, rtl);
          break;
        default:
          assert(false);
      }
      break;

    /* Instructions operating on general vector registers. */
    case VECPS:
    case VECPD:
    case VECSS:
    case VECSD:
      switch (emit.operands) {
        case 2:
          emitvec2(out, rtl);
          break;
        default:
          assert(false);
      }
      break;

    default:
      assert(false);
  }
}

void
Compiler::assemble()
{
  m_asm.clear();

  /* Emit machine instructions from the RTL encoding. This is the first pass,
   * where all instructions are generated. Label positions and label users are
   * recorded for a second pass that patches branch offsets. The patch map
   * is in the form [disp32-offset] => label ID, with the assumption that the
   * branch will be relative to the byte immediately after the disp32. */
  std::map<unsigned, LabelId> branches;
  for (const jit::RtlInstruction &rtl : m_rtl.block(0)) {
    if (rtl.op & 0x8000u) {
      switch (jit::RtlOpcode(rtl.op)) {
        case jit::RtlOpcode::Move: {
          /* Move instructions can be inserted by the register allocator to
           * preserve constraints that hit conflicts. */
          /* TODO Spills should avoid loading all 8 bytes, right? Won't there be
           *      uninitialized data in there? */
          /* TODO Add logic for moves between xmm registers. The allocator
           *      shouldn't generate any moves unless we have fixed assignments,
           *      though. */
          if (rtl.result(0).hw.is_spill()) {
            assert(!rtl.source(0).hw.is_spill());
            m_asm.mov(Address<QWORD>(RBP, rtl.result(0).hw.spill_index() * sizeof(u64)),
                      Register<QWORD>(rtl.source(0).hw));
          } else if (rtl.source(0).hw.is_spill()) {
            assert(!rtl.result(0).hw.is_spill());
            m_asm.mov(Register<QWORD>(rtl.result(0).hw),
                      Address<QWORD>(RBP, rtl.source(0).hw.spill_index() * sizeof(u64)));
          } else {
            m_asm.mov(Register<QWORD>(rtl.result(0).hw), Register<QWORD>(rtl.source(0).hw));
          }
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
      case Opcode::LABEL: {
        const LabelId id = (LabelId)rtl.data;
        assert(m_labels[id] == UINT32_MAX);
        m_labels[id] = m_asm.size();
        continue;
      }

      case Opcode::AND_DWORD_IMM32: {
        fix_result_source0_mismatch<DWORD>(&m_asm, rtl, gpr_scratch);
        m_asm._and(assignment<DWORD>(rtl.result(0).hw), get_constant<u32>(rtl.data));
        break;
      }

      case Opcode::OR_DWORD_IMM32: {
        fix_result_source0_mismatch<DWORD>(&m_asm, rtl, gpr_scratch);
        m_asm._or(assignment<DWORD>(rtl.result(0).hw), get_constant<u32>(rtl.data));
        break;
      }

      case Opcode::XOR_BYTE_IMM8: {
        fix_result_source0_mismatch<DWORD>(&m_asm, rtl, gpr_scratch);
        m_asm._xor(assignment<DWORD>(rtl.result(0).hw), get_constant<u8>(rtl.data));
        break;
      }

      case Opcode::ADD_DWORD_IMM32: {
        fix_result_source0_mismatch<DWORD>(&m_asm, rtl, gpr_scratch);
        m_asm.add(assignment<DWORD>(rtl.result(0).hw), get_constant<u32>(rtl.data));
        break;
      }

      case Opcode::SUB_DWORD_IMM32: {
        fix_result_source0_mismatch<DWORD>(&m_asm, rtl, gpr_scratch);
        m_asm.sub(assignment<DWORD>(rtl.result(0).hw), get_constant<u32>(rtl.data));
        break;
      }

      case Opcode::SHIFTR_DWORD_IMM8: {
        fix_result_source0_mismatch<DWORD>(&m_asm, rtl, gpr_scratch);
        m_asm.shr(assignment<DWORD>(rtl.result(0).hw), get_constant<u8>(rtl.data));
        break;
      }

      case Opcode::SHIFTL_DWORD_IMM8: {
        fix_result_source0_mismatch<DWORD>(&m_asm, rtl, gpr_scratch);
        m_asm.shl(assignment<DWORD>(rtl.result(0).hw), get_constant<u8>(rtl.data));
        break;
      }

      case Opcode::ASHIFTR_DWORD_IMM8: {
        fix_result_source0_mismatch<DWORD>(&m_asm, rtl, gpr_scratch);
        m_asm.sar(assignment<DWORD>(rtl.result(0).hw), get_constant<u8>(rtl.data));
        break;
      }

      case Opcode::TEST_DWORD_IMM32: {
        m_asm.test(assignment<DWORD>(rtl.source(0).hw), get_constant<u32>(rtl.data));
        break;
      }

      case Opcode::CMP_DWORD_IMM32: {
        m_asm.cmp(assignment<DWORD>(rtl.source(0).hw), get_constant<u32>(rtl.data));
        break;
      }

      case Opcode::PUSH_REGISTERS: {
        /* TODO Add support for xmm/ymm registers. */
        const unsigned rmask = rtl.data & 0xffff;
        for (unsigned reg = 0; reg < 16; ++reg) {
          if (rmask & (1lu << reg)) {
            m_asm.push(Register<QWORD>((GeneralRegister)reg));
          }
        }
        break;
      }

      case Opcode::POP_REGISTERS: {
        /* TODO Add support for xmm/ymm registers. */
        const unsigned rmask = rtl.data & 0xffff;
        for (unsigned i = 0; i < 16; ++i) {
          /* Restore needs to be done in reverse order of save. */
          const unsigned reg = 15 - i;
          if (rmask & (1lu << reg)) {
            m_asm.pop(Register<QWORD>((GeneralRegister)reg));
          }
        }
        break;
      }

      case Opcode::ALLOCATE_SPILL: {
        /* Spill must be allocated in units of 16 bytes, since the ABI wants
         * the stack always aligned. */
        if (m_rtl.spill_size() > 0) {
          const unsigned spill_bytes = (m_rtl.spill_size() * sizeof(u64) + 15) & ~15;
          m_asm.sub(Register<QWORD>(RSP), i32(spill_bytes));
          m_asm.mov(Register<QWORD>(RBP), Register<QWORD>(RSP));
        }
        break;
      }

      case Opcode::FREE_SPILL: {
        if (m_rtl.spill_size() > 0) {
          const unsigned spill_bytes = (m_rtl.spill_size() * sizeof(u64) + 15) & ~15;
          m_asm.add(Register<QWORD>(RSP), i32(spill_bytes));
        }
        break;
      }

      case Opcode::READ_GUEST_REGISTER32: {
        const unsigned index = rtl.data & 0xffff;
        const RegMemAny guest = m_register_address_cb(index);
        if (rtl.result(0).hw.is_spill()) {
          m_asm.mov(Register<DWORD>(gpr_scratch), RegMem<DWORD>(guest));
          m_asm.mov(assignment<DWORD>(rtl.result(0).hw), Register<DWORD>(gpr_scratch));
        } else if (rtl.result(0).hw.type() == VectorType) {
          m_asm.movd(Vector<DWORD>(rtl.result(0).hw), RegMem<DWORD>(guest));
        } else {
          m_asm.mov(assignment<DWORD>(rtl.result(0).hw), RegMem<DWORD>(guest));
        }
        break;
      }

      case Opcode::READ_GUEST_REGISTER64: {
        const unsigned index = rtl.data & 0xffff;
        const RegMemAny guest = m_register_address_cb(index);
        if (rtl.result(0).hw.is_spill()) {
          m_asm.mov(Register<QWORD>(gpr_scratch), RegMem<QWORD>(guest));
          m_asm.mov(assignment<QWORD>(rtl.result(0).hw), Register<QWORD>(gpr_scratch));
        } else if (rtl.result(0).hw.type() == VectorType) {
          m_asm.movd(Vector<QWORD>(rtl.result(0).hw), RegMem<QWORD>(guest));
        } else {
          m_asm.mov(assignment<QWORD>(rtl.result(0).hw), RegMem<QWORD>(guest));
        }
        break;
      }

      case Opcode::WRITE_GUEST_REGISTER32: {
        const unsigned index = rtl.data & 0xffff;
        const RegMemAny guest = m_register_address_cb(index);
        if (rtl.source(0).hw.is_spill()) {
          m_asm.mov(Register<DWORD>(gpr_scratch), assignment<DWORD>(rtl.source(0).hw));
          m_asm.mov(RegMem<DWORD>(guest), Register<DWORD>(gpr_scratch));
        } else if (rtl.source(0).hw.type() == VectorType) {
          m_asm.movd(RegMem<DWORD>(guest), Vector<DWORD>(rtl.source(0).hw));
        } else {
          m_asm.mov(RegMem<DWORD>(guest), assignment<DWORD>(rtl.source(0).hw));
        }
        break;
      }

      case Opcode::WRITE_GUEST_REGISTER64: {
        const unsigned index = rtl.data & 0xffff;
        const RegMemAny guest = m_register_address_cb(index);
        if (rtl.source(0).hw.is_spill()) {
          m_asm.mov(Register<DWORD>(gpr_scratch), assignment<DWORD>(rtl.source(0).hw));
          m_asm.mov(RegMem<DWORD>(guest), Register<DWORD>(gpr_scratch));
        } else if (rtl.source(0).hw.type() == VectorType) {
          m_asm.movd(RegMem<QWORD>(guest), Vector<QWORD>(rtl.source(0).hw));
        } else {
          m_asm.mov(RegMem<DWORD>(guest), assignment<DWORD>(rtl.source(0).hw));
        }
        break;
      }

      case Opcode::LOAD_GUEST_MEMORY: {
        /* Attempt to use optimized pathway if available. */
        const unsigned size = rtl.data & 0xffff;
        if (m_load_emitter) {
          GeneralRegister address;
          if (rtl.source(0).hw.is_spill()) {
            m_asm.mov(Register<DWORD>(gpr_scratch),
                      Address<DWORD>(RBP, rtl.source(0).hw.spill_index() * sizeof(u64)));
            address = gpr_scratch;
          } else {
            address = GeneralRegister(rtl.source(0).hw.index());
          }

          GeneralRegister result;
          if (rtl.result(0).hw.is_spill()) {
            result = gpr_scratch;
          } else {
            result = GeneralRegister(rtl.result(0).hw.index());
          }

          m_load_emitter(&m_asm, RegisterSize(size), address, result);

          if (result == gpr_scratch) {
            m_asm.mov(assignment<QWORD>(rtl.result(0).hw), Register<QWORD>(result));
          }
          break;
        }

        const auto &saved_state = rtl.saved_state();

        /* The set of registers that need to be saved is the intersection of
         * registers that are caller-saved and the set of registers that were
         * not available for allocation at the time of the call. */
        const jit::RegisterSet gpr_state = saved_state[size_t(ScalarType)];
        u32 gpmask = abi_caller_saved & ~((1lu << RAX) | 1lu << gpr_scratch);
        for (unsigned i = 0; i < 16; ++i) {
          /* Mark registers that were unallocated as not requiring a save. */
          if (gpr_state.is_free(jit::HwRegister(ScalarType, i))) {
            gpmask &= ~(1lu << i);
          }
        }

        const jit::RegisterSet vec_state = saved_state[size_t(VectorType)];
        u32 vecmask = 0xffff & ~(1u << vec_scratch);
        for (unsigned i = 0; i < 16; ++i) {
          /* Mark registers that were unallocated as not requiring a save. */
          if (vec_state.is_free(jit::HwRegister(VectorType, i))) {
            vecmask &= ~(1u << i);
          }
        }

        bool aligned = false;
        for (unsigned reg = 0; reg < 16; ++reg) {
          if (gpmask & (1lu << reg)) {
            m_asm.push(Register<QWORD>((GeneralRegister)reg));
            aligned = !aligned;
          }
        }
        if (!aligned) {
          /* Extra push to align stack. */
          m_asm.push(Register<QWORD>(RCX));
        }

        /* XXX This logic needs to change when we use actual vectors. */
        if (vecmask != 0) {
          m_asm.sub(Register<QWORD>(RSP), u16(16 * 8));
          for (unsigned reg = 0; reg < 16; ++reg) {
            if (vecmask & (1u << reg)) {
              m_asm.movd(Address<QWORD>(RSP, reg * 8),
                         Vector<QWORD>((VectorRegister)reg));
            }
          }
        }

        if (!rtl.source(0).hw.is_spill()) {
          if (rtl.source(0).hw.index() != RSI) {
            m_asm.mov(Register<DWORD>(RSI), Register<DWORD>(rtl.source(0).hw));
          }
        } else {
          m_asm.mov(Register<DWORD>(RSI),
                    Address<DWORD>(RBP, rtl.source(0).hw.spill_index() * sizeof(u64)));
        }

        assert(size == 1 || size == 2 || size == 4 || size == 8);
        m_asm.mov(Register<DWORD>(RDX), (u32)size);
        m_asm.mov(Register<QWORD>(RAX), (u64)&wrap_load);
        m_asm.call(Register<QWORD>(RAX));

        if (vecmask != 0) {
          /* Restore needs to be done in reverse order of save. */
          for (unsigned i = 0; i < 16; ++i) {
            const unsigned reg = 15 - i;
            if (vecmask & (1u << reg)) {
              m_asm.movd(Vector<QWORD>((VectorRegister)reg),
                         Address<QWORD>(RSP, reg * 8));
            }
          }

          m_asm.add(Register<QWORD>(RSP), u16(16 * 8));
        }

        if (!aligned) {
          /* Extra pop to restore stack. */
          m_asm.pop(Register<QWORD>(RCX));
        }
        for (unsigned i = 0; i < 16; ++i) {
          /* Restore needs to be done in reverse order of save. */
          const unsigned reg = 15 - i;
          if (gpmask & (1lu << reg)) {
            m_asm.pop(Register<QWORD>((GeneralRegister)reg));
          }
        }
        break;
      }

      case Opcode::CALL_FRAMED: {
        const auto &saved_state = rtl.saved_state();

        /* The set of registers that need to be saved is the intersection of
         * registers that are caller-saved and the set of registers that were
         * not available for allocation at the time of the call. */
        const jit::RegisterSet gpr_state = saved_state[size_t(ScalarType)];
        u32 gpmask = abi_caller_saved & ~((1u << RAX) | (1u << gpr_scratch));
        for (unsigned i = 0; i < 16; ++i) {
          /* Mark registers that were unallocated as not requiring a save. */
          if (gpr_state.is_free(jit::HwRegister(ScalarType, i))) {
            gpmask &= ~(1u << i);
          }
        }

        const jit::RegisterSet vec_state = saved_state[size_t(VectorType)];
        u32 vecmask = 0xffff & ~(1u << vec_scratch);
        for (unsigned i = 0; i < 16; ++i) {
          /* Mark registers that were unallocated as not requiring a save. */
          if (vec_state.is_free(jit::HwRegister(VectorType, i))) {
            vecmask &= ~(1u << i);
          }
        }

        bool aligned = false;
        for (unsigned reg = 0; reg < 16; ++reg) {
          if (gpmask & (1lu << reg)) {
            m_asm.push(Register<QWORD>((GeneralRegister)reg));
            aligned = !aligned;
          }
        }

        if (!aligned) {
          /* Extra push to align stack. */
          m_asm.push(Register<QWORD>(RCX));
        }

        /* XXX This logic needs to change when we use actual vectors. */
        if (vecmask != 0) {
          m_asm.sub(Register<QWORD>(RSP), u16(16 * 8));
          for (unsigned reg = 0; reg < 16; ++reg) {
            if (vecmask & (1u << reg)) {
              m_asm.movd(Address<QWORD>(RSP, reg * 8),
                         Vector<QWORD>((VectorRegister)reg));
            }
          }
        }

        m_asm.call(Register<QWORD>(rtl.source(0).hw));

        if (vecmask != 0) {
          /* Restore needs to be done in reverse order of save. */
          for (unsigned i = 0; i < 16; ++i) {
            const unsigned reg = 15 - i;
            if (vecmask & (1u << reg)) {
              m_asm.movd(Vector<QWORD>((VectorRegister)reg),
                         Address<QWORD>(RSP, reg * 8));
            }
          }

          m_asm.add(Register<QWORD>(RSP), u16(16 * 8));
        }

        if (!aligned) {
          /* Extra pop to restore stack. */
          m_asm.pop(Register<QWORD>(RCX));
        }
        for (unsigned i = 0; i < 16; ++i) {
          /* Restore needs to be done in reverse order of save. */
          const unsigned reg = 15 - i;
          if (gpmask & (1u << reg)) {
            m_asm.pop(Register<QWORD>((GeneralRegister)reg));
          }
        }
        break;
      }

      case Opcode::LOAD_BYTE_IMM8: {
        /* XXX Uses the longer ModRM form, even if target is a register. */
        m_asm.mov(assignment<BYTE>(rtl.result(0).hw), get_constant<u8>(rtl.data));
        break;
      }

      case Opcode::LOAD_QWORD_IMM32: {
        /* Zero-extended DWORD. */
        if (!rtl.result(0).hw.is_spill()) {
          m_asm.mov(Register<DWORD>(rtl.result(0).hw), get_constant<u32>(rtl.data));
        } else {
          /* XXX Use scratch register to get zero extension. */
          m_asm.mov(Register<DWORD>(gpr_scratch), get_constant<u32>(rtl.data));
          m_asm.mov(assignment<QWORD>(rtl.result(0).hw), Register<QWORD>(gpr_scratch));
        }
        break;
      }

      case Opcode::LOAD_QWORD_IMM64: {
        assert(!rtl.result(0).hw.is_spill());
        m_asm.mov(Register<QWORD>(rtl.result(0).hw), get_constant<u64>(rtl.data));
        break;
      }

      case Opcode::SHIFTR_BYTE: {
        emitF<BYTE, FixedRegister<BYTE, RCX>>(&m_asm, &Assembler::shr, rtl);
        break;
      }

      case Opcode::SHIFTR_WORD: {
        emitF<WORD, FixedRegister<BYTE, RCX>>(&m_asm, &Assembler::shr, rtl);
        break;
      }

      case Opcode::SHIFTR_DWORD: {
        emitF<DWORD, FixedRegister<BYTE, RCX>>(&m_asm, &Assembler::shr, rtl);
        break;
      }

      case Opcode::SHIFTR_QWORD: {
        emitF<QWORD, FixedRegister<BYTE, RCX>>(&m_asm, &Assembler::shr, rtl);
        break;
      }

      case Opcode::SHIFTL_BYTE: {
        emitF<BYTE, FixedRegister<BYTE, RCX>>(&m_asm, &Assembler::shl, rtl);
        break;
      }

      case Opcode::SHIFTL_WORD: {
        emitF<WORD, FixedRegister<BYTE, RCX>>(&m_asm, &Assembler::shl, rtl);
        break;
      }

      case Opcode::SHIFTL_DWORD: {
        emitF<DWORD, FixedRegister<BYTE, RCX>>(&m_asm, &Assembler::shl, rtl);
        break;
      }

      case Opcode::SHIFTL_QWORD: {
        emitF<QWORD, FixedRegister<BYTE, RCX>>(&m_asm, &Assembler::shl, rtl);
        break;
      }

      case Opcode::ASHIFTR_BYTE: {
        emitF<BYTE, FixedRegister<BYTE, RCX>>(&m_asm, &Assembler::sar, rtl);
        break;
      }

      case Opcode::ASHIFTR_WORD: {
        emitF<WORD, FixedRegister<BYTE, RCX>>(&m_asm, &Assembler::sar, rtl);
        break;
      }

      case Opcode::ASHIFTR_DWORD: {
        emitF<DWORD, FixedRegister<BYTE, RCX>>(&m_asm, &Assembler::sar, rtl);
        break;
      }

      case Opcode::ASHIFTR_QWORD: {
        emitF<QWORD, FixedRegister<BYTE, RCX>>(&m_asm, &Assembler::sar, rtl);
        break;
      }

      case Opcode::ROL_BYTE: {
        emitF<BYTE, FixedRegister<BYTE, RCX>>(&m_asm, &Assembler::rol, rtl);
        break;
      }

      case Opcode::ROL_WORD: {
        emitF<WORD, FixedRegister<BYTE, RCX>>(&m_asm, &Assembler::rol, rtl);
        break;
      }

      case Opcode::ROL_DWORD: {
        emitF<DWORD, FixedRegister<BYTE, RCX>>(&m_asm, &Assembler::rol, rtl);
        break;
      }

      case Opcode::ROL_QWORD: {
        emitF<QWORD, FixedRegister<BYTE, RCX>>(&m_asm, &Assembler::rol, rtl);
        break;
      }

      case Opcode::ROR_BYTE: {
        emitF<BYTE, FixedRegister<BYTE, RCX>>(&m_asm, &Assembler::ror, rtl);
        break;
      }

      case Opcode::ROR_WORD: {
        emitF<WORD, FixedRegister<BYTE, RCX>>(&m_asm, &Assembler::ror, rtl);
        break;
      }

      case Opcode::ROR_DWORD: {
        emitF<DWORD, FixedRegister<BYTE, RCX>>(&m_asm, &Assembler::ror, rtl);
        break;
      }

      case Opcode::ROR_QWORD: {
        emitF<QWORD, FixedRegister<BYTE, RCX>>(&m_asm, &Assembler::ror, rtl);
        break;
      }

      case Opcode::MUL_BYTE: {
        if (rtl.source(0).hw.is_spill()) {
          m_asm.mov(Register<BYTE>(Compiler::gpr_scratch), assignment<BYTE>(rtl.source(0).hw));
          m_asm.mul(Register<BYTE>(Compiler::gpr_scratch));
        } else {
          m_asm.mul(assignment<BYTE>(rtl.source(0).hw));
        }
        break;
      }

      case Opcode::MUL_WORD: {
        if (rtl.source(0).hw.is_spill()) {
          m_asm.mov(Register<WORD>(Compiler::gpr_scratch), assignment<WORD>(rtl.source(0).hw));
          m_asm.mul(Register<WORD>(Compiler::gpr_scratch));
        } else {
          m_asm.mul(assignment<WORD>(rtl.source(0).hw));
        }
        break;
      }

      case Opcode::MUL_DWORD: {
        if (rtl.source(0).hw.is_spill()) {
          m_asm.mov(Register<DWORD>(Compiler::gpr_scratch), assignment<DWORD>(rtl.source(0).hw));
          m_asm.mul(Register<DWORD>(Compiler::gpr_scratch));
        } else {
          m_asm.mul(assignment<DWORD>(rtl.source(0).hw));
        }
        break;
      }

      case Opcode::MUL_QWORD: {
        if (rtl.source(0).hw.is_spill()) {
          m_asm.mov(Register<QWORD>(Compiler::gpr_scratch), assignment<QWORD>(rtl.source(0).hw));
          m_asm.mul(Register<QWORD>(Compiler::gpr_scratch));
        } else {
          m_asm.mul(assignment<QWORD>(rtl.source(0).hw));
        }
        break;
      }

      case Opcode::EXTEND32_BYTE: {
        emitN<DWORD, BYTE>(&m_asm, &Assembler::movsx, rtl);
        break;
      }

      case Opcode::EXTEND32_WORD: {
        emitN<DWORD, WORD>(&m_asm, &Assembler::movsx, rtl);
        break;
      }

      case Opcode::EXTEND64_BYTE: {
        assert(false);
        // emitN<QWORD, BYTE>(&m_asm, &Assembler::movsx, rtl);
        break;
      }

      case Opcode::EXTEND64_WORD: {
        emitN<QWORD, WORD>(&m_asm, &Assembler::movsx, rtl);
        break;
      }

      case Opcode::EXTEND64_DWORD: {
        emitN<QWORD, WORD>(&m_asm, &Assembler::movsx, rtl);
        break;
      }

      case Opcode::ZEXTEND32_BYTE: {
        emitN<DWORD, BYTE>(&m_asm, &Assembler::movzx, rtl);
        break;
      }

      case Opcode::ZEXTEND32_WORD: {
        emitN<DWORD, WORD>(&m_asm, &Assembler::movzx, rtl);
        break;
      }

      case Opcode::ZEXTEND64_BYTE: {
        emitN<DWORD, BYTE>(&m_asm, &Assembler::movzx, rtl);
        break;
      }

      case Opcode::ZEXTEND64_WORD: {
        emitN<QWORD, WORD>(&m_asm, &Assembler::movzx, rtl);
        break;
      }

      case Opcode::ZEXTEND64_DWORD: {
        emitN<DWORD, DWORD>(&m_asm, &Assembler::mov, rtl);
        break;
      }

      case Opcode::MOVD_DWORD: {
        /* TODO Optimize / cleanup */
        if (rtl.result(0).hw.is_spill() && rtl.source(0).hw.is_spill()) {
          m_asm.mov(Register<DWORD>(gpr_scratch), assignment<DWORD>(rtl.source(0).hw));
          m_asm.mov(assignment<DWORD>(rtl.result(0).hw), Register<DWORD>(gpr_scratch));
        } else if (rtl.result(0).hw.type() == VectorType) {
          m_asm.movd(Vector<DWORD>(rtl.result(0).hw), assignment<DWORD>(rtl.source(0).hw));
        } else if (rtl.source(0).hw.type() == VectorType) {
          m_asm.movd(assignment<DWORD>(rtl.result(0).hw), Vector<DWORD>(rtl.source(0).hw));
        } else {
          m_asm.mov(assignment<DWORD>(rtl.result(0).hw),
                    assignment<DWORD>(rtl.source(0).hw));
        }
        break;
      }

      case Opcode::MOVD_QWORD: {
        /* TODO Optimize / cleanup */
        if (rtl.result(0).hw.is_spill() && rtl.source(0).hw.is_spill()) {
          m_asm.mov(Register<QWORD>(gpr_scratch), assignment<QWORD>(rtl.source(0).hw));
          m_asm.mov(assignment<QWORD>(rtl.result(0).hw), Register<QWORD>(gpr_scratch));
        } else if (rtl.result(0).hw.type() == VectorType) {
          m_asm.movd(Vector<QWORD>(rtl.result(0).hw), assignment<QWORD>(rtl.source(0).hw));
        } else if (rtl.source(0).hw.type() == VectorType) {
          m_asm.movd(assignment<QWORD>(rtl.result(0).hw), Vector<QWORD>(rtl.source(0).hw));
        } else {
          m_asm.mov(assignment<QWORD>(rtl.result(0).hw),
                    assignment<QWORD>(rtl.source(0).hw));
        }
        break;
      }

      case Opcode::JMP: {
        const LabelId label = (LabelId)rtl.data;
        m_asm.jmp(i32(0));
        branches.emplace(m_asm.size() - sizeof(i32), label);
        break;
      }

      case Opcode::JNZ: {
        const LabelId label = (LabelId)rtl.data;
        m_asm.jnz(i32(0));
        branches.emplace(m_asm.size() - sizeof(i32), label);
        break;
      }

      default:
        /* Basic instructions are handled with the opcode mapping table. */
        assert(rtl.op < emit_table_size);
        if (emit_table[rtl.op].method) {
          emit(&m_asm, rtl);
          break;
        }

        printf("Invalid amd64 RTL opcode: %u\n", (unsigned)opcode);
        assert(false);
    }
  }

  /* Patch all relative offset branches in the generated source stream. */
  for (const auto &patch : branches) {
    const i32 reference_point = patch.first + sizeof(i32);
    assert(m_labels[patch.second] < m_asm.size());
    const i32 displacement = m_labels[patch.second] - reference_point;
    memcpy(&m_asm.data()[patch.first], &displacement, sizeof(i32));
  }

  Routine *const result = new Routine(m_asm.data(), m_asm.size());
  m_routine = std::unique_ptr<Routine>(result);
}

jit::RtlRegister
Compiler::get_rtl_ssa(const ir::Operand operand)
{
  if (operand.is_register()) {
    assert(m_ir_to_rtl.size() > operand.register_index());
    assert(m_ir_to_rtl[operand.register_index()].valid());
    return m_ir_to_rtl[operand.register_index()];
  }

  /* TODO optimize. */
  jit::RtlRegister ssa_constant;
  switch (operand.type()) {
    case ir::Type::Integer8: {
      const u32 value = operand.value().u8_value;
      const u64 constant = make_constant<u32>(value);
      ssa_constant = m_rtl.ssa_allocate(BYTE);
      RTL_ENCODE_0R(Opcode::LOAD_QWORD_IMM32, constant, HW_ANY(ssa_constant));
      break;
    }

    case ir::Type::Integer16: {
      const u32 value = operand.value().u16_value;
      const u64 constant = make_constant<u32>(value);
      ssa_constant = m_rtl.ssa_allocate(WORD);
      RTL_ENCODE_0R(Opcode::LOAD_QWORD_IMM32, constant, HW_ANY(ssa_constant));
      break;
    }

    case ir::Type::Integer32: {
      const u32 value = operand.value().u32_value;
      const u64 constant = make_constant<u32>(value);
      ssa_constant = m_rtl.ssa_allocate(DWORD);
      RTL_ENCODE_0R(Opcode::LOAD_QWORD_IMM32, constant, HW_ANY(ssa_constant));
      break;
    }

    case ir::Type::Integer64: {
      const u64 value = operand.value().u64_value;
      const u64 constant = make_constant<u64>(value);
      ssa_constant = m_rtl.ssa_allocate(QWORD);
      RTL_ENCODE_0R(Opcode::LOAD_QWORD_IMM64, constant, HW_ANY(ssa_constant));
      break;
    }

    case ir::Type::Float32: {
      const f32 value = operand.value().f32_value;
      const u64 constant = make_constant<f32>(value);
      const jit::RtlRegister ssa_temp = m_rtl.ssa_allocate(DWORD);
      ssa_constant = m_rtl.ssa_allocate(VECSS);
      RTL_ENCODE_0R(Opcode::LOAD_QWORD_IMM32, constant, HW_ANY(ssa_temp));
      RTL_ENCODE_1R(Opcode::MOVD_DWORD, 0, VEC_ANY(ssa_constant), HW_ANY(ssa_temp));
      break;
    }

    case ir::Type::Float64: {
      const f64 value = operand.value().f64_value;
      const u64 constant = make_constant<f64>(value);
      const jit::RtlRegister ssa_temp = m_rtl.ssa_allocate(QWORD);
      ssa_constant = m_rtl.ssa_allocate(VECSD);
      RTL_ENCODE_0R(Opcode::LOAD_QWORD_IMM64, constant, HW_ANY(ssa_temp));
      RTL_ENCODE_1R(Opcode::MOVD_QWORD, 0, VEC_ANY(ssa_constant), HW_ANY(ssa_temp));
      break;
    }

    case ir::Type::Bool: {
      const u32 value = operand.value().bool_value;
      const u64 constant = make_constant<u32>(value ? 1u : 0u);
      ssa_constant = m_rtl.ssa_allocate(BYTE);
      RTL_ENCODE_0R(Opcode::LOAD_QWORD_IMM32, constant, HW_ANY(ssa_constant));
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

  m_ir_to_rtl[index] = m_rtl.ssa_allocate(ir_to_amd64_type(operand.type()));
  return m_ir_to_rtl[index];
}

Compiler::LabelId
Compiler::allocate_label(const char *const name)
{
  const LabelId id = m_labels.size();
  m_labels.resize(id + 1u, UINT32_MAX);
  return id;
}

extern "C" {

ir::Constant
wrap_load(Guest *const guest, const u32 address, const size_t bytes)
{
  return guest->guest_load(address, bytes);
}

void
wrap_store(Guest *const guest,
           const u32 address,
           const size_t bytes,
           const ir::Constant value)
{
  guest->guest_store(address, bytes, value);
}

}

}
}
}
