#pragma once

#include "shared/types.h"

bool arm7di_debug_enabled();

namespace guest::arm7di {

enum class OpcodeClass
{
  DataProcessing = 1,
  Multiply,
  SingleDataSwap,
  SingleDataTransfer,
  Undefined,
  BlockDataTransfer,
  Branch,
  CoprocDataTransfer,
  CoprocDataOperation,
  CoprocRegisterTransfer,
  SoftwareInterrupt,
};

OpcodeClass decode_opcode_class(u32 word);

/* Conditional Execution Encoding */
enum OpcodeCondition : u8
{
  EQ = 0b0000, /* (Z) */
  NE = 0b0001, /* (!Z) */
  CS = 0b0010, /* (C) */
  CC = 0b0011, /* (!C) */
  MI = 0b0100, /* (N) */
  PL = 0b0101, /* (!N) */
  VS = 0b0110, /* (V) */
  VC = 0b0111, /* (!V) */
  HI = 0b1000, /* (C && !Z) */
  LS = 0b1001, /* (!C || Z) */
  GE = 0b1010, /* (N && V) || (!N && !V) */
  LT = 0b1011, /* (N && !V) || (!N && V) */
  GT = 0b1100, /* (!Z) && ((N && V) || !N && !V) */
  LE = 0b1101, /* (Z) || (N && !V) || (!N && V)  */
  AL = 0b1110, /* Always */
  NV = 0b1111  /* Never */
};

/* Processor Mode Control */
enum ProcessorMode : u8
{
  Mode_USR = 0b10000,
  Mode_FIQ = 0b10001,
  Mode_SVC = 0b10011,
  Mode_ABT = 0b10111,
  Mode_IRQ = 0b10010,
  Mode_UND = 0b11011
};

/** Exception indexes */
enum Exception : unsigned
{
  Exception_Reset = 0,
  Exception_UndefinedInstruction = 1,
  Exception_SoftwareInterrupt = 2,
  Exception_PrefetchAbort = 3,
  Exception_DataAbort = 4,
  Exception_Reserved = 5,
  Exception_IRQ = 6,
  Exception_FIQ = 7,
  Exception_NUM_EXCEPTIONS,
};

/** Exception handler addresses, indexable by Exception enum */
inline constexpr u32 kExceptionHandlers[] = {
  0x00000000, /* Reset */
  0x00000004, /* Undefined Instruction */
  0x00000008, /* Software Interrupt */
  0x0000000C, /* Prefetch Abort */
  0x00000010, /* Data Abort */
  0x00000014, /* Reserved */
  0x00000018, /* IRQ */
  0x0000001C, /* FIQ */
};

inline constexpr ProcessorMode kExceptionModes[] = {
  Mode_SVC, /* Reset */
  Mode_UND, /* Undefined Instruction */
  Mode_SVC, /* Software Interrupt */
  Mode_ABT, /* Prefetch Abort */
  Mode_ABT, /* Data Abort */
  Mode_UND, /* Reserved */
  Mode_IRQ, /* IRQ */
  Mode_FIQ, /* FIQ */
};

struct Arm7DIInstructionInfo {
  u32 address = UINT32_MAX;
  u32 word = 0;
  u32 flags = 0;
  u32 cycles = 0;
};

/*! Data Processing and PSR Transfer opcodes */
union OpcodeDataProcessing {
  struct {
    u32 operand2 : 12;
    u32 Rd : 4;
    u32 Rn : 4;
    u32 S : 1;
    u32 opcode : 4;
    u32 I : 1;
    u32 fixed0 : 2; /* Must be 0b00 */
    u32 cond : 4;
  };
  u32 raw;
};

/*! Multiply opcode */
union OpcodeMultiply {
  struct {
    u32 Rm : 4;
    u32 fixed0 : 4; /* Must be 0b1001 */
    u32 Rs : 4;
    u32 Rn : 4;
    u32 Rd : 4;
    u32 S : 1;
    u32 A : 1;
    u32 fixed1 : 6; /* Must be 0b000000 */
    u32 cond : 4;
  };

  u32 raw;
};

/*! Single Data Swap opcode */
union OpcodeSingleDataSwap {
  struct {
    u32 Rm : 4;
    u32 fixed0 : 4; /* Must be 0b1001 */
    u32 fixed1 : 4; /* Must be 0b0000 */
    u32 Rd : 4;
    u32 Rn : 4;
    u32 fixed2 : 2; /* Must be 0b00 */
    u32 B : 1;
    u32 fixed3 : 5; /* Must be 0b00010 */
    u32 cond : 4;
  };

  u32 raw;
};

/* Single Data Transfer opcode */
union OpcodeSingleDataTransfer {
  struct {
    u32 offset : 12;
    u32 Rd : 4;
    u32 Rn : 4;
    u32 L : 1;
    u32 W : 1;
    u32 B : 1;
    u32 U : 1;
    u32 P : 1;
    u32 I : 1;
    u32 fixed1 : 2; /* Must be 0b01 */
    u32 cond : 4;
  };

  u32 raw;
};

/* Undefined opcode */
union OpcodeUndefined {
  struct {
    u32 x0 : 4;     /* Any value */
    u32 fixed0 : 1; /* Must be 0b1 */
    u32 x1 : 20;    /* Any value */
    u32 fixed1 : 3; /* Must be 0b011 */
    u32 cond : 4;
  };

  u32 raw;
};

/* Block Data Transfer opcode */
union OpcodeBlockDataTransfer {
  struct {
    u32 list : 16;
    u32 Rn : 4;
    u32 L : 1;
    u32 W : 1;
    u32 S : 1;
    u32 U : 1;
    u32 P : 1;
    u32 fixed0 : 3; /* Must be 0b100 */
    u32 cond : 4;
  };

  u32 raw;
};

/* Branch opcode */
union OpcodeBranch {
  struct {
    u32 offset : 24;
    u32 L : 1;
    u32 fixed0 : 3; /* Must be 0b101 */
    u32 cond : 4;
  };

  u32 raw;
};

/* 8, 9, 10 types are for coprocessor. Do we need them? */

/* Software Interrupt opcode  */
union OpcodeSoftwareInterrupt {
  struct {
    u32 x0 : 24;    /* Ignored by CPU */
    u32 fixed0 : 4; /* Must be 0b1111 */
    u32 cond : 4;
  };

  u32 raw;
};

enum Arm7DIRegisterIndex
{
  Arm7DIRegisterIndex_R0 = 0,
  Arm7DIRegisterIndex_R1,
  Arm7DIRegisterIndex_R2,
  Arm7DIRegisterIndex_R3,
  Arm7DIRegisterIndex_R4,
  Arm7DIRegisterIndex_R5,
  Arm7DIRegisterIndex_R6,
  Arm7DIRegisterIndex_R7,
  Arm7DIRegisterIndex_R8,
  Arm7DIRegisterIndex_R9,
  Arm7DIRegisterIndex_R10,
  Arm7DIRegisterIndex_R11,
  Arm7DIRegisterIndex_R12,
  Arm7DIRegisterIndex_R13,
  Arm7DIRegisterIndex_R14,
  Arm7DIRegisterIndex_R15,
  Arm7DIRegisterIndex_CPSR,
  Arm7DIRegisterIndex_SPSR,
  Arm7DIRegisterIndex_NUM_REGISTERS,

  // aliases
  Arm7DIRegisterIndex_PC = Arm7DIRegisterIndex_R15,
  Arm7DIRegisterIndex_LR = Arm7DIRegisterIndex_R14,
  Arm7DIRegisterIndex_SP = Arm7DIRegisterIndex_R13,
};

/* Program Status Register */
union CPSR_bits {
  struct {
    u32 M : 5;      /* Operating Mode */
    u32 rsvd0 : 1;  /* Unused */
    u32 F : 1;      /* FIQ interrupt disable */
    u32 I : 1;      /* IRQ disable */
    u32 rsvd1 : 20; /* Unused */
    u32 V : 1;
    u32 C : 1;
    u32 Z : 1;
    u32 N : 1;
  };
  u32 raw;
};
static_assert(sizeof(CPSR_bits) == 4);

} // namespace guest::arm7di
