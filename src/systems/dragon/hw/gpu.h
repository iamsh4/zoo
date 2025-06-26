#pragma once

#include <thread>
#include <variant>

#include "fox/mmio_device.h"
#include "shared/types.h"
#include "shared/scheduler.h"
#include "dragon_sdk/floating.h"

#include "vpu.h"

namespace zoo::dragon {

namespace gui {
class GPU;
}

class Console;

class GPU : public fox::MMIODevice {
public:
  struct Command {
    union {
      struct {
        u32 value;
        u32 command;
      };
      u64 raw;
    };
  };

  struct CommandList {
    u32 base_address;
    u32 end_address;
    u32 id;
    std::vector<Command> commands;
  };

private:
  static const u32 REGISTER_COUNT = 256;
  static const u32 PERF_REGISTER_COUNT = 256;
  static const u32 GPU_TOTAL_REGISTERS = REGISTER_COUNT + PERF_REGISTER_COUNT;

  u32 m_base_address = 0;
  std::array<u32, GPU_TOTAL_REGISTERS> m_registers;
  u32 m_cycle_count = 0;
  Console *m_console;

  EventScheduler::Event m_ee_fifo_callback;

  enum class EESMState
  {
    Idle,
    Running,
    Waiting,
  };

  struct EEState {
    u32 fifo_address_current;
    u32 fifo_address_end;
    EESMState state;
    u32 wait_bits;
  };
  EEState m_ee;

  struct VPUDMAConfig {
    union {
      struct {
        u32 dma_direction : 1;  /**< 0: Bus->VPU, 1: VPU->Bus */
        u32 dma_convert : 1;    /**< Enable write conversion to RGB555 */
        u32 dma_step_size : 5;  /**< In units of 8 bytes, minus 1 */
        u32 dma_step_count : 4; /**< Number of steps, minus 1 */
        u32 dma_bus_stride : 8; /**< In units of 8 bytes */
        u32 _unused : (32 - 1 - 1 - 5 - 4 - 8);
      };
      u32 raw;
    };
  };

  struct VPUDMAState {
    VPUDMAConfig dma_config;
    u32 dma_buffer_addr;
    u32 dma_external_addr;
  };

  static const u32 num_vpus = 2;

  struct State {
    u32 bin_x;
    u32 bin_y;
    VPU vpu[2];
    VPUDMAState vpu_dma_state[num_vpus];

    u32 vpu_reg_xy;
    u32 vpu_reg_zw;
    std::atomic_bool busy_vpu_dma[num_vpus];
  };

  State m_state;

  void ee_tick_wait();
  void ee_tick_running();
  void ee_tick_commands();
  void ee_fifo_callback();

  u32 calculate_busy_bits() const;

  struct WorkItem_VPUDMA {
    u32 vpu_index;
    VPUDMAConfig dma_config;
    u32 dma_buffer_addr;
    u32 dma_external_addr;
  };
  struct WorkItem_SetVPUGlobal {
    u32 register_index;
    u32 xy;
    u32 zw;
  };
  struct WorkItem_SetVPUShared {
    u32 register_index;
    u32 xy;
    u32 zw;
  };
  struct WorkItem_VPULaunchArray {
    u32 pc_offset;
    u32 count;
  };

  using WorkQueueItem =
    std::variant<WorkItem_VPUDMA, WorkItem_SetVPUGlobal, WorkItem_SetVPUShared, WorkItem_VPULaunchArray>;

  std::queue<WorkQueueItem> m_work_queue;
  mutable std::mutex m_work_queue_mutex;
  void work_enqueue(WorkQueueItem);

  void func_vpu_dma(const WorkItem_VPUDMA &);
  void func_vpu_set_global(const WorkItem_SetVPUGlobal&);
  void func_vpu_set_shared(const WorkItem_SetVPUShared&);
  void func_vpu_launch_array(const WorkItem_VPULaunchArray&);

  std::thread m_worker_thread;
  bool m_worker_shutdown = false;
  void worker_thread_body();
  bool worker_peek(WorkQueueItem *);
  void worker_pop();

  u32 m_command_list_counter = 0;
  std::mutex m_command_list_mutex;
  CommandList m_command_list;

  friend class ::zoo::dragon::gui::GPU;

public:
  GPU(u32 base_address, Console *console);
  ~GPU();
  void reset();

  u8 read_u8(u32 addr) override;
  u16 read_u16(u32 addr) override;
  u32 read_u32(u32 addr) override;

  void write_u8(u32 addr, u8 value) override;
  void write_u16(u32 addr, u16 value) override;
  void write_u32(u32 addr, u32 value) override;

  void handle_command(u32 value);

  void register_regions(fox::MemoryTable *memory) override;

  u32 base_address() const
  {
    return m_base_address;
  }

  bool get_command_list_if_different(CommandList *list)
  {
    std::lock_guard lock { m_command_list_mutex };
    if (list && list->id != m_command_list.id) {
      *list = m_command_list;
      return true;
    }
    return false;
  }

  static const u32 MMIO_TOTAL_BYTES = GPU_TOTAL_REGISTERS;

  struct Vec4 {
    Float16 x;
    Float16 y;
    Float16 z;
    Float16 w;
  };
};

} // namespace zoo::ps1
