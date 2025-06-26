#pragma once

#include "fox/codegen/routine.h"
#include "fox/jit/cache.h"
#include "fox/jit/routine.h"
#include "fox/ir/execution_unit.h"

#include "guest/r3000/r3000.h"

namespace guest::r3000 {

class R3000::BasicBlock final : public fox::jit::CacheEntry {
private:
  fox::ir::ExecutionUnit m_execution_unit;

  /*!
   * @brief Storage for the bytecode compilation of the instruction sequence,
   *        when available.
   */
  std::unique_ptr<fox::jit::Routine> m_bytecode;

  /*!
   * @brief Storage for the host-native compilation of the instruction sequence,
   *        when available.
   */
  std::unique_ptr<fox::codegen::Routine> m_native;

public:
  BasicBlock(const u32 virt_address, const u32 size, fox::ir::ExecutionUnit &&eu)
    : CacheEntry(virt_address, virt_address & PHYSICAL_MASK, size),
      m_execution_unit(std::move(eu))
  {
  }

  /*!
   * @brief Compile the cached block into something suitable for execution on
   *        the current host.
   */
  bool compile() override;

  u64 execute(R3000 *cpu, u64 cycle_limit);
};

}
