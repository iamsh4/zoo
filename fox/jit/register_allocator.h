// vim: expandtab:ts=2:sw=2

#pragma once

#include "fox/fox_types.h"
#include "fox/jit/types.h"
#include "fox/jit/rtl.h"

namespace fox {
namespace jit {

/*!
 * @class jit::RegisterAllocator
 * @brief Generic interface used by register allocation backends.
 */
class RegisterAllocator {
public:
  RegisterAllocator();
  virtual ~RegisterAllocator();

  /*!
   * @brief Define a set of registers that can be used to fulfill allocations
   *        for a specific register type.
   */
  virtual void define_register_type(RegisterSet available) = 0;

  /*!
   * @brief Perform register allocation for the provided instruction sequence.
   *        Returns a new instruction sequence, possibly with moves / spills
   *        added, that has all hardware register assignments filled in.
   *
   * The input instruction sequence is consumed.
   */
  virtual RtlProgram execute(RtlProgram &&input) = 0;
};

}
}
