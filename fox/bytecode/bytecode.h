// vim: expandtab:ts=2:sw=2

#pragma once

#include <map>
#include <cassert>
#include <memory>

#include "fox/jit/routine.h"
#include "fox/ir/execution_unit.h"

namespace fox {
namespace bytecode {

/*!
 * @class fox::bytecode::Routine
 * @brief Implementation of a JIT routine that uses a bytecode virtual machine
 *        for execution.
 */
class Routine : public ::fox::jit::Routine {
public:
  Routine()
  {
    return;
  }

  Routine(const u8 *const data, const size_t data_size)
    : m_storage(data),
      m_bytes(data_size)
  {
    return;
  }

  uint64_t execute(Guest *guest,
                   void *memory_base = nullptr,
                   void *opaque = nullptr) override;
  std::string disassemble() const override;
  void debug_print() const;

private:
  std::unique_ptr<const u8[]> m_storage;
  const size_t m_bytes = 0lu;
};

}
}
