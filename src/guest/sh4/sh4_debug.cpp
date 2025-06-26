// vim: expandtab:ts=2:sw=2

#include "sh4.h"
#include "sh4_debug.h"

namespace cpu {

std::string
Debugger::disassemble(const u16 fetch, const u32 PC)
{
  const u16 opcode_id = SH4::decode_table[fetch];
  if (opcode_id == 0xFFFFu) {
    return "????????";
  }

  const Opcode &opcode = SH4::opcode_table[opcode_id];
  std::string disassembly = opcode.disassemble(fetch, PC);

  // Show which instructions are impossible to JIT right now.
  if (opcode.flags & OpcodeFlags::DISABLE_JIT) {
    disassembly = disassembly + " [DISABLE_JIT]";
  }

  return disassembly;
}

std::string
Debugger::disassemble(const BasicBlockOpcodes &ebb)
{
  std::string result;
  result.reserve(ebb.size() * 20 /* Rough estimate */);
  for (const auto &entry : ebb) {
    char buffer[256];
    snprintf(buffer,
             sizeof(buffer),
             "[%08x] %s\n",
             entry.address,
             disassemble(entry.raw, entry.address).c_str());
    result += buffer;
  }
  return result;
}

void
Debugger::disassemble(const BasicBlockOpcodes &ebb, std::vector<std::string> &results)
{
  for (const auto &entry : ebb) {
    char buffer[256];
    snprintf(buffer,
             sizeof(buffer),
             "[%08x] %s\n",
             entry.address,
             disassemble(entry.raw, entry.address).c_str());
    results.push_back(buffer);
  }
}

}
