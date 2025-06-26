// vim: expandtab:ts=2:sw=2

#pragma once

#include <string>

#include "shared/types.h"
#include "sh4_jit.h"

namespace cpu {

class Debugger {
public:
  static std::string disassemble(u16 fetch, u32 PC);
  static std::string disassemble(const BasicBlockOpcodes &ebb);
  static void disassemble(const BasicBlockOpcodes &ebb,
                          std::vector<std::string> &results);

public: /* XXX */
  template<typename T>
  static std::string raw_disassemble(u16 fetch, u32 PC);
};

}
