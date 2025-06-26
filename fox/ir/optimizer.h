// vim: expandtab:ts=2:sw=2

#pragma once

#include "fox/ir/execution_unit.h"

namespace fox {
namespace ir {
namespace optimize {

/*!
 * @class fox::ir::optimize::Pass
 * @brief Common base class for all optimization passes.
 */
class Pass {
public:
  Pass();
  virtual ~Pass();

  /*!
   * @brief Apply the optimization pass. Returns a new ExecutionUnit instance
   *        with optimizations applied.
   */
  virtual ExecutionUnit execute(const ExecutionUnit &source) = 0;
};

/*!
 * @class fox::ir::ConstantPropagation
 * @brief Simplifies operations on constants or that always produce a constant
 *        into a simple constant.
 */
class ConstantPropagation final : public Pass {
public:
  ConstantPropagation();
  ~ConstantPropagation();

  /*!
   * @brief Apply the optimization pass. Destroys the incoming execution unit
   *        and returns a new unit with optimizations applied.
   */
  ExecutionUnit execute(const ExecutionUnit &source) override;
};

/*!
 * @class fox::ir::DeadCodeElimination
 * @brief Removes IR instructions which have no visible side effects. For
 *        example, the result of an addition operation which is never
 *        stored / written / etc.
 */
class DeadCodeElimination final : public Pass {
  bool instruction_has_side_effects(Opcode) const;

public:
  DeadCodeElimination();
  ~DeadCodeElimination();

  /*!
   * @brief Apply the optimization pass. Destroys the incoming execution unit
   *        and returns a new unit with optimizations applied.
   */
  ExecutionUnit execute(const ExecutionUnit &source) override;
};

}
}
}
