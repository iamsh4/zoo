#include "fox/fox_types.h"
#include "shared/bitmanip.h"

#include "guest/arm7di/arm7di.h"
#include "guest/arm7di/arm7di_ir.h"

using Type    = fox::ir::Type;
using Operand = fox::ir::Operand;

fox::Value
subtraction_overflows_u32(fox::Guest *, fox::Value a, fox::Value b)
{
  i32 signed_a, signed_b;
  memcpy(&signed_a, &a.u32_value, sizeof(signed_a));
  memcpy(&signed_b, &b.u32_value, sizeof(signed_b));

  i32 result;
  const bool overflows = __builtin_sub_overflow(signed_a, signed_b, &result);
  return fox::Value { .bool_value = overflows };
}

namespace guest::arm7di {

const u32 CPSR_N_INDEX = 31;
const u32 CPSR_Z_INDEX = 30;
const u32 CPSR_C_INDEX = 29;
const u32 CPSR_V_INDEX = 28;

fox::Value
addition_overflows_u32(fox::Guest *, fox::Value a, fox::Value b)
{
  i32 signed_a, signed_b;
  memcpy(&signed_a, &a.u32_value, sizeof(signed_a));
  memcpy(&signed_b, &b.u32_value, sizeof(signed_b));

  i32 result;
  const bool overflows = __builtin_add_overflow(signed_a, signed_b, &result);
  return fox::Value { .bool_value = overflows };
}

OpcodeClass
decode_opcode_class(u32 word)
{
  // All of these are identified on Page 25 of DDI0027D_7di_ds.pdf
  // See correspondence with structs OpcodeType1, OpcodeType2, etc.

  // NOTE: The order these are decoded matters, so don't re-arrange. See
  // the table on pg. 25 to understand how there would be ambiguity otherwise.

  // clang-format off
  //           3322222222221111111111
  //           10987654321098765432109876543210
  if((word & 0b00001111110000000000000011110000) ==
             0b00000000000000000000000010010000) return OpcodeClass::Multiply;
  if((word & 0b00001111101100000000111111110000) ==
             0b00000001000000000000000010010000) return OpcodeClass::SingleDataSwap;
  if((word & 0b00001100000000000000000000000000) ==
             0b00000100000000000000000000000000) return OpcodeClass::SingleDataTransfer;
  if((word & 0b00001100000000000000000000000000) ==
             0b00000000000000000000000000000000) return OpcodeClass::DataProcessing;
  if((word & 0b00001110000000000000000000010000) ==
             0b00000110000000000000000000010000) return OpcodeClass::Undefined;
  if((word & 0b00001110000000000000000000000000) ==
             0b00001000000000000000000000000000) return OpcodeClass::BlockDataTransfer;
  if((word & 0b00001110000000000000000000000000) ==
             0b00001010000000000000000000000000) return OpcodeClass::Branch;
  if((word & 0b00001110000000000000000000000000) ==
             0b00001100000000000000000000000000) return OpcodeClass::CoprocDataTransfer;
  if((word & 0b00001111000000000000000000010000) ==
             0b00001110000000000000000000000000) return OpcodeClass::CoprocDataOperation;
  if((word & 0b00001111000000000000000000010000) ==
             0b00001110000000000000000000010000) return OpcodeClass::CoprocRegisterTransfer;
  if((word & 0b00001111000000000000000000000000) ==
             0b00001111000000000000000000000000) return OpcodeClass::SoftwareInterrupt;

  printf("Word 0x%08x didn't match any known opcode class\n", word);
  assert(false && "impossible arm7di instruction encoding");
  // clang-format on
  throw std::runtime_error("Unhandled opcode class");
}

// Note: Anything using this should be updated to actual cycle count
constexpr u32 DEFAULT_CYCLES = 1;

#define DISAS_PRINTF(...)                                                                \
  {                                                                                      \
    assert(context.disas_buffer);                                                        \
    assert(context.disas_size);                                                          \
    snprintf(context.disas_buffer, context.disas_size, __VA_ARGS__);                     \
  }

const char *
cond_string(u32 cond_bits)
{
  static const char *cond_strings[16] = { "EQ",        "NE", "CS", "CC", "MI", "PL", "VS",
                                          "VC",        "HI", "LS", "GE", "LT", "GT", "LE",
                                          /*"AL"*/ "", "NV" };

  cond_bits &= 0b1111;
  return cond_strings[cond_bits];
}

void
Arm7DIAssembler::generate_ir(Arm7DIInstructionInfo &info)
{
  Context context { .info = info };

  // Handle condition code...
  const Operand cpsr             = read_reg(Arm7DIRegisterIndex_CPSR);
  const u32 condition_bits       = extract_bits(info.word, 31, 28);
  const Operand condition_failed = _not(check_condition_code(cpsr, condition_bits));

  const Operand maybe_exit_pc =
    select(condition_failed, const_u32(info.address), const_u32(info.address + 4));
  write_reg(Arm7DIRegisterIndex_PC, maybe_exit_pc);
  exit(condition_failed, const_u64(DEFAULT_CYCLES));

  const u32 instruction_count_entry = instruction_count();

  const OpcodeClass opcode_class = decode_opcode_class(info.word);
  // printf("OPCLASS %u\n", u32(opcode_class));
  switch (opcode_class) {
    case OpcodeClass::DataProcessing:
      opcode_data_processing(context);
      break;
    case OpcodeClass::Multiply:
      opcode_multiply(context);
      break;
    case OpcodeClass::SingleDataSwap:
      opcode_single_data_swap(context);
      break;
    case OpcodeClass::SingleDataTransfer:
      opcode_single_data_transfer(context);
      break;
    case OpcodeClass::Undefined:
      opcode_undefined(context);
      break;
    case OpcodeClass::BlockDataTransfer:
      opcode_block_data_transfer(context);
      break;
    case OpcodeClass::Branch:
      opcode_branch(context);
      break;
    case OpcodeClass::CoprocDataTransfer:
    case OpcodeClass::CoprocDataOperation:
    case OpcodeClass::CoprocRegisterTransfer:
      assert(false && "arm7 coprocessor instructions not implemented");
      break;
    case OpcodeClass::SoftwareInterrupt:
      opcode_software_interrupt(context);
      break;
    default:
      assert(false && "invalid arm7 opcode encountered");
  }

  exit(const_bool(true), const_u64(DEFAULT_CYCLES));

  // printf("instruction count %u\n", instruction_count());
  assert(instruction_count() != instruction_count_entry && "no IR was generated for instruction");
  (void)instruction_count_entry;
}

void
Arm7DIAssembler::opcode1_decode_op2_reg(OpcodeDataProcessing op,
                                        Operand &output,
                                        Operand &carry_out)
{
  if (op.I) {
    // "The immediate operand rotate field is a 4 bit unsigned integer which specifies a
    // shift operation on the 8 bit immediate value. This value is zero extended to 32
    // bits, and then subject to a rotate right by twice the value in the rotate field.
    // This enables many common constants to be generated, for example all powers of 2."

    const u32 imm        = extract_bits(op.operand2, 7, 0);
    const u32 ror_amount = 2u * extract_bits(op.operand2, 11, 8);
    // printf("OP2: I=1, imm=0x%08x, ror_amount=%u, output=0x%08x\n", imm, ror_amount);

    if (ror_amount > 0 && ror_amount < 32) {
      const u32 rotated = rotate_right(imm, ror_amount);
      output            = const_u32(rotated);
      carry_out         = const_u32((rotated >> 31) & 1);

    } else {
      output          = const_u32(imm);
      const auto cpsr = read_reg(Arm7DIRegisterIndex_CPSR);
      carry_out       = _and(shiftr(cpsr, const_u32(CPSR_C_INDEX)), const_u32(1));
    }

  } else {
    // Page 31 ...

    // op2 is a shift or rotate operation on a register specified in Rm
    Operand Rm = read_reg(extract_bits(op.operand2, 3, 0));

    // Shift amount is either an immediate, or given by Rs.
    Operand shift_amount;
    const bool shift_by_reg = extract_bits(op.raw, 4, 4);
    if (shift_by_reg) {
      const auto Rs = read_reg(extract_bits(op.raw, 11, 8));
      shift_amount  = _and(Rs, const_u32(0xFF));
    } else {
      shift_amount = const_u32(extract_bits(op.raw, 11, 7));
    }

    const u8 shift_type = extract_bits(op.raw, 6, 5);
    shift_logic(Rm, shift_amount, shift_type, output, carry_out);

    // u32 shift_amount_cpp =
    //   shift_by_reg ? extract_bits(op.raw, 11, 8) : extract_bits(op.raw, 11, 7);

    // printf("OP2: I=0, Rm=%u shift_by_reg=%u shift_amount=%u shift_type=%u\n",
    //        op.operand2 & 0xf,
    //        shift_by_reg,
    //        shift_amount_cpp,
    //        shift_type);
  }
}

using namespace fox::ir;

void
Arm7DIAssembler::shift_logic(fox::ir::Operand Rm,
                             fox::ir::Operand shift_amount,
                             u8 shift_type,
                             fox::ir::Operand &output,
                             fox::ir::Operand &carry_out)
{
  // In all cases...
  // Shift amount 0 does nothing, and the carry out is the old value of the CPSR C flag.

  // LSR 32. carry_out = bit 31 of Rm, result 0
  // ROR 32. carry_out = bit 31 of Rm, result = original
  // ROR (n>32) gives sam result as ROR (n % 32)

  enum shift_types
  {
    LSL = 0,
    LSR = 1,
    ASR = 2,
    ROR = 3,
  };

  Operand CPSR_C = read_reg_bit(Arm7DIRegisterIndex_CPSR, CPSR_C_INDEX);

  if (shift_type == shift_types::LSL) {
    // Note that LSL #0 is a special case, where the shifter carry out is the old value of
    // the CPSR C flag. The contents of Rm are used directly as the second operand.

    // Special cases for for LSL:
    // LSL=0 : output=input, carry_out=old_carry
    // LSL=32: output=0, carry_out=bit31
    // LSL>32: output=0, carry_out=0
    // So, we'll treat as one long 64b, and select the correct bits at the end.

    // full = 0 ... [C] [b31 .. b0]
    Operand full = bitcast(Type::Integer64, Rm);
    full         = _or(full, shiftl(bitcast(Type::Integer64, CPSR_C), const_u32(32)));

    // call(fox::ir::Type::Integer32, [](fox::Guest *guest, fox::Value x) {
    //   printf("x=0x%08x\n", x.u32_value);
    //   return fox::Value { .u32_value = 0 };
    // },  CPSR_C);

    // Perform LSL, bottom 32b hold typical result, bit 32 holds carry_out
    Operand output_64 = shiftl(full, shift_amount);

    Operand result_normal = bitcast(Type::Integer32, output_64);
    Operand carry_normal =
      bitcast(Type::Integer32, _and(shiftr(output_64, const_u64(32)), const_u64(1)));

    Operand shift_gte_32 = cmp_ugt(shift_amount, const_u32(32));
    output               = select(shift_gte_32, result_normal, const_u32(0));
    carry_out            = select(shift_gte_32, carry_normal, const_u32(0));

  } else if (shift_type == shift_types::LSR) {

    // Similar strategy to LSL
    // full = 000 [b31 .. b0] [C]

    // LSR #0 is used to encode LSR #32. pg 32
    shift_amount =
      select(cmp_eq(shift_amount, const_u32(0)), shift_amount, const_u32(32));

    Operand full = bitcast(Type::Integer64, Rm);
    full         = shiftl(full, const_u64(1));
    full         = _or(full, bitcast(Type::Integer64, CPSR_C));

    Operand output_64 = shiftr(full, shift_amount);

    Operand result_normal = shiftr(output_64, const_u32(1));
    result_normal         = bitcast(Type::Integer32, result_normal);

    Operand carry_normal = _and(output_64, const_u64(1));
    carry_normal         = bitcast(Type::Integer32, carry_normal);

    Operand shift_gte_32 = cmp_ugte(shift_amount, const_u32(32));
    output               = select(shift_gte_32, result_normal, const_u32(0));
    carry_out            = select(shift_gte_32, carry_normal, const_u32(0));

  } else if (shift_type == shift_types::ASR) {

    //////////////
    // ASR

    // ASR=0   : output=bit31 (x32), carry_out=bit31
    // ASR>=32 : same as above
    // else    : the expected behavior

    // boolean = bit31 set in Rm
    Operand bit31_val  = _and(shiftr(Rm, const_u32(31)), const_u32(1));
    Operand bit31_is_1 = cmp_eq(bit31_val, const_u32(1));

    // Duplicate bit31 of Rm to all positions
    Operand all_bit31 = select(bit31_is_1, const_u32(0), const_u32(0xffff'ffff));

    // Normal ASR and carry. Carry is the thing immediately "after" the bits
    // remaining after the shift, so shift amount - 1
    Operand normal_output = ashiftr(Rm, shift_amount);
    Operand normal_carry =
      _and(shiftr(Rm, sub(shift_amount, const_u32(1))), const_u32(1));

    // Weird edge case if shifting 0 or >=32 bits
    Operand is_edge_case =
      _or(cmp_eq(shift_amount, const_u32(0)), cmp_ugte(shift_amount, const_u32(32)));

    output    = select(is_edge_case, normal_output, all_bit31);
    carry_out = select(is_edge_case, normal_carry, bit31_val);

    //////////////

  } else if (shift_type == shift_types::ROR) {
    // throw std::logic_error("ROR not implemented");

    // ROR by 32, result=Rm, carry_out=bit31
    // ROR 0 performs Rotate Right Extended (RRX)
    // ROR >n, same as ROR n%32

    shift_amount = _and(shift_amount, const_u32(0x1f));

    const Operand normal_result = rotr(Rm, shift_amount);
    const Operand normal_carry =
      _and(const_u32(1), shiftr(Rm, sub(shift_amount, const_u32(1))));

    // If mod(shift_amount, 32) == 0, we're using RRX mode
    const Operand rrx_result =
      _or(shiftr(Rm, const_u32(1)), shiftl(CPSR_C, const_u32(31)));
    const Operand rrx_carry = _and(Rm, const_u32(1));

    const Operand is_rrx = cmp_eq(shift_amount, const_u32(0));

    output    = select(is_rrx, normal_result, rrx_result);
    carry_out = select(is_rrx, normal_carry, rrx_carry);

  } else {
    assert(false && "Invalid shift type");
  }
}

fox::ir::Operand
Arm7DIAssembler::check_condition_code(fox::ir::Operand CPSR, u32 cond)
{
  const Operand Z = _not(cmp_eq(const_u32(0), _and(CPSR, const_u32(1 << CPSR_Z_INDEX))));
  const Operand N = _not(cmp_eq(const_u32(0), _and(CPSR, const_u32(1 << CPSR_N_INDEX))));
  const Operand C = _not(cmp_eq(const_u32(0), _and(CPSR, const_u32(1 << CPSR_C_INDEX))));
  const Operand V = _not(cmp_eq(const_u32(0), _and(CPSR, const_u32(1 << CPSR_V_INDEX))));

  const Operand True  = const_bool(true);
  const Operand False = const_bool(false);

  // Page 26
  switch (cond) {
      // clang-format off
    case 0b0000: return Z;                         // EQ : Z set
    case 0b0001: return _not(Z);                   // NE : Z clear
    case 0b0010: return C;                         // CS : C set
    case 0b0011: return _not(C);                   // CC : C clear
    case 0b0100: return N;                         // MI : N set
    case 0b0101: return _not(N);                   // PL : N clear
    case 0b0110: return V;                         // VS : V set
    case 0b0111: return _not(V);                   // VC : V clear
    case 0b1000: return _and(C,_not(Z));           // HI : C set and Z clear
    case 0b1001: return _or(_not(C),Z);            // LS : C clear or Z set
    case 0b1010: return cmp_eq(N,V);               // GE : N == V
    case 0b1011: return _not(cmp_eq(N,V));         // LT : N != V
    case 0b1100: return _and(_not(Z),cmp_eq(N,V)); // GT : Z clear and N == V
    case 0b1101: return _or(Z,_not(cmp_eq(N,V)));  // LE : Z set or N != V
    case 0b1110: return True;                      // AL : Always
    case 0b1111: return False;                     // NV : Never
    // clang-format on
    default:
      throw std::logic_error("Unimplemented condition code");
  }
}

void
Arm7DIAssembler::handle_msr_write(bool which_psr, fox::ir::Operand value)
{
  // Handle mode switch if it took place
  call(
    fox::ir::Type::Integer32,
    [](fox::Guest *guest, fox::Value which, fox::Value value) {
      Arm7DI *const arm7di = static_cast<Arm7DI *>(guest);

      const u32 dest_psr_index = Arm7DIRegisterIndex_CPSR + which.u32_value;
      const u32 current_cpsr =
        guest->guest_register_read(Arm7DIRegisterIndex_CPSR, 4).u32_value;
      const u32 current_mode = current_cpsr & 0x1F;
      const u32 new_mode     = value.u32_value & 0x1F;

      // printf("Current CPSR value 0x%08x\n", current_cpsr);
      // printf("MSR write value 0x%08x to CPSR=%u\n", value.u32_value, dest_psr_index);

      // Write the new CPSR/SPSR value
      guest->guest_register_write(dest_psr_index, 4, value);

      // Switch modes if necessary
      const bool write_to_cpsr = which.u32_value == 0;
      if (current_mode != new_mode && write_to_cpsr) {
        arm7di->mode_switch(ProcessorMode { u8(current_mode) },
                            ProcessorMode { u8(new_mode) });
      }

      return fox::Value { .u32_value = 0 };
    },
    const_u32(which_psr),
    value);
}

void
Arm7DIAssembler::opcode_data_processing(Arm7DIAssembler::Context &context)
{
  const OpcodeDataProcessing op { .raw = context.info.word };
  assert(op.fixed0 == 0b00);

  const Operand CPSR      = read_reg(Arm7DIRegisterIndex_CPSR);
  const Operand cpsr_c_32 = _and(shiftr(CPSR, const_u32(CPSR_C_INDEX)), const_u32(1));
  const Operand cpsr_c_64 = _and(bitcast(Type::Integer64, cpsr_c_32), const_u64(1));
  // const Operand is_opcode_executed = check_condition_code(CPSR, op.cond);

  // Second operand depends on the [I]mmediate bit.
  Operand op1_32 = read_reg(op.Rn);
  Operand op2_32;
  Operand shift_logic_carry_out;
  opcode1_decode_op2_reg(op, op2_32, shift_logic_carry_out);

  enum DataProcSubOperation
  {
    AND = 0,
    EOR,
    SUB,
    RSB,
    ADD,
    ADC,
    SBC,
    RSC,
    TST,
    TEQ,
    CMP,
    CMN,
    ORR,
    MOV,
    BIC,
    MVN
  };

  // Handle MRS/MSR 'PSR instructions' which are encoded as the test/compare
  // instructions but with the 'set status' bit as zero. Encodings on pg 37.
  // clang-format off

  const bool is_mrs        = ((op.raw & 0b1111101111110000111111111111) == 
                                        0b0001000011110000000000000000);
  const bool is_msr_simple = ((op.raw & 0b1111101111111111111111110000) == 
                                        0b0001001010011111000000000000);
  const bool is_msr_flag   = ((op.raw & 0b1101101111111111000000000000) == 
                                        0b0001001010001111000000000000);
  // clang-format oon
  if(is_mrs) {
    const u32 psr_index = (op.raw >>22) & 1; // CPSR=0 or SPSR_current_mode=1
    const Operand psr = read_reg(Arm7DIRegisterIndex_CPSR + psr_index);
    write_reg(op.Rd, psr);

    // Advance PC
    write_reg(Arm7DIRegisterIndex_PC, const_u32(context.info.address + 4));
    // printf("MRS: CPSR=%u, dest=%u\n", psr_index, op.Rd);
    return;
  }
  if(is_msr_simple) {
    // printf("MSR simple\n");
    const u32 which_psr = (op.raw >>22) & 1; // CPSR=0 or SPSR_current_mode=1
    const Operand source_reg_value = read_reg(op.raw & 0b1111);
    handle_msr_write(which_psr, source_reg_value);

    write_reg(Arm7DIRegisterIndex_PC, const_u32(context.info.address + 4));
    return;
  }

  if (is_msr_flag) {
    // In this case, only the flag bits are effected. Mode is not changed.

    // printf("MSR\n");
    const u32 which_psr = (op.raw >>22) & 1; // CPSR=0 or SPSR_current_mode=1
    const u32 I = (op.raw >> 25) & 1;

    if (I) {
      const u32 rot_amount = (op.raw >> 8) & 0b1111;
      const u32 imm = rotate_right(op.raw & 0xff, rot_amount * 2);

      // printf("MSR I=1, imm=0x%08x\n", imm);

      const Operand old_psr_value = read_reg(Arm7DIRegisterIndex_CPSR + which_psr);
      const Operand new_psr_value = _or(_and(old_psr_value, const_u32(0x0fff'ffff)), const_u32(imm & 0xf000'0000));
      write_reg(Arm7DIRegisterIndex_CPSR + which_psr, new_psr_value);
      
    } else {
      throw std::logic_error("Unimplemented MSR I=0");
    }

    // Advance PC
    write_reg(Arm7DIRegisterIndex_PC, const_u32(context.info.address + 4));
    
    return;
  }

  // General data processing

  bool is_logical_op = false;

  // bool carry = false, zero = false, negative = false;
  Operand temp;
  Operand overflow = const_bool(false);

  Operand result_64 = const_u64(0);
  Operand op1_64 = _and(bitcast(Type::Integer64, op1_32), const_u64(0xffff'ffff));
  Operand op2_64 = _and(bitcast(Type::Integer64, op2_32), const_u64(0xffff'ffff));

  Operand is_carry = const_bool(false);

  switch (op.opcode) {
    case TST:
    case AND:
      is_logical_op = true;
      result_64 = _and(op1_64, op2_64);
      break;
    case TEQ:
    case EOR:
      is_logical_op = true;
      result_64 = _xor(op1_64, op2_64);
      break;
    case RSB: // Same as subtraction, but swaps args
      temp = op1_32;
      op1_32 = op2_32;
      op2_32 = temp;
      [[fallthrough]];
    case CMP:
    case SUB:
      // m_registers.c = IS_NOTBORROW(tmp, operand, operand2);
      // m_registers.v = IS_SUBOVERFLOW(tmp, operand, operand2);

      // 0x20 - 1
      // borrow(a - b) => b > a
      // notborrow => b <= a
      result_64 = sub(op1_32, op2_32);
      result_64 = bitcast(Type::Integer64, result_64); // hack

      is_carry = cmp_lte(op2_32, op1_32);
      overflow = call(Type::Bool, subtraction_overflows_u32, op1_32, op2_32);

      // is_carry = cmp_gt(shiftr(result_64, const_u64(32)), const_u64(0));
      
      // overflow = _and(_xor(op1_64, op2_64), _xor(op1_64, result_64));
      // overflow = _and(shiftr(overflow, const_u64(31)), const_u64(1));
      // overflow = _not(cmp_eq(overflow, const_u64(0)));
      break;
    case CMN:
    case ADD:
      result_64 = add(op1_64, op2_64);
      overflow = call(Type::Bool, addition_overflows_u32, op1_64, op2_64);
      is_carry = cmp_gt(result_64, const_u64(0xffff'ffff));
      break;
    case ADC:
      temp = add(op2_64, cpsr_c_64);
      result_64 = add(op1_64, temp);
      overflow = call(Type::Bool, addition_overflows_u32, op1_64, temp);
      // print out result_64
      call(Type::Integer32, [](fox::Guest *guest, fox::Value x) {
        // printf("result_64=0x%016x\n", x.u64_value);
        return fox::Value { .u32_value = 0 };
      }, result_64);

      temp = shiftr(result_64, const_u64(32));
      call(Type::Integer32, [](fox::Guest *guest, fox::Value x) {
        // printf("temp=0x%016x\n", x.u64_value);
        return fox::Value { .u32_value = 0 };
      }, temp);

      is_carry = cmp_gt(temp, const_u64(0));
      call(Type::Integer32, [](fox::Guest *guest, fox::Value x) {
        // printf("is_carry=%u\n", x.bool_value);
        return fox::Value { .u32_value = 0 };
      }, is_carry);
      break;
    case RSC:
      temp = op1_64;
      op1_64 = op2_64;
      op2_64 = temp;
      [[fallthrough]];
    case SBC:
      // SBC = Op1 - Op2 + C - 1  (pg 29)
      //     = Op1 + (~Op2 + 1) + C - 1
      //     = Op1 + ~Op2 + C

      temp = add(op1_64, _and(_not(op2_64), const_u64(0xffff'ffff)));
      result_64 = add(temp, cpsr_c_64);

      // V = { (op1 ^ op2) & (op1 ^ result) & 0x8000'0000 } != 0
      overflow = _and(_and(_xor(op1_64, op2_64), _xor(op1_64, result_64)), const_u64(0x8000'0000));
      overflow = _not(cmp_eq(overflow, const_u64(0)));

      // Check if the overall addition       
      is_carry = cmp_gt(result_64, const_u64(0xffff'ffff));

      break;
    case ORR:
      is_logical_op = true;
      result_64 = _or(op1_64, op2_64);
      break;
    case MOV:
      is_logical_op = true;
      result_64 = op2_64;
      break;
    case BIC:
      is_logical_op = true;
      result_64 = _and(op1_64, _not(op2_64));
      break;
    case MVN:
      is_logical_op = true;
      result_64 = _not(op2_64);
      break;
    default:
      throw std::logic_error("Unimplemented");
  }

  // Get the lower 32 bits which are the typical result. We'll need the higher bits
  // later for some flag calculations.
  const Operand result_32 = bitcast(Type::Integer32, result_64);

  // All instructions except TST/TEQ/CMP/CMN write their results to Rd
  if (op.opcode < 0b1000 || op.opcode >= 0b1100) {
    write_reg(op.Rd, result_32);
  }

#if 1
  // Certain codes set flags in the CPSR regardless of S bit
  const bool is_cond_op = op.opcode == TST || op.opcode == TEQ || op.opcode == CMP || op.opcode == CMN;

  //
  if (op.S && op.Rd == 15 && !is_cond_op) {
    call(fox::ir::Type::Integer32,[](fox::Guest *guest) {
        Arm7DI *const arm7di = static_cast<Arm7DI *>(guest);

        const u32 cpsr = guest->guest_register_read(Arm7DIRegisterIndex_CPSR, 4).u32_value;
        const u32 spsr = guest->guest_register_read(Arm7DIRegisterIndex_SPSR, 4).u32_value;

        const ProcessorMode cpsr_mode { u8(cpsr & 0x1F) };
        const ProcessorMode spsr_mode { u8(spsr & 0x1F) };

        // Switch modes if necessary
        if (cpsr_mode != spsr_mode) {
          arm7di->mode_switch(cpsr_mode, spsr_mode);
        }

        // Set CPSR to saved (SPSR)
        guest->guest_register_write(Arm7DIRegisterIndex_CPSR, 4, fox::Value{.u32_value=spsr});

        return fox::Value { .u32_value = 0 };
      });
  }
  else if (op.S || is_cond_op){
      const auto is_result_zero = cmp_eq(result_32, const_u32(0));
      const auto is_result_negative = _not(cmp_eq(_and(result_32, const_u32(0x80000000)), const_u32(0)));

      auto cpsr = read_reg(Arm7DIRegisterIndex_CPSR);
      cpsr = bsc(cpsr, is_result_zero, const_u32(CPSR_Z_INDEX));
      cpsr = bsc(cpsr, is_result_negative, const_u32(CPSR_N_INDEX));

      // V flag is only updated on arithmetic operations (pg 30)
      // V is not affected by logical operations
      // C flag is set to ALU carry out of bit 31 for arithmetic operations
      // C flag is set to carry out of barrel shifter for logical operations
      if (!is_logical_op) {
        cpsr = bsc(cpsr, overflow, const_u32(CPSR_V_INDEX));
        cpsr = bsc(cpsr, is_carry, const_u32(CPSR_C_INDEX));
      } else {
        const Operand out_c = _not(cmp_eq(shift_logic_carry_out, const_u32(0)));
        cpsr = bsc(cpsr, out_c, const_u32(CPSR_C_INDEX));
      }

      write_reg(Arm7DIRegisterIndex_CPSR, cpsr);
  }

#else

  // Set flags of CPSR
  if (op.S) {
    if (op.Rd == 15) {
      // S flag and Rd=15 is a special case, where the SPSR is copied to the 
      // CPSR / mode switch back out of an exception handler
      
      call(fox::ir::Type::Integer32,[](fox::Guest *guest) {
        Arm7DI *const arm7di = static_cast<Arm7DI *>(guest);

        const u32 cpsr = guest->guest_register_read(Arm7DIRegisterIndex_CPSR, 4).u32_value;
        const u32 spsr = guest->guest_register_read(Arm7DIRegisterIndex_SPSR, 4).u32_value;

        const ProcessorMode cpsr_mode { u8(cpsr & 0x1F) };
        const ProcessorMode spsr_mode { u8(spsr & 0x1F) };

        // Switch modes if necessary
        if (cpsr_mode != spsr_mode) {
          arm7di->mode_switch(cpsr_mode, spsr_mode);
        }

        // Set CPSR to saved (SPSR)
        guest->guest_register_write(Arm7DIRegisterIndex_CPSR, 4, fox::Value{.u32_value=spsr});

        return fox::Value { .u32_value = 0 };
      });
      
    } else {
      // CPSR is only updated if S && Rd != 15

      const auto is_result_zero = cmp_eq(result_32, const_u32(0));
      const auto is_result_negative = _not(cmp_eq(_and(result_32, const_u32(0x80000000)), const_u32(0)));

      auto cpsr = read_reg(Arm7DIRegisterIndex_CPSR);
      cpsr = bsc(cpsr, is_result_zero, const_u32(CPSR_Z_INDEX));
      cpsr = bsc(cpsr, is_result_negative, const_u32(CPSR_N_INDEX));

      // V flag is only updated on arithmetic operations (pg 30)
      // V is not affected by logical operations
      // C flag is set to ALU carry out of bit 31 for arithmetic operations
      // C flag is set to carry out of barrel shifter for logical operations
      if (!is_logical_op) {
        cpsr = bsc(cpsr, overflow, const_u32(CPSR_V_INDEX));
        cpsr = bsc(cpsr, is_carry, const_u32(CPSR_C_INDEX));
      } else {
        const Operand out_c = _not(cmp_eq(shift_logic_carry_out, const_u32(0)));
        cpsr = bsc(cpsr, out_c, const_u32(CPSR_C_INDEX));
      }

      write_reg(Arm7DIRegisterIndex_CPSR, cpsr);
    }
  }

  #endif

  // Advance PC
  if(op.Rd != Arm7DIRegisterIndex_PC) {
    write_reg(Arm7DIRegisterIndex_PC, const_u32(context.info.address + 4));
  }
}

void
Arm7DIAssembler::opcode_multiply(Arm7DIAssembler::Context &context)
{
  // pg. 40
  const OpcodeMultiply op { .raw = context.info.word };
  Operand result;

  // "The destination register (Rd) should not be the same as the operand
  // register (Rm), as Rd is used to hold intermediate values and Rm is used
  // repeatedly during multiply. A MUL will give a zero result if RM=Rd, and an
  // MLA will give a meaningless result. R15 shall not be used as an operand or
  // as the destination register."
  assert(op.Rd != 15);
  assert(op.Rd != op.Rm);
  if(op.Rd == op.Rm) {
    result = const_u32(0);
  }

  if(op.A) {
    const Operand Rm = read_reg(op.Rm);
    const Operand Rs = read_reg(op.Rs);
    const Operand Rn = read_reg(op.Rn);
    result = add(mul(Rm, Rs), Rn);
  } else {
    const Operand Rm = read_reg(op.Rm);
    const Operand Rs = read_reg(op.Rs);
    result = mul(Rm, Rs);
  }

  // Write result
  write_reg(op.Rd, result);

  if (op.S) {
    const auto is_result_zero = cmp_eq(result, const_u32(0));
    const auto is_result_negative = _and(result, const_u32(0x80000000));

    // Carry and Overflow flags are not affected by multiplication instructions

    auto cpsr = read_reg(Arm7DIRegisterIndex_CPSR);
    cpsr = bsc(cpsr, is_result_zero, const_u32(CPSR_Z_INDEX));
    cpsr = bsc(cpsr, any_bits_set(is_result_negative), const_u32(CPSR_N_INDEX));
    write_reg(Arm7DIRegisterIndex_CPSR, cpsr);
  }

  // Advance PC
  write_reg(Arm7DIRegisterIndex_PC, const_u32(context.info.address + 4));
}

fox::ir::Operand Arm7DIAssembler::ldr_byte(Operand address)
{
  return bitcast(Type::Integer32, load(Type::Integer8, address));
}

Operand Arm7DIAssembler::ldr_word(Operand address)
{
  // Handling for misaligned addresses, see page 43.
  // Consider data stored at address 0..3: [ABCD] (little-endian)
  // LDR 0 : reg = [DCBA]
  // LDR 1 : reg = [ADCB]
  // LDR 2 : reg = [BADC]
  // LDR 3 : reg = [CBAD]

  // Of note, mis-aligned LDR will not read beyond the word boundary.

  const Operand aligned = _and(address, const_u32(0xfffffffc));
  Operand data = load(Type::Integer32, aligned);
  data = rotr(data, mul(_and(address, const_u32(0b11)), const_u32(8)) );
  return data;
}

void Arm7DIAssembler::str_byte(Operand address, Operand value)
{
  // STRB modifies the byte at the specified address, and leaves the other
  // bytes in the word unchanged.
  store(address, bitcast(Type::Integer8, value));
}

void Arm7DIAssembler::str_word(Operand address, Operand value)
{
  // STR has no alignment restrictions (Section 4.7.3, pg 44)
  store(address, value);
}

void
Arm7DIAssembler::opcode_single_data_swap(Arm7DIAssembler::Context &context)
{
  const OpcodeSingleDataSwap op { .raw = context.info.word };

  if (op.Rd == 15) {
    throw std::logic_error("Rd cannot be R15 in single data swap");
  }
  if (op.Rm == 15) {
    throw std::logic_error("Rm cannot be R15 in single data swap");
  }
  if (op.Rn == 15) {
    throw std::logic_error("Rn cannot be R15 in single data swap");
  }

  // 1. Load the word from the memory location specified by the base register
  // 2. Store source register value to the memory location specified by the base register
  // 3. Write the value from step 1 to the destination register

  // Called "base" in the ARM manual (pg 55)
  const Operand address = read_reg(op.Rn); 

  // Source register value
  const Operand source_val = read_reg(op.Rm);

  if (op.B) {
    const Operand mem_old = ldr_byte(address);
    write_reg(op.Rd, mem_old);
    str_byte(address, source_val);
  } else {
    const Operand mem_old = ldr_word(address);
    write_reg(op.Rd, mem_old);
    str_word(address, source_val);
  }

  // No care needed for R15 as it is not a valid destination register
  write_reg(Arm7DIRegisterIndex_PC, const_u32(context.info.address + 4));
}

/* Single Data Transfer (LDR, STR) */
void
Arm7DIAssembler::opcode_single_data_transfer(Arm7DIAssembler::Context &context)
{
  const OpcodeSingleDataTransfer op { .raw = context.info.word };

  Operand base_address = read_reg(op.Rn);

  Operand offset;
  if (!op.I) {
    /* Immediate Offset */
    // Sign extend offset from 12 bits to 32 (i12 -> i32)
    // offset = const_u32(extend_sign<12>(op.offset));
    offset = const_u32(op.offset);

  } else {
    // These instructions don't support shifting by a variable amount.
    assert(extract_bits(op.raw, 4, 4) == 0 &&
           "LDR/STR instructions do NOT support shift by amount specified in a register");

    /* Offset Register */
    const auto Rm = read_reg(extract_bits(op.raw, 3, 0));
    const auto shift_amount = const_u32(extract_bits(op.raw, 11, 7));
    const u32 shift_type = extract_bits(op.raw, 6, 5);

    // Perform shift operation on Rm, write to offset, carry_out result is
    // unused in these instructions.
    Operand carry_out_unused;
    shift_logic(Rm, shift_amount, shift_type, offset, carry_out_unused);
  }

  // Offset is either positive or negative based on U bit.
  offset = select(const_bool(op.U), neg32(offset), offset);

  const auto base_plus_offset = add(base_address, offset);

  // Pre/Post-indexed offset
  const auto target = op.P ? base_plus_offset : base_address;

  // Note: "Therefore post-indexed data transfers always write back the modified base."
  if (op.W || (!op.P)) {
    /* Enable write-back */
    write_reg(op.Rn, base_plus_offset);
  }

  if (op.L) {
    if (op.B) {
      write_reg(op.Rd, ldr_byte(target));
    } else {
      write_reg(op.Rd, ldr_word(target));
    }
  } else {
    if (op.B) {
      str_byte(target, read_reg(op.Rd));
    } else {
      str_word(target, read_reg(op.Rd));
    }
  }

  const bool op_modified_pc = op.L && (op.Rd == 15);
  if (!op_modified_pc) {
    write_reg(Arm7DIRegisterIndex_R15, const_u32(context.info.address + 4));
  }
}

void
Arm7DIAssembler::opcode_undefined(Arm7DIAssembler::Context &)
{
  // const OpcodeUndefined op { .raw = context.info.word };
  throw std::logic_error("Unimplemented Opcode5");
}

fox::Value fox_read_register_user(fox::Guest *guest, fox::Value reg_index)
{
  const u32 current_mode = guest->guest_register_read(Arm7DIRegisterIndex_CPSR, 4).u32_value & 0x1F;
  const bool currently_user_mode = current_mode == 0x10;
  if (currently_user_mode) {
    return guest->guest_register_read(reg_index.u32_value, 4);
  } else {
    // If we're not in user mode, we need to read the register from the user bank
    Arm7DI *const arm7di = static_cast<Arm7DI *>(guest);
    return fox::Value{.u32_value = arm7di->read_register_user(reg_index.u32_value)};
  }
}

fox::Value fox_store_register_user(fox::Guest *guest, fox::Value reg_index, fox::Value value)
{
  Arm7DI *const arm7di = static_cast<Arm7DI *>(guest);
  arm7di->write_register_user(reg_index.u32_value, value.u32_value);
  return fox::Value{.u32_value = 0};
}

void
Arm7DIAssembler::opcode_block_data_transfer(Arm7DIAssembler::Context &context)
{
  const OpcodeBlockDataTransfer op { .raw = context.info.word };

  if(op.list == 0) {
    throw std::logic_error("Block Data Transfer with empty register list");
  }

  // Compute the set of registers being stored/loaded
  const bool list_contains_rn = (op.list & (1 << op.Rn));
  const bool list_contains_r15 = (op.list & (1 << Arm7DIRegisterIndex_R15));

  std::vector<u32> register_indexes;
  for(u32 i=0; i<16; ++i) {
    if(op.list & (1 << i)) {
      // printf("Block Data Transfer: Register %u\n", i);
      register_indexes.push_back(Arm7DIRegisterIndex_R0 + i);
    }
  }

  const bool Up = op.U;
  const bool PreIndex = op.P;
  const bool Load = op.L;
  const bool S = op.S;

  // printf("Block Data Transfer: Up=%u, PreIndex=%u, Load=%u, S=%u\n", Up, PreIndex, Load, S);

  // const Operand current_cpsr = read_reg(Arm7DIRegisterIndex_CPSR);
  // const Operand current_spsr = read_reg(Arm7DIRegisterIndex_SPSR);
  // const Operand currently_in_user_mode = cmp_eq(_and(current_cpsr, const_u32(0x1F)), const_u32(0x10));

  Operand base_address = read_reg(op.Rn);

  bool should_mode_switch = false;

  using Action = std::function<void(Operand address, u32 reg_index)>;
  
  const Action action_store = [&](Operand address, u32 reg_index) {
    if (S) {
      // See page 51 explanation for S bit. We're store to memory the user mode register
      const Operand reg_val = call(Type::Integer32, fox_read_register_user, const_u32(reg_index));
      str_word(address, reg_val);      
    } else {
      Operand reg_value;
      // "Whenever R15 is stored to memory the stored value is the address of the STM instruction plus 12."
      if (reg_index == Arm7DIRegisterIndex_R15) {
        reg_value = const_u32(context.info.address + 12);
      } else {
         reg_value = read_reg(reg_index);
      }
      str_word(address, reg_value);
    }
  };

  const Action action_load = [&](Operand address, u32 reg_index) {
    if (!list_contains_r15 && S) {
      // In this case, we need to write to the user bank instead of the current mode
      // write_reg(reg_index, ldr_word(address));
      const Operand value = ldr_word(address);
      call(Type::Integer32, fox_store_register_user, const_u32(reg_index), value);
    } else {
      write_reg(reg_index, ldr_word(address));
    }

    if(S && reg_index == Arm7DIRegisterIndex_R15) {
      // LDM with R15 in transfer list and S bit set (Mode changes)
      // If the instruction is a LDM then SPSR_<mode> is transferred to CPSR at the same time as R15 is loaded.
      should_mode_switch = true;
    }
  };

  Action action = op.L ? action_load : action_store;

  if (Up) {
    for(u32 reg_index : register_indexes)
    {
      if(!PreIndex) {
        action(base_address, reg_index);
        base_address = add(base_address, const_u32(4));
      }
      else {
        base_address = add(base_address, const_u32(4));
        action(base_address, reg_index);
      }
    }
  } else {
    if(!PreIndex) {
      const Operand start_address = sub(base_address, const_u32(4*register_indexes.size()));
      Operand store_address = start_address;
      for(u32 reg_index : register_indexes)
      {
        store_address = add(store_address, const_u32(4));
        action(store_address, reg_index);
      }
      base_address = start_address;
    } else {
      const Operand start_address = sub(base_address, const_u32(4*register_indexes.size()));
      Operand store_address = start_address;
      for(u32 reg_index : register_indexes)
      {
        action(store_address, reg_index);
        store_address = add(store_address, const_u32(4));
      }
      base_address = start_address;
    }
  }

  // Consider the case at the end of FIQ handler in Dreamcast Boot Audio Driver
  //   ldmia sp!,{pc}^ 
  // In this case, PC from before interrupt is loaded from the stack into the old mode
  // but we do /not/ want to trash a register in the destination mode. 
  // const bool mode_changed = Load && list_contains_r15 && S;

  // Optional write-back, only if Rn not in the list (section 4.8.6)
  if(op.W && (!Load || !list_contains_rn)) {
    write_reg(op.Rn, base_address);
  }

  // At this point, if S=1 and R15 was loaded, we need to potentially mode switch if SPSR had a different mode. 
  // Note, the order here is important. The writeback should have happened to the old mode, not the new mode!
  if (should_mode_switch) {
    // Also mode switch
    call(fox::ir::Type::Integer32, [](fox::Guest *guest) {
      Arm7DI *const arm7di = static_cast<Arm7DI *>(guest);

      const u32 cpsr = guest->guest_register_read(Arm7DIRegisterIndex_CPSR, 4).u32_value;
      const u32 spsr = guest->guest_register_read(Arm7DIRegisterIndex_SPSR, 4).u32_value;

      // Set CPSR to saved (SPSR)
      guest->guest_register_write(Arm7DIRegisterIndex_CPSR, 4, fox::Value{.u32_value=spsr});

      // Switch modes if necessary
      const ProcessorMode cpsr_mode { u8(cpsr & 0x1F) };
      const ProcessorMode spsr_mode { u8(spsr & 0x1F) };

      // printf("Block Data Transfer: Mode switch from %u to %u\n", cpsr_mode, spsr_mode);

      if (cpsr_mode != spsr_mode) {
        arm7di->mode_switch(cpsr_mode, spsr_mode);
      }
      return fox::Value { .u32_value = 0 };
    });
  }

  // Advance PC
  const bool load_modified_pc = (list_contains_r15 && Load);
  if(!load_modified_pc) {
    write_reg(Arm7DIRegisterIndex_PC, const_u32(context.info.address + 4));
  }
}

/* Branch + Branch With Link */
void
Arm7DIAssembler::opcode_branch(Arm7DIAssembler::Context &context)
{
  const OpcodeBranch op { .raw = context.info.word };
  assert(op.fixed0 == 0b101);

  i32 disp = extend_sign<24u>(op.offset << 2);
  // Note additional 4 bytes: "The branch offset must take account of the
  // prefetch operation, which causes the PC to be 2 words (8 bytes) ahead of
  // the current instruction." 
  const u32 target = (u32)((i32)context.info.address + disp + 8);
  // if (arm7di_debug_enabled()) {
  //   printf("arm7di Branch info.addr=0x%08x + disp=0x%08x + 8 = 0x%08x\n",
  //          (i32)context.info.address,
  //          disp,
  //          target);
  // }

  write_reg(Arm7DIRegisterIndex_PC, const_u32(target));
  if (op.L) {
    // "... The PC value written into R14 [contains] the instruction following the
    // branch and link instruction."
    write_reg(Arm7DIRegisterIndex_LR, const_u32(context.info.address + 4u));
  }
}

// Opcode 8,9,10 represent coprocessor, which we don't support currently

void
Arm7DIAssembler::opcode_software_interrupt(Arm7DIAssembler::Context &)
{
  // const OpcodeSoftwareInterrupt op { .raw = context.info.word };
  //   raise_exception(EXC_SOFTWARE);
  throw std::logic_error("Unimplemented Opcode11: Software Interrupt");
}

} // namespace guest::arm7di
