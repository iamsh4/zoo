#pragma once

#include "fox/codegen/routine.h"

namespace fox {
namespace codegen {
namespace arm64 {

/*!
 * @brief Specialization of the codegen::Routine that adds a disassembly method
 *        for generated arm64 instructions.
 */
class Routine : public ::fox::codegen::Routine {
public:
  Routine()
  {
    return;
  }

  Routine(const u8 *const data, const size_t data_size)
    : ::fox::codegen::Routine(data, data_size)
  {
    return;
  }

  std::string disassemble() const override;
  void debug_print() override;
};

}
}
}
