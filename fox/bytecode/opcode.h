#pragma once

#include "fox/fox_types.h"

namespace fox {
namespace bytecode {

/*
 * == Overview of bytecode instruction format ==
 *
 * The bytecode uses a variable length encoding. The length of each instruction
 * depends on the number of input/output registers and the presence of immediate
 * data. The minimum size is 16 bits and the maximum size is 80 bits. The first
 * 8 bits choose the type-specific opcode and determine the instruction size.
 */

enum class Opcodes : u8 {
  Constant8,              /* Load 1 byte immediate constant. */
  Constant16,             /* Load 2 byte immediate constant. */
  Constant32,             /* Load 4 byte immediate constant. */
  Constant64,             /* Load 8 byte immediate constant. */
  ExtendConstant8,        /* Load 1 byte immediate constant and sign extend. */
  ExtendConstant16,       /* Load 2 byte immediate constant and sign extend. */
  ExtendConstant32,       /* Load 4 byte immediate constant and sign extend. */
  ReadRegister8,          /* Read guest register. */
  ReadRegister16,         /* Read guest register. */
  ReadRegister32,         /* Read guest register. */
  ReadRegister64,         /* Read guest register. */
  WriteRegister8,         /* Write guest register. */
  WriteRegister16,        /* Write guest register. */
  WriteRegister32,        /* Write guest register. */
  WriteRegister64,        /* Write guest register. */
  Load8,                  /* Load and 0-extend 1 byte from guest memory. */
  Load16,                 /* Load and 0-extend 2 bytes from guest memory. */
  Load32,                 /* Load and 0-extend 4 bytes from guest memory. */
  Load64,                 /* Load 8 bytes from guest memory. */
  Store8,                 /* Store 1 byte to guest memory. */
  Store16,                /* Store 2 bytes to guest memory. */
  Store32,                /* Store 4 bytes to guest memory. */
  Store64,                /* Store 8 bytes to guest memory. */
  RotateRight8,           /* Rotate lower 8 register bits right. */
  RotateRight16,          /* Rotate lower 16 register bits right. */
  RotateRight32,          /* Rotate lower 32 register bits right. */
  RotateRight64,          /* Rotate register bits right. */
  RotateLeft8,            /* Rotate lower 8 register bits left. */
  RotateLeft16,           /* Rotate lower 16 register bits left. */
  RotateLeft32,           /* Rotate lower 32 register bits left. */
  RotateLeft64,           /* Rotate register bits left. */
  ShiftRight8,            /* Logical shift lower 8 register bits right. */
  ShiftRight16,           /* Logical shift lower 16 register bits right. */
  ShiftRight32,           /* Logical shift lower 32 register bits right. */
  ShiftRight64,           /* Logical shift register bits right. */
  ShiftLeft8,             /* Logical shift lower 8 register bits left. */
  ShiftLeft16,            /* Logical shift lower 16 register bits left. */
  ShiftLeft32,            /* Logical shift lower 32 register bits left. */
  ShiftLeft64,            /* Logical shift register bits left. */
  ArithmeticShiftRight8,  /* Arithmetic shift lower 8 register bits left. */
  ArithmeticShiftRight16, /* Arithmetic shift lower 16 register bits left. */
  ArithmeticShiftRight32, /* Arithmetic shift lower 32 register bits left. */
  ArithmeticShiftRight64, /* Arithmetic shift register bits left. */
  And8,                   /* For lower 8 bits: rA = (rB & rC) */
  And16,                  /* For lower 16 bits: rA = (rB & rC) */
  And32,                  /* For lower 32 bits: rA = (rB & rC) */
  And64,                  /* rA = (rB & rC) */
  AndBool,                /* rA = (rB & rC) (boolean values) */
  Or8,                    /* For lower 8 bits: rA = (rB | rC) */
  Or16,                   /* For lower 16 bits: rA = (rB | rC) */
  Or32,                   /* For lower 32 bits: rA = (rB | rC) */
  Or64,                   /* rA = (rB | rC) */
  OrBool,                 /* rA = (rB | rC) (boolean values) */
  Xor8,                   /* For lower 8 bits: rA = (rB ^ rC) */
  Xor16,                  /* For lower 16 bits: rA = (rB ^ rC) */
  Xor32,                  /* For lower 32 bits: rA = (rB ^ rC) */
  Xor64,                  /* rA = (rB ^ rC) */
  Not8,                   /* For lower 8 bits: rA = ~rB */
  Not16,                  /* For lower 16 bits: rA = ~rB */
  Not32,                  /* For lower 32 bits: rA = ~rB */
  Not64,                  /* rA = ~rB */
  NotBool,                /* rA = !rB */
  BitSetClear8,           /* rA = rB & ~(1 << constant) | (rC << constant) */
  BitSetClear16,          /* rA = rB & ~(1 << constant) | (rC << constant) */
  BitSetClear32,          /* rA = rB & ~(1 << constant) | (rC << constant) */
  BitSetClear64,          /* rA = rB & ~(1 << constant) | (rC << constant) */
  AddInteger,             /* rA = rB + rC (as 64-bit integers) */
  AddFloat32,             /* rA = rB + rC (as 32-bit floats) */
  AddFloat64,             /* rA = rB + rC (as 64-bit floats) */
  SubInteger8,            /* rA = rB - rC (lower 8 bits) */
  SubInteger16,           /* rA = rB - rC (lower 16 bits) */
  SubInteger32,           /* rA = rB - rC (lower 32 bits) */
  SubInteger64,           /* rA = rB - rC */
  SubFloat32,             /* rA = rB - rC (as 32-bit floats) */
  SubFloat64,             /* rA = rB - rC (as 64-bit floats) */
  MultiplyI8,             /* rA = rB * rC (lower 8 bits, signed) */
  MultiplyI16,            /* rA = rB * rC (lower 16 bits, signed) */
  MultiplyI32,            /* rA = rB * rC (lower 32 bits, signed) */
  MultiplyI64,            /* rA = rB * rC (64 bits, signed) */
  MultiplyU8,             /* rA = rB * rC (lower 8 bits, unsigned) */
  MultiplyU16,            /* rA = rB * rC (lower 16 bits, unsigned) */
  MultiplyU32,            /* rA = rB * rC (lower 32 bits, unsigned) */
  MultiplyU64,            /* rA = rB * rC (64 bits, unsigned) */
  MultiplyF32,            /* rA = rB * rC (32 bit floats) */
  MultiplyF64,            /* rA = rB * rC (64 bit floats) */
  DivideI8,               /* rA = rB / rC (lower 8 bits, signed) */
  DivideI16,              /* rA = rB / rC (lower 16 bits, signed) */
  DivideI32,              /* rA = rB / rC (lower 32 bits, signed) */
  DivideI64,              /* rA = rB / rC (64 bits, signed) */
  DivideU8,               /* rA = rB / rC (lower 8 bits, unsigned) */
  DivideU16,              /* rA = rB / rC (lower 16 bits, unsigned) */
  DivideU32,              /* rA = rB / rC (lower 32 bits, unsigned) */
  DivideU64,              /* rA = rB / rC (64 bits, unsigned) */
  DivideF32,              /* rA = rB / rC (32 bit floats) */
  DivideF64,              /* rA = rB / rC (64 bit floats) */
  SquareRootF32,          /* rA = sqrt(rB) (32 bit floats) */
  SquareRootF64,          /* rA = sqrt(rB) (64 bit floats) */
  Extend8to16,            /* (i16)rA = (i8)rB */
  Extend8to32,            /* (i32)rA = (i8)rB */
  Extend8to64,            /* (i64)rA = (i8)rB */
  Extend16to32,           /* (i32)rA = (i16)rB */
  Extend16to64,           /* (i64)rA = (i16)rB */
  Extend32to64,           /* (i64)rA = (i32)rB */
  Float32to64,            /* (double)rA = (float)rB */
  Float64to32,            /* (float)rA = (double)rB */
  Cast8,                  /* rA = (u8)rB; */
  Cast16,                 /* rA = (u16)rB; */
  Cast32,                 /* rA = (u32)rB; */
  Cast64,                 /* rA = (u64)rB; */
  CastF32toI32,           /* (i32)rA = (float)rB; */
  CastF64toI32,           /* (i32)rA = (double)rB; */
  CastF32toI64,           /* (i64)rA = (float)rB; */
  CastF64toI64,           /* (i64)rA = (double)rB; */
  CastI32toF32,           /* (float)rA = (i32)rB; */
  CastI32toF64,           /* (double)rA = (i32)rB; */
  CastI64toF32,           /* (float)rA = (i64)rB; */
  CastI64toF64,           /* (double)rA = (i64)rB; */
  Test8,                  /* rA = (rB & rC) ? true : false (lower 8 bits) */
  Test16,                 /* rA = (rB & rC) ? true : false (lower 16 bits) */
  Test32,                 /* rA = (rB & rC) ? true : false (lower 32 bits) */
  Test64,                 /* rA = (rB & rC) ? true : false */
  CompareEq8,             /* rA = (rB == rC); (lower 8 bits) */
  CompareEq16,            /* rA = (rB == rC); (lower 16 bits) */
  CompareEq32,            /* rA = (rB == rC); (lower 32 bits) */
  CompareEq64,            /* rA = (rB == rC); (as 64-bit integers) */
  CompareEqF32,           /* rA = (rB == rC); (as 32-bit floats) */
  CompareEqF64,           /* rA = (rB == rC); (as 64-bit floats) */
  CompareEqBool,          /* rA = (rB == rC); (as booleans) */
  CompareLtI8,            /* rA = (rB == rC); (lower 8 bits, signed) */
  CompareLtI16,           /* rA = (rB == rC); (lower 16 bits, signed) */
  CompareLtI32,           /* rA = (rB == rC); (lower 32 bits, signed) */
  CompareLtI64,           /* rA = (rB == rC); (as 64-bit integers, signed) */
  CompareLtU8,            /* rA = (rB == rC); (lower 8 bits, unsigned) */
  CompareLtU16,           /* rA = (rB == rC); (lower 16 bits, unsigned) */
  CompareLtU32,           /* rA = (rB == rC); (lower 32 bits, unsigned) */
  CompareLtU64,           /* rA = (rB == rC); (as 64-bit integers, unsigned) */
  CompareLtF32,           /* rA = (rB == rC); (as 32-bit floats) */
  CompareLtF64,           /* rA = (rB == rC); (as 64-bit floats) */
  CompareLteI8,           /* rA = (rB == rC); (lower 8 bits, signed) */
  CompareLteI16,          /* rA = (rB == rC); (lower 16 bits, signed) */
  CompareLteI32,          /* rA = (rB == rC); (lower 32 bits, signed) */
  CompareLteI64,          /* rA = (rB == rC); (as 64-bit integers, signed) */
  CompareLteU8,           /* rA = (rB == rC); (lower 8 bits, unsigned) */
  CompareLteU16,          /* rA = (rB == rC); (lower 16 bits, unsigned) */
  CompareLteU32,          /* rA = (rB == rC); (lower 32 bits, unsigned) */
  CompareLteU64,          /* rA = (rB == rC); (as 64-bit integers, unsigned) */
  CompareLteF32,          /* rA = (rB == rC); (as 32-bit floats) */
  CompareLteF64,          /* rA = (rB == rC); (as 64-bit floats) */
  Select,                 /* rA = rB ? rC : rD */
  Exit,                   /* Unconditional exit. */
  ExitIf,                 /* Conditional exit if rA is true. */
  HostVoidCall0,          /* Execute host method with 0 user arguments and no result. */
  HostCall0,              /* Execute host method with 0 user arguments. */
  HostCall1,              /* Execute host method with 1 user argument. */
  HostCall2,              /* Execute host method with 2 user arguments. */
  LoadSpill,              /* Load a spill register into an normal register. */
  StoreSpill,             /* Store a normal register into a spill register. */
};

struct Instruction8R0C0 {
  u8 opcode;
};

struct Instruction16R1C0 {
  u8 opcode;
  u8 rA : 4;
  u8 _unused : 4;
};

struct Instruction16R2C0 {
  u8 opcode;
  u8 rA : 4;
  u8 rB : 4;
};

struct Instruction32R3C0 {
  u8 opcode;
  u8 rA : 4;
  u8 rB : 4;
  u8 rC : 4;
  u8 _unused0 : 4;
  u8 _unused1;
};

struct Instruction32R3C1 {
  u8 opcode;
  u8 rA : 4;
  u8 rB : 4;
  u8 rC : 4;
  u8 _unused0 : 4;
  u8 constant;
};

struct Instruction32R4C0 {
  u8 opcode;
  u8 rA : 4;
  u8 rB : 4;
  u8 rC : 4;
  u8 rD : 4;
  u8 _unused;
};

struct Instruction32R4C1 {
  u8 opcode;
  u8 rA : 4;
  u8 rB : 4;
  u8 rC : 4;
  u8 rD : 4;
  u8 constant;
};

struct Instruction32R0C3 {
  u32 opcode : 8;
  u32 constant : 24;
};

struct Instruction32R1C2 {
  u8 opcode;
  u8 rA : 4;
  u8 _unused : 4;
  u16 constant;
};

}
}
