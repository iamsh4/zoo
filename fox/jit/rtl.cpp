#include <fmt/printf.h>

#include "fox/jit/rtl.h"

namespace fox {
namespace jit {

/*****************************************************************************
 * RtlInstructions                                                           *
 *****************************************************************************/

RtlInstructions::RtlInstructions(const std::string &label)
  : m_label(label)
{
  return;
}

void
RtlInstructions::debug_print(std::function<const char *(u16)> opcode_name)
{
  size_t i = 0lu;
  for (auto it = begin(); it != end(); ++it, ++i) {
    const RtlInstruction &entry = *it;
    printf("\t[%04lu]", i);
    for (unsigned j = 0; j < entry.result_count; ++j) {
      if (j > 0) {
        printf(",");
      }

      if (!entry.result(j).rtl.valid()) {
        printf(" $NONE");
      } else {
        printf(" $%u", entry.result(j).rtl.index());
      }

      if (!entry.result(j).hw.assigned()) {
        printf("(?)");
      } else if (entry.result(j).hw.is_spill()) {
        printf("(SPILL:%u)", entry.result(j).hw.spill_index());
      } else {
        printf("(HW:%u)", entry.result(j).hw.index());
      }
    }

    if (entry.result_count > 0) {
      printf(" :=");
    }

    switch (entry.op) {
      case u16(RtlOpcode::Move):
        printf(" {MOVE}");
        break;

      case u16(RtlOpcode::None):
        printf(" {NOP}");
        break;

      default:
        assert((entry.op & 0x8000u) == 0);
        fmt::print(" {}:{:x}", opcode_name(entry.op), entry.data);
    }

    for (unsigned j = 0; j < entry.source_count; ++j) {
      if (j > 0) {
        printf(",");
      }

      if (!entry.source(j).rtl.valid()) {
        printf(" $NONE");
      } else {
        printf(" $%u", entry.source(j).rtl.index());
      }

      if (!entry.source(j).hw.assigned()) {
        printf("(?)");
      } else if (entry.source(j).hw.is_spill()) {
        printf("(SPILL:%u)", entry.source(j).hw.spill_index());
      } else {
        printf("(HW:%u)", entry.source(j).hw.index());
      }
    }
    printf("\n");
  }

  return;
}

/*****************************************************************************
 * RtlProgram                                                                *
 *****************************************************************************/

RtlProgram::RtlProgram()
{
  for (unsigned i = 0; i < HwRegister::MaxTypes; ++i) {
    m_register_usage[i] = RegisterSet(HwRegister::Type(i));
  }
}

RtlProgram::RtlProgram(RtlProgram &&from)
  : RtlProgram()
{
  *this = std::move(from);
}

RtlProgram &
RtlProgram::operator=(RtlProgram &&from)
{
  if (this == &from) {
    return *this;
  }

  m_blocks = std::move(from.m_blocks);
  m_next_ssa = from.m_next_ssa;
  from.m_next_ssa = 0;

  for (unsigned i = 0; i < HwRegister::MaxTypes; ++i) {
    m_register_usage[i] = from.m_register_usage[i];
    from.m_register_usage[i] = RegisterSet(HwRegister::Type(i));
  }

  return *this;
}

void
RtlProgram::debug_print(std::function<const char *(u16)> opcode_name)
{
  for (size_t i = 0lu; i < m_blocks.size(); ++i) {
    fmt::print("{}: (block {})\n", m_blocks[i]->label().c_str(), i);
    m_blocks[i]->debug_print(opcode_name);
  }
}

}
}
