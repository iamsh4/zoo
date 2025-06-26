#include "vpu.h"

namespace zoo::dragon {

void
VPU::enqueue(const vpu::AttributeEntry &entry)
{
  std::lock_guard<std::mutex> lock(m_attribute_queue_mutex);
  m_attribute_queue.push(entry);
}

bool
VPU::busy() const
{
  std::lock_guard<std::mutex> lock(m_attribute_queue_mutex);
  return !m_attribute_queue.empty();
}

vpu::Vector
VPU::read_register(u32 index)
{
  if (index > 32)
    return m_reg_global[index - 32];
  else if (index > 16)
    return m_reg_shared[index - 16];
  else
    return m_reg_local[index];
}

u64
VPU::run_task(u32 pc_offset, u32 position)
{
  // There are 4 bits of PC offset, so multiply by 512/16 to get the actual offset.
  pc_offset = pc_offset * 512 / 16;

  u64 instructions = 0;
  for (; pc_offset < m_program_memory.size(); pc_offset++) {
    vpu::Encoding encoding;
    memcpy(&encoding, &m_program_memory[pc_offset], sizeof(encoding));

    run_instruction(encoding, position);
    instructions++;

    // Is exit?
    if (encoding.subunit == u32(vpu::SubUnit::PRG) && encoding.opcode == 0) {
      // TODO : check condition flag on x component
      break;
    }
  }

  return instructions;
}

void
VPU::run_instruction(vpu::Encoding encoding, u32 position)
{
  // printf("running instruction word 0x%016x position %u\n", encoding.raw, position);

  if (encoding.subunit == u32(vpu::SubUnit::MEM) && encoding.opcode == 2) {
    // STORE
    const u32 store_source = encoding.input_b;
    const u32 store_buffer = encoding.immediate;
    vpu::Vector value = read_register(store_source);
    m_tile_buffers[store_buffer][position] = value;
  }

  else if (encoding.subunit == u32(vpu::SubUnit::PRG) && encoding.opcode == 0) {
    // Do nothing. The run_task will perform the exit
  }

  else {
    printf("unhandled vpu instruction subunit %u opcode %u\n",
           encoding.subunit,
           encoding.opcode);
  }
}

void
VPU::step_cycles(u64 cycles)
{
  m_cycle_budget += cycles;

  while (m_cycle_budget > 0) {

    vpu::AttributeEntry entry;

    {
      std::lock_guard<std::mutex> lock(m_attribute_queue_mutex);
      if (m_attribute_queue.empty()) {
        break;
      }
      entry = m_attribute_queue.front();
    }

    // Do something with the entry.
    if (std::holds_alternative<vpu::AttributeGlobal>(entry)) {
      const auto &global = std::get<vpu::AttributeGlobal>(entry);
      m_reg_global[global.index] = global.value;

    } else if (std::holds_alternative<vpu::AttributeShared>(entry)) {
      const auto &shared = std::get<vpu::AttributeShared>(entry);
      m_reg_shared[shared.index] = shared.value;

    } else if (std::holds_alternative<vpu::AttributeLocal>(entry)) {
      const auto &local = std::get<vpu::AttributeLocal>(entry);
      m_reg_local[local.index] = local.value;

    } else if (std::holds_alternative<vpu::AttributeLaunch>(entry)) {
      const auto &launch = std::get<vpu::AttributeLaunch>(entry);
      const u64 task_instructions = run_task(launch.pc_offset, launch.position);
      m_cycle_budget -= task_instructions; // effectively 1 cycle per instruction

    } else {
      // Unknown entry type.
      assert(false);
    }

    {
      std::lock_guard<std::mutex> lock(m_attribute_queue_mutex);
      m_attribute_queue.pop();
    }
  }

  // You can't "bank" time for the next run if nothing was running this time slice.
  if (m_cycle_budget > 0)
    m_cycle_budget = 0;
}
}
