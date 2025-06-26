#pragma once

#include "amd64/amd64_assembler.h"
#include "amd64/amd64_opcodes.h"

/*
 * Operand count and types.
 *     M: Register or memory
 *     R: Register only,
 *     X: No operands
 *     _: Not implemented
 */
#define M  1, true,  false
#define R  1, false, false
#define MM 2, true,  true
#define MR 2, true,  false
#define RM 2, false, true
#define RR 2, false, false
#define X  0, false, false
#define _  0, false, false

/*
 * Result formats
 *     INOUT: First operand is both input / output (destructive)
 *     OUT:   First operand is output only (2 operand only)
 *     IN:    First operand is input only (no outputs, e.g. cmp/test)
 *     NONE:  Instruction takes 0 operands
 */
#define INOUT true,  true
#define OUT   false, true
#define IN    true,  false
#define NONE  false, false

namespace fox {
namespace codegen {
namespace amd64 {

struct EmitTableEntry {
  /* The RTL opcode this applies to */
  Opcode opcode;

  /* The first operand is used as an input. */
  bool first_input;

  /* The first operand is used as an output. */
  bool first_output;

  /* The number of operands the instruction accepts. */
  unsigned operands;

  /* The first operand can be either a memory location or a register. */
  bool first_memory;

  /* The second operand can be either a memory location or a register. */
  bool second_memory;

  /* The bit size of the operation. Corresponds to an entry in the emit method
   * union. */
  RegisterSize size;

  /* The assembler's instruction emitting method. */
  union {
    void (Assembler::*none)();
    void (Assembler::*byte1)(RegMem<BYTE>);
    void (Assembler::*word1)(RegMem<WORD>);
    void (Assembler::*dword1)(RegMem<DWORD>);
    void (Assembler::*qword1)(RegMem<QWORD>);
    void (Assembler::*byte)(RegMem<BYTE>, RegMem<BYTE>);
    void (Assembler::*word)(RegMem<WORD>, RegMem<WORD>);
    void (Assembler::*dword)(RegMem<DWORD>, RegMem<DWORD>);
    void (Assembler::*qword)(RegMem<QWORD>, RegMem<QWORD>);
    void (Assembler::*vecps)(Vector<XMM>, RegMemVector<XMM>); /* XXX */
    void (Assembler::*vecpd)(Vector<XMM>, RegMemVector<XMM>); /* XXX */
    void (Assembler::*vecss)(Vector<DWORD>, RegMemVector<DWORD>); /* XXX */
    void (Assembler::*vecsd)(Vector<QWORD>, RegMemVector<QWORD>); /* XXX */
    void *method = nullptr;
  };
};

/* clang-format off */
static const EmitTableEntry emit_table[] = {
  /* High level instructions */
  /* OPCODE                         FIRST,  MODE  SIZE   METHOD                       */
  {  Opcode::LABEL,                 NONE,   _,    BYTE,                               },
  {  Opcode::PUSH_REGISTERS,        NONE,   _,    BYTE,                               },
  {  Opcode::POP_REGISTERS,         NONE,   _,    BYTE,                               },
  {  Opcode::ALLOCATE_SPILL,        NONE,   _,    BYTE,                               },
  {  Opcode::FREE_SPILL,            NONE,   _,    BYTE,                               },
  {  Opcode::READ_GUEST_REGISTER32, NONE,   _,    BYTE,                               },
  {  Opcode::READ_GUEST_REGISTER64, NONE,   _,    BYTE,                               },
  {  Opcode::WRITE_GUEST_REGISTER32,NONE,   _,    BYTE,                               },
  {  Opcode::WRITE_GUEST_REGISTER64,NONE,   _,    BYTE,                               },
  {  Opcode::LOAD_GUEST_MEMORY,     NONE,   _,    BYTE,                               },
  {  Opcode::LOAD_GUEST_MEMORY_SSE, NONE,   _,    BYTE,                               },
  {  Opcode::STORE_GUEST_MEMORY,    NONE,   _,    BYTE,                               },
  {  Opcode::STORE_GUEST_MEMORY_SSE,NONE,   _,    BYTE,                               },
  {  Opcode::CALL_FRAMED,           NONE,   _,    BYTE,                               },
  {  Opcode::RET,                   INOUT,  X,    BYTE,  .none = &Assembler::ret      },

  /* General purpose instructions */
  /* OPCODE                         FIRST,  MODE  SIZE   METHOD                       */
  {  Opcode::LOAD_BYTE_IMM8,        NONE,   _,    BYTE,                               },
  {  Opcode::LOAD_QWORD_IMM32,      NONE,   _,    BYTE,                               },
  {  Opcode::LOAD_QWORD_IMM64,      NONE,   _,    BYTE,                               },
  {  Opcode::SHIFTR_BYTE,           NONE,   _,    BYTE,                               },
  {  Opcode::SHIFTR_WORD,           NONE,   _,    BYTE,                               },
  {  Opcode::SHIFTR_DWORD,          NONE,   _,    BYTE,                               },
  {  Opcode::SHIFTR_QWORD,          NONE,   _,    BYTE,                               },
  {  Opcode::SHIFTL_BYTE,           NONE,   _,    BYTE,                               },
  {  Opcode::SHIFTL_WORD,           NONE,   _,    BYTE,                               },
  {  Opcode::SHIFTL_DWORD,          NONE,   _,    BYTE,                               },
  {  Opcode::SHIFTL_QWORD,          NONE,   _,    BYTE,                               },
  {  Opcode::ASHIFTR_BYTE,          NONE,   _,    BYTE,                               },
  {  Opcode::ASHIFTR_WORD,          NONE,   _,    BYTE,                               },
  {  Opcode::ASHIFTR_DWORD,         NONE,   _,    BYTE,                               },
  {  Opcode::ASHIFTR_QWORD,         NONE,   _,    BYTE,                               },
  {  Opcode::ROL1_BYTE,             INOUT,  M,    BYTE,  .byte1  = &Assembler::rol1   },
  {  Opcode::ROL1_WORD,             INOUT,  M,    WORD,  .word1  = &Assembler::rol1   },
  {  Opcode::ROL1_DWORD,            INOUT,  M,    DWORD, .dword1 = &Assembler::rol1   },
  {  Opcode::ROL1_QWORD,            INOUT,  M,    QWORD, .qword1 = &Assembler::rol1   },
  {  Opcode::ROL_BYTE,              NONE,   _,    BYTE,                               },
  {  Opcode::ROL_WORD,              NONE,   _,    BYTE,                               },
  {  Opcode::ROL_DWORD,             NONE,   _,    BYTE,                               },
  {  Opcode::ROL_QWORD,             NONE,   _,    BYTE,                               },
  {  Opcode::ROR1_BYTE,             INOUT,  M,    BYTE,  .byte1  = &Assembler::ror1   },
  {  Opcode::ROR1_WORD,             INOUT,  M,    WORD,  .word1  = &Assembler::ror1   },
  {  Opcode::ROR1_DWORD,            INOUT,  M,    DWORD, .dword1 = &Assembler::ror1   },
  {  Opcode::ROR1_QWORD,            INOUT,  M,    QWORD, .qword1 = &Assembler::ror1   },
  {  Opcode::ROR_BYTE,              NONE,   _,    BYTE,                               },
  {  Opcode::ROR_WORD,              NONE,   _,    BYTE,                               },
  {  Opcode::ROR_DWORD,             NONE,   _,    BYTE,                               },
  {  Opcode::ROR_QWORD,             NONE,   _,    BYTE,                               },
  {  Opcode::SHIFTR_DWORD_IMM8,     NONE,   _,    BYTE,                               },
  {  Opcode::SHIFTL_DWORD_IMM8,     NONE,   _,    BYTE,                               },
  {  Opcode::ASHIFTR_DWORD_IMM8,    NONE,   _,    BYTE,                               },
  {  Opcode::AND_BYTE,              INOUT,  MM,   BYTE,  .byte   = &Assembler::_and   },
  {  Opcode::AND_WORD,              INOUT,  MM,   WORD,  .word   = &Assembler::_and   },
  {  Opcode::AND_DWORD,             INOUT,  MM,   DWORD, .dword  = &Assembler::_and   },
  {  Opcode::AND_QWORD,             INOUT,  MM,   QWORD, .qword  = &Assembler::_and   },
  {  Opcode::OR_BYTE,               INOUT,  MM,   BYTE,  .byte   = &Assembler::_or    },
  {  Opcode::OR_WORD,               INOUT,  MM,   WORD,  .word   = &Assembler::_or    },
  {  Opcode::OR_DWORD,              INOUT,  MM,   DWORD, .dword  = &Assembler::_or    },
  {  Opcode::OR_QWORD,              INOUT,  MM,   QWORD, .qword  = &Assembler::_or    },
  {  Opcode::XOR_BYTE,              INOUT,  MM,   BYTE,  .byte   = &Assembler::_xor   },
  {  Opcode::XOR_WORD,              INOUT,  MM,   WORD,  .word   = &Assembler::_xor   },
  {  Opcode::XOR_DWORD,             INOUT,  MM,   DWORD, .dword  = &Assembler::_xor   },
  {  Opcode::XOR_QWORD,             INOUT,  MM,   QWORD, .qword  = &Assembler::_xor   },
  {  Opcode::NOT_BYTE,              INOUT,  M,    BYTE,  .byte1  = &Assembler::_not   },
  {  Opcode::NOT_WORD,              INOUT,  M,    WORD,  .word1  = &Assembler::_not   },
  {  Opcode::NOT_DWORD,             INOUT,  M,    DWORD, .dword1 = &Assembler::_not   },
  {  Opcode::NOT_QWORD,             INOUT,  M,    QWORD, .qword1 = &Assembler::_not   },
  {  Opcode::AND_DWORD_IMM32,       NONE,   _,    BYTE,                               },
  {  Opcode::OR_DWORD_IMM32,        NONE,   _,    BYTE,                               },
  {  Opcode::XOR_BYTE_IMM8,         NONE,   _,    BYTE,                               },
  {  Opcode::ADD_BYTE,              INOUT,  MM,   BYTE,  .byte   = &Assembler::add    },
  {  Opcode::ADD_WORD,              INOUT,  MM,   WORD,  .word   = &Assembler::add    },
  {  Opcode::ADD_DWORD,             INOUT,  MM,   DWORD, .dword  = &Assembler::add    },
  {  Opcode::ADD_QWORD,             INOUT,  MM,   QWORD, .qword  = &Assembler::add    },
  {  Opcode::SUB_BYTE,              INOUT,  MM,   BYTE,  .byte   = &Assembler::sub    },
  {  Opcode::SUB_WORD,              INOUT,  MM,   WORD,  .word   = &Assembler::sub    },
  {  Opcode::SUB_DWORD,             INOUT,  MM,   DWORD, .dword  = &Assembler::sub    },
  {  Opcode::SUB_QWORD,             INOUT,  MM,   QWORD, .qword  = &Assembler::sub    },
  {  Opcode::MUL_BYTE,              NONE,   _,    BYTE,                               },
  {  Opcode::MUL_WORD,              NONE,   _,    BYTE,                               },
  {  Opcode::MUL_DWORD,             NONE,   _,    BYTE,                               },
  {  Opcode::MUL_QWORD,             NONE,   _,    BYTE,                               },
  {  Opcode::IMUL_BYTE,             NONE,   _,    BYTE,                               },
  {  Opcode::IMUL_WORD,             INOUT,  RM,   WORD,  .word   = &Assembler::imul   },
  {  Opcode::IMUL_DWORD,            INOUT,  RM,   DWORD, .dword  = &Assembler::imul   },
  {  Opcode::IMUL_QWORD,            INOUT,  RM,   QWORD, .qword  = &Assembler::imul   },
  {  Opcode::ADD_DWORD_IMM32,       NONE,   _,    BYTE,                               },
  {  Opcode::SUB_DWORD_IMM32,       NONE,   _,    BYTE,                               },
  {  Opcode::EXTEND32_BYTE,         NONE,   _,    BYTE,                               },
  {  Opcode::EXTEND32_WORD,         NONE,   _,    BYTE,                               },
  {  Opcode::ZEXTEND32_BYTE,        NONE,   _,    BYTE,                               },
  {  Opcode::ZEXTEND32_WORD,        NONE,   _,    BYTE,                               },
  {  Opcode::EXTEND64_BYTE,         NONE,   _,    BYTE,                               },
  {  Opcode::EXTEND64_WORD,         NONE,   _,    BYTE,                               },
  {  Opcode::EXTEND64_DWORD,        NONE,   _,    BYTE,                               },
  {  Opcode::ZEXTEND64_BYTE,        NONE,   _,    BYTE,                               },
  {  Opcode::ZEXTEND64_WORD,        NONE,   _,    BYTE,                               },
  {  Opcode::ZEXTEND64_DWORD,       NONE,   _,    BYTE,                               },
  {  Opcode::CMOVNZ_WORD,           INOUT,  RM,   WORD,  .word   = &Assembler::cmovnz },
  {  Opcode::CMOVNZ_DWORD,          INOUT,  RM,   DWORD, .dword  = &Assembler::cmovnz },
  {  Opcode::CMOVNZ_QWORD,          INOUT,  RM,   QWORD, .qword  = &Assembler::cmovnz },
  {  Opcode::CMOVZ_WORD,            INOUT,  RM,   WORD,  .word   = &Assembler::cmovz  },
  {  Opcode::CMOVZ_DWORD,           INOUT,  RM,   DWORD, .dword  = &Assembler::cmovz  },
  {  Opcode::CMOVZ_QWORD,           INOUT,  RM,   QWORD, .qword  = &Assembler::cmovz  },
  {  Opcode::CMOVL_WORD,            INOUT,  RM,   WORD,  .word   = &Assembler::cmovl  },
  {  Opcode::CMOVL_DWORD,           INOUT,  RM,   DWORD, .dword  = &Assembler::cmovl  },
  {  Opcode::CMOVL_QWORD,           INOUT,  RM,   QWORD, .qword  = &Assembler::cmovl  },
  {  Opcode::CMOVLE_WORD,           INOUT,  RM,   WORD,  .word   = &Assembler::cmovle },
  {  Opcode::CMOVLE_DWORD,          INOUT,  RM,   DWORD, .dword  = &Assembler::cmovle },
  {  Opcode::CMOVLE_QWORD,          INOUT,  RM,   QWORD, .qword  = &Assembler::cmovle },
  {  Opcode::CMOVB_WORD,            INOUT,  RM,   WORD,  .word   = &Assembler::cmovb  },
  {  Opcode::CMOVB_DWORD,           INOUT,  RM,   DWORD, .dword  = &Assembler::cmovb  },
  {  Opcode::CMOVB_QWORD,           INOUT,  RM,   QWORD, .qword  = &Assembler::cmovb  },
  {  Opcode::CMOVBE_WORD,           INOUT,  RM,   WORD,  .word   = &Assembler::cmovbe },
  {  Opcode::CMOVBE_DWORD,          INOUT,  RM,   DWORD, .dword  = &Assembler::cmovbe },
  {  Opcode::CMOVBE_QWORD,          INOUT,  RM,   QWORD, .qword  = &Assembler::cmovbe },
  {  Opcode::SETNZ,                 OUT,    M,    BYTE,  .byte1  = &Assembler::setnz  },
  {  Opcode::SETZ,                  OUT,    M,    BYTE,  .byte1  = &Assembler::setz   },
  {  Opcode::SETL,                  OUT,    M,    BYTE,  .byte1  = &Assembler::setl   },
  {  Opcode::SETLE,                 OUT,    M,    BYTE,  .byte1  = &Assembler::setle  },
  {  Opcode::SETB,                  OUT,    M,    BYTE,  .byte1  = &Assembler::setb   },
  {  Opcode::SETBE,                 OUT,    M,    BYTE,  .byte1  = &Assembler::setbe  },
  {  Opcode::TEST_BYTE,             IN,     MM,   BYTE,  .byte   = &Assembler::test   },
  {  Opcode::TEST_WORD,             IN,     MM,   WORD,  .word   = &Assembler::test   },
  {  Opcode::TEST_DWORD,            IN,     MM,   DWORD, .dword  = &Assembler::test   },
  {  Opcode::TEST_QWORD,            IN,     MM,   QWORD, .qword  = &Assembler::test   },
  {  Opcode::CMP_BYTE,              IN,     MM,   BYTE,  .byte   = &Assembler::cmp    },
  {  Opcode::CMP_WORD,              IN,     MM,   WORD,  .word   = &Assembler::cmp    },
  {  Opcode::CMP_DWORD,             IN,     MM,   DWORD, .dword  = &Assembler::cmp    },
  {  Opcode::CMP_QWORD,             IN,     MM,   QWORD, .qword  = &Assembler::cmp    },
  {  Opcode::TEST_DWORD_IMM32,      OUT,    MM,   DWORD,                              },
  {  Opcode::CMP_DWORD_IMM32,       OUT,    MM,   DWORD,                              },
  {  Opcode::MOV_BYTE,              OUT,    MM,   BYTE,  .byte   = &Assembler::mov    },
  {  Opcode::MOV_WORD,              OUT,    MM,   WORD,  .word   = &Assembler::mov    },
  {  Opcode::MOV_DWORD,             OUT,    MM,   DWORD, .dword  = &Assembler::mov    },
  {  Opcode::MOV_QWORD,             OUT,    MM,   QWORD, .qword  = &Assembler::mov    },
  {  Opcode::MOVD_DWORD,            OUT,    MM,   DWORD,                              },
  {  Opcode::MOVD_QWORD,            OUT,    MM,   QWORD,                              },
  {  Opcode::JMP,                   NONE,   _,    BYTE,                               },
  {  Opcode::JNZ,                   NONE,   _,    BYTE,                               },

  /* Vector instructions */
  /* OPCODE                         FIRST,  MODE  SIZE   METHOD                       */
  {  Opcode::ADD_VECPS,             INOUT,  RM,   VECPS, .vecps  = &Assembler::addps  },
  {  Opcode::ADD_VECPD,             INOUT,  RM,   VECPD, .vecpd  = &Assembler::addpd  },
  {  Opcode::ADD_VECSS,             INOUT,  RM,   VECSS, .vecss  = &Assembler::addss  },
  {  Opcode::ADD_VECSD,             INOUT,  RM,   VECSD, .vecsd  = &Assembler::addsd  },
  {  Opcode::SUB_VECPS,             INOUT,  RM,   VECPS, .vecps  = &Assembler::subps  },
  {  Opcode::SUB_VECPD,             INOUT,  RM,   VECPD, .vecpd  = &Assembler::subpd  },
  {  Opcode::SUB_VECSS,             INOUT,  RM,   VECSS, .vecss  = &Assembler::subss  },
  {  Opcode::SUB_VECSD,             INOUT,  RM,   VECSD, .vecsd  = &Assembler::subsd  },
  {  Opcode::MUL_VECPS,             INOUT,  RM,   VECPS, .vecps  = &Assembler::mulps  },
  {  Opcode::MUL_VECPD,             INOUT,  RM,   VECPD, .vecpd  = &Assembler::mulpd  },
  {  Opcode::MUL_VECSS,             INOUT,  RM,   VECSS, .vecss  = &Assembler::mulss  },
  {  Opcode::MUL_VECSD,             INOUT,  RM,   VECSD, .vecsd  = &Assembler::mulsd  },
  {  Opcode::DIV_VECPS,             INOUT,  RM,   VECPS, .vecps  = &Assembler::divps  },
  {  Opcode::DIV_VECPD,             INOUT,  RM,   VECPD, .vecpd  = &Assembler::divpd  },
  {  Opcode::DIV_VECSS,             INOUT,  RM,   VECSS, .vecss  = &Assembler::divss  },
  {  Opcode::DIV_VECSD,             INOUT,  RM,   VECSD, .vecsd  = &Assembler::divsd  },
  {  Opcode::SQRT_VECPS,            OUT,    RM,   VECPS, .vecps  = &Assembler::sqrtps },
  {  Opcode::SQRT_VECPD,            OUT,    RM,   VECPD, .vecpd  = &Assembler::sqrtpd },
  {  Opcode::SQRT_VECSS,            OUT,    RM,   VECSS, .vecss  = &Assembler::sqrtss },
  {  Opcode::SQRT_VECSD,            OUT,    RM,   VECSD, .vecsd  = &Assembler::sqrtsd },
};
/* clang-format on */

static constexpr size_t emit_table_size = sizeof(emit_table) / sizeof(EmitTableEntry);

}
}
}

#undef M
#undef R
#undef MM
#undef MR
#undef RM
#undef RR
#undef X
#undef _
#undef INOUT
#undef OUT
#undef IN
#undef NONE
