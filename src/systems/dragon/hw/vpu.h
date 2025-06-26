#pragma once

#include <array>
#include <mutex>
#include <queue>

#include "shared/types.h"
#include "vpu_types.h"

namespace zoo::dragon {

using TileBuffer = std::array<vpu::Vector, 32 * 16>;

class VPU {
public:
  // 4 tile buffers
  static const u32 TILE_BUFFER_COUNT = 4;

  VPU() {}
  ~VPU() {}

  TileBuffer *tile_buffer(unsigned i)
  {
    assert(i < TILE_BUFFER_COUNT);
    return &m_tile_buffers[i];
  }

  u64* program_memory()
  {
    return m_program_memory.data();
  }

  void set_global(unsigned i, vpu::Vector value)
  {
    assert(i < m_reg_global.size());
    m_reg_global[i] = value;
  }

  void enqueue(const vpu::AttributeEntry &entry);

  void step_cycles(u64 cycles);

  bool busy() const;

private:
  // Run task completely, return number of instructions executed
  u64 run_task(u32 pc_offset, u32 position);

  vpu::Vector read_register(u32 index);

  void run_instruction(vpu::Encoding encoding, u32 position);

  TileBuffer m_tile_buffers[TILE_BUFFER_COUNT];
  std::array<u64, 512> m_program_memory;

  mutable std::mutex m_attribute_queue_mutex;
  std::queue<vpu::AttributeEntry> m_attribute_queue;
  
  std::array<vpu::Vector, 16> m_reg_local;
  std::array<vpu::Vector, 16> m_reg_shared;
  std::array<vpu::Vector, 32> m_reg_global;

  i64 m_cycle_budget = 0;
};

}
