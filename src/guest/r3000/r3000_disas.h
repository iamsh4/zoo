#pragma once

#include <string>

#include "guest/r3000/r3000.h"

namespace guest::r3000 {

struct Disassembler {
  std::pair<std::string, std::string> disassemble(u32 pc, Instruction instruction);
  static const char *r(unsigned i);
};

}
