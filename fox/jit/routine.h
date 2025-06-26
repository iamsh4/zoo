// vim: expandtab:ts=2:sw=2

#pragma once

#include <string>

#include "fox/guest.h"

namespace fox {
namespace jit {

/*!
 * @class fox::jit::Routine
 * @brief Base class for all JIT-compiled routines. This should be specialized
 *        by each JIT backend - e.g. amd64, bytecode.
 */
class Routine {
public:
  Routine();
  virtual ~Routine();

  /*!
   * @brief Execute the routine against a specific Guest, passing the base
   *        addresses for memory access and register access.
   */
  virtual uint64_t execute(Guest *guest,
                           void *memory_base = nullptr,
                           void *register_base = nullptr) = 0;

  /*!
   * @brief Disassemble the routine into a human-readable format suitable for
   *        debugging.
   */
  virtual std::string disassemble() const = 0;
};

}
}
