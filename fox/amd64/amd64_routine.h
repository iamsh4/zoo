// vim: expandtab:ts=2:sw=2

#pragma once

#include "fox/codegen/routine.h"

namespace fox {
namespace codegen {
namespace amd64 {

/*!
 * @class fox::codegen::amd64::Routine
 * @brief Specialization of Routine that adds a disassembly method.
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
