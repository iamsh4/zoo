#include <stdexcept>
#include <algorithm>

#include "shared/types.h"
#include "systems/dragon/console.h"
#include "systems/dragon/hw/gpu.h"
#include "gpu_regs.h"

namespace zoo::dragon {

using namespace gpu;

GPU::GPU(u32 base_address, Console *console)
  : m_base_address(base_address),
    m_console(console),
    m_ee_fifo_callback("gpu.fifo",
                       std::bind(&GPU::ee_fifo_callback, this),
                       console->scheduler())
{
  m_console->schedule_event(1 * 10 * 1000, &m_ee_fifo_callback);
}

GPU::~GPU()
{
  if (m_worker_thread.joinable()) {
    m_worker_shutdown = true;
    m_worker_thread.join();
  }
}

void
GPU::reset()
{
  m_registers[CMD_BUF_EXEC] = 0;
  m_registers[CMD_FIFO_START] = 0;
  m_registers[CMD_FIFO_CLEAR] = 1;
  m_registers[CMD_FIFO_COUNT] = 0;
  m_ee.state = EESMState::Idle;

  // Reset worker
  if (m_worker_thread.joinable()) {
    m_worker_shutdown = true;
    m_worker_thread.join();
  }
  m_worker_shutdown = false;
  m_worker_thread = std::thread(std::bind(&GPU::worker_thread_body, this));
}

u8
GPU::read_u8(u32 addr)
{
  printf("gpu: unhandled read_u8 0x%08x pc=0x%08x\n", addr, m_console->cpu()->PC());
  return 0;
}

u16
GPU::read_u16(u32 addr)
{
  printf("gpu: unhandled read_u16 0x%08x pc=0x%08x\n", addr, m_console->cpu()->PC());
  return 0;
}

u32
GPU::read_u32(u32 addr)
{
  const u32 reg_index = (addr >> 2) & 0x1ff;
  assert(reg_index < GPU_TOTAL_REGISTERS);

  // printf("gpu: read_u32 0x%08x reg_index=%d pc=0x%08x\n",
  //        addr,
  //        reg_index,
  //        m_console->cpu()->PC());

  if (reg_index == BUSY) {
    return calculate_busy_bits();
  }
  return m_registers[reg_index];
}

void
GPU::write_u8(u32 addr, u8 value)
{
  printf("gpu: unhandled write_u8 0x%08x < 0x%x pc=0x%08x\n",
         addr,
         value,
         m_console->cpu()->PC());
}

void
GPU::write_u16(u32 addr, u16 value)
{
  printf("gpu: unhandled write_u16 0x%08x < 0x%x pc=0x%08x\n",
         addr,
         value,
         m_console->cpu()->PC());
}

// === Concurrent processes ===
// MMIO Read/Write
// GPU EE FIFO
// GPU

u32
GPU::calculate_busy_bits() const
{
  {
    std::lock_guard lock { m_work_queue_mutex };
    if (!m_work_queue.empty()) {
      return 0xffff'ffff;
    }
  }

  u32 busy_bits = 0;
  if (m_state.busy_vpu_dma[0])
    busy_bits |= BUSY_BIT_VPU0_DMA;
  if (m_state.busy_vpu_dma[1])
    busy_bits |= BUSY_BIT_VPU1_DMA;
  if (m_state.vpu[0].busy())
    busy_bits |= BUSY_BIT_VPU0;
  if (m_state.vpu[1].busy())
    busy_bits |= BUSY_BIT_VPU1;
  // XXX : Implement draw busy bit
  return busy_bits;
}

void
GPU::ee_tick_wait()
{
  // printf("GPU waiting...\n");
  const u32 busy_bits = calculate_busy_bits();

  // If selected functional units are no longer busy than continue EE
  if ((busy_bits & m_ee.wait_bits) == 0) {
    m_ee.state = EESMState::Running;
  }
}

u32
float16_color(Float16 x)
{
  // typedef struct packed {
  //     logic       sign;
  //     logic [4:0] exponent;
  //     logic [9:0] mantissa;
  // } VPU_Scalar;
  const u32 raw = x.raw();

  // S0_exponent <= ({ 1'b0, in_float.exponent } - 11) | { in_float.sign, 5'b0 };
  const i32 E = (raw >> 10) & 0b11111;
  const i32 s0e = E - 11;

  if (s0e < 0) {
    return 0;
  }

  // S0_mantissa <= in_float.mantissa[(MANTISSA_BITS - 1):(MANTISSA_BITS - 4)];
  const u32 s0m = (raw & 0x3ff) >> 6;

  switch (s0e) {
      // clang-format off
    case 0:  return 1;
    case 1:  return 0b00010 | ((s0m >> 3) & 0b1);
    case 2:  return 0b00100 | ((s0m >> 2) & 0b11);
    case 3:  return 0b01000 | ((s0m >> 1) & 0b111);
    case 4:  return 0b10000 | ((s0m >> 0) & 0b1111);
    default: return (raw & 0x8000) ? 0b00000 : 0b11111;
      // clang-format on
  }
}

uint16_t
to_argb5551(GPU::Vec4 input)
{
  uint32_t r = float16_color(input.x); // float16_color(input.x);
  uint32_t g = float16_color(input.y); // float16_color(input.y);
  uint32_t b = float16_color(input.z); // float16_color(input.z);

  uint16_t result = 0;
  result |= r << 11;
  result |= g << 6;
  result |= b << 1;
  result |= (input.w.raw() != 0) ? 1 : 0;

  return result;
}

void
GPU::func_vpu_dma(const WorkItem_VPUDMA &work_item)
{
  const u32 vpu_index = work_item.vpu_index;
  const VPUDMAConfig dma_config = work_item.dma_config;

  const u32 step_size = (dma_config.dma_step_size + 1) * 8;
  const u32 step_count = dma_config.dma_step_count + 1;
  const u32 bus_stride = dma_config.dma_bus_stride * 8;
  const u32 tile_buffer_index = (work_item.dma_buffer_addr >> 12) & 0b1111;

  assert(!m_state.vpu[0].busy());
  assert(!m_state.vpu[1].busy());

  // Is this program DMA ?
  if (dma_config.dma_direction == 0 && tile_buffer_index == 8) {
    m_console->memory()->dma_read(
      m_state.vpu[vpu_index].program_memory(), work_item.dma_external_addr, step_size);
    m_state.busy_vpu_dma[vpu_index] = 0;
    return;
  }

  u64 *buffer = nullptr;
  if (tile_buffer_index < 4) {
    buffer = (u64 *)&m_state.vpu[vpu_index].tile_buffer(tile_buffer_index)[0];
  } else {
    throw std::runtime_error("unhandled buffer index in vpu dma");
  }

  // We don't handle the case where you're DMA'ing to/from the buffer at anywhere
  // other than the start (though it probably works with the current logic. just
  // untested.)
  if ((work_item.dma_buffer_addr & 0xfff) != 0) {
    throw std::runtime_error("unhandled vpu dma case: non-zero buffer start offset");
  }

  // Starting buffer offset (8 bytes per element)
  u32 buffer_offset = (work_item.dma_buffer_addr & 0xfff) >> 3;

  if (dma_config.dma_direction == 0) {
    // Bus->VPU

    if (dma_config.dma_convert) {
      throw std::runtime_error("dma color conversion not implemented for bus->vpu");
    }

    u32 bus_addr = work_item.dma_external_addr;
    for (u32 step_i = 0; step_i < step_count; ++step_i) {
      m_console->memory()->dma_read(&buffer[buffer_offset], bus_addr, step_size);
      buffer_offset += step_size / 8;
      bus_addr += bus_stride;
    }

  } else {
    // VPU -> Bus
    u32 bus_addr = work_item.dma_external_addr;

    for (u32 step_i = 0; step_i < step_count; ++step_i) {
      const u32 bus_addr_before_loop = bus_addr;

      if (dma_config.dma_convert) {
        u16 dma_buff[512];

        // Convert a whole buffer worth of color, them dma the buffer to sysmem
        for (u32 i = 0; i < step_size; i += 8) {
          Vec4 vec;
          const u64 bits = buffer[buffer_offset + i / 8];
          vec.x = Float16(Float16::from_bits, (bits >> 48) & 0xffff);
          vec.y = Float16(Float16::from_bits, (bits >> 32) & 0xffff);
          vec.z = Float16(Float16::from_bits, (bits >> 16) & 0xffff);
          vec.w = Float16(Float16::from_bits, (bits >> 0) & 0xffff);
          dma_buff[i / 8] = to_argb5551(vec);
        }

        // 2 bytes written per vpu buffer element
        m_console->memory()->dma_write(bus_addr, dma_buff, step_size * 2 / 8);
        buffer_offset += step_size / 8;
        bus_addr += step_size * 2 / 8;

      } else {
        m_console->memory()->dma_write(bus_addr, &buffer[buffer_offset], step_size);
        buffer_offset += step_size / 8;
      }

      bus_addr = bus_addr_before_loop + bus_stride;
    }
  }

  m_state.busy_vpu_dma[vpu_index] = 0;
}

void
GPU::func_vpu_set_global(const WorkItem_SetVPUGlobal &item)
{
  vpu::Vector global;
  // memcpy(&global.x, &item.xy, sizeof(u32));
  // memcpy(&global.z, &item.zw, sizeof(u32));
  global.x = Float16(Float16::from_bits, item.xy & 0xffff);
  global.y = Float16(Float16::from_bits, item.xy >> 16);
  global.z = Float16(Float16::from_bits, item.zw & 0xffff);
  global.w = Float16(Float16::from_bits, item.zw >> 16);

  for (unsigned vpu_index = 0; vpu_index < num_vpus; ++vpu_index) {
    m_state.vpu[vpu_index].enqueue(
      vpu::AttributeGlobal { .index = item.register_index, .value = global });
  }
}

void
GPU::func_vpu_set_shared(const WorkItem_SetVPUShared &item)
{
  vpu::Vector shared;
  // memcpy(&shared.x, &item.xy, sizeof(u32));
  // memcpy(&shared.z, &item.zw, sizeof(u32));
  shared.x = Float16(Float16::from_bits, item.xy & 0xffff);
  shared.y = Float16(Float16::from_bits, item.xy >> 16);
  shared.z = Float16(Float16::from_bits, item.zw & 0xffff);
  shared.w = Float16(Float16::from_bits, item.zw >> 16);

  for (unsigned vpu_index = 0; vpu_index < num_vpus; ++vpu_index) {
    m_state.vpu[vpu_index].enqueue(
      vpu::AttributeShared { .index = item.register_index, .value = shared });
  }
}

void
GPU::func_vpu_launch_array(const WorkItem_VPULaunchArray &item)
{
  // printf("launch array pc_offset=%u count=%u\n", item.pc_offset, item.count);
  for (unsigned i = 0; i < item.count; ++i) {
    const u32 vpu_index = (i / 32) & 1;
    const u32 position = (i / 64) * 32 + (i % 32);
    printf(
      "launching vpu %u pc_offset=%u position=%u\n", vpu_index, item.pc_offset, position);
    m_state.vpu[vpu_index].enqueue(
      vpu::AttributeLaunch { .pc_offset = item.pc_offset, .position = position });
  }
}

void
GPU::ee_tick_commands()
{
  struct Command {
    union {
      struct {
        u32 value;
        u32 command;
      };
      u64 raw;
    };
  };
  Command command_packet;

  for (int timeslice_cmd_count = 0;
       timeslice_cmd_count < 128 && m_ee.fifo_address_current != m_ee.fifo_address_end;
       ++timeslice_cmd_count) {

    command_packet.raw = m_console->memory()->read<u64>(m_ee.fifo_address_current);

    if ((command_packet.command & 0x100) == 0) {
      // Normal register write
      const u32 reg_index = command_packet.command & 0xff;
      switch (reg_index) {

        case DRAW_BIN_XY:
          m_state.bin_x = command_packet.value & 0xffff;
          m_state.bin_y = command_packet.value >> 16;
          // printf("drawxy %u %u\n", m_state.bin_x, m_state.bin_y);
          break;

        case WAIT:
          // printf("TODO: WAIT on 0x%08x\n", command_packet.value);

          // Go into waiting, immediately process wait. Wait might determine
          // immediately that no waiting is needed, so we could continue processing
          // commands. If we do actually stay in the waiting state, stop processing
          // commands this timeslice.
          m_ee.state = EESMState::Waiting;
          m_ee.wait_bits = command_packet.value;
          ee_tick_wait();

          // If we're still waiting, stop processing commands this timeslice. Immediately
          // return.
          if (EESMState::Waiting == m_ee.state) {
            return;
          }
          break;
        case EE_INTERRUPT:
          printf("TODO: Generate interrupt 0x%08x\n", command_packet.value);
          break;

        case VPU0_DMA_CONFIG:
        case VPU1_DMA_CONFIG: {
          const u32 vpu_index = reg_index - VPU0_DMA_CONFIG;
          m_state.vpu_dma_state[vpu_index].dma_config.raw = command_packet.value;
          // printf("TODO: vpu(%u) dma config 0x%08x\n", vpu_index,
          // command_packet.value);
        } break;

        case VPU0_DMA_BUFFER_ADDR:
        case VPU1_DMA_BUFFER_ADDR: {
          const u32 vpu_index = reg_index - VPU0_DMA_BUFFER_ADDR;
          m_state.vpu_dma_state[vpu_index].dma_buffer_addr = command_packet.value;
          // printf("TODO: vpu(%u) dma buffer addr 0x%08x (id %u)\n",
          //        vpu_index,
          //        u16(command_packet.value),
          //        buffer_id);
        } break;

        case VPU0_DMA_EXTERNAL_ADDR:
        case VPU1_DMA_EXTERNAL_ADDR: {
          const u32 vpu_index = reg_index - VPU0_DMA_EXTERNAL_ADDR;
          m_state.vpu_dma_state[vpu_index].dma_external_addr = command_packet.value;
          // printf("TODO: vpu(%u) dma external addr 0x%08x\n",
          //        vpu_index,
          //        command_packet.value);
        } break;

        case VPU_REG_XY:
          m_state.vpu_reg_xy = command_packet.value;
          // printf("TODO: GPU_GLOBAL_XY 0x%08x\n", command_packet.value);
          break;
        case VPU_REG_ZW:
          m_state.vpu_reg_zw = command_packet.value;
          // printf("TODO: GPU_GLOBAL_ZW 0x%08x\n", command_packet.value);
          break;

        case EXEC_VPU0_DMA:
        case EXEC_VPU1_DMA: {
          const u32 vpu_index = reg_index - EXEC_VPU0_DMA;
          m_state.busy_vpu_dma[vpu_index] = 1;

          std::lock_guard lock { m_work_queue_mutex };
          m_work_queue.push(WorkItem_VPUDMA {
            .vpu_index = vpu_index,
            .dma_config = m_state.vpu_dma_state[vpu_index].dma_config,
            .dma_buffer_addr = m_state.vpu_dma_state[vpu_index].dma_buffer_addr,
            .dma_external_addr = m_state.vpu_dma_state[vpu_index].dma_external_addr,
          });
        } break;

        case EXEC_WRITE_VPU_GLOBAL: {
          // printf("TODO: EXEC_WRITE_VPU_GLOBAL\n");
          std::lock_guard lock { m_work_queue_mutex };
          m_work_queue.push(WorkItem_SetVPUGlobal {
            .register_index = command_packet.value,
            .xy = m_state.vpu_reg_xy,
            .zw = m_state.vpu_reg_zw,
          });
        } break;

        case EXEC_WRITE_VPU_SHARED: {
          // printf("TODO: EXEC_WRITE_VPU_SHARED\n");
          std::lock_guard lock { m_work_queue_mutex };
          m_work_queue.push(WorkItem_SetVPUShared {
            .register_index = command_packet.value,
            .xy = m_state.vpu_reg_xy,
            .zw = m_state.vpu_reg_zw,
          });
        } break;

        case EXEC_VPU_LAUNCH_ARRAY: {
          // printf("TODO: EXEC_VPU_LAUNCH_ARRAY\n");
          std::lock_guard lock { m_work_queue_mutex };
          m_work_queue.push(WorkItem_VPULaunchArray {
            .pc_offset = command_packet.value & 0xf,
            .count = ((command_packet.value >> 4) & 0x3ff) + 1,
          });
        } break;

        default:
          printf("UNHANDLED gpu_ee_fifo @ 0x%08x : cmd 0x%08x <- val 0x%08x\n",
                 m_ee.fifo_address_current,
                 command_packet.command,
                 command_packet.value);
          break;
      }
    }

    if (m_ee.state == EESMState::Waiting) {
      // If we entered waiting, we don't advance address current, and a later time slice
      // will unset this.
      return;
    }

    m_ee.fifo_address_current += sizeof(Command);
    m_registers[Register::CMD_FIFO_COUNT]--;

    if (m_ee.fifo_address_current == m_ee.fifo_address_end) {
      // printf("GPU EE reached 0x%08x, stopping\n", m_ee.fifo_address_current);
      m_ee.state = EESMState::Idle;
      return;
    }
  }
}

bool
GPU::worker_peek(WorkQueueItem *item)
{
  assert(item != nullptr);
  std::lock_guard lock { m_work_queue_mutex };

  if (!m_work_queue.empty()) {
    *item = m_work_queue.front();
    return true;
  }

  return false;
}

void
GPU::worker_pop()
{
  std::lock_guard lock { m_work_queue_mutex };
  m_work_queue.pop();
}

void
GPU::worker_thread_body()
{
  while (!m_worker_shutdown) {

    m_state.vpu[0].step_cycles(1000);
    m_state.vpu[1].step_cycles(1000);

    WorkQueueItem item;
    if (!worker_peek(&item)) {
      // std::this_thread::sleep_for(std::chrono::milliseconds(10));
      std::this_thread::yield();
      continue;
    }

    if (std::holds_alternative<WorkItem_VPUDMA>(item)) {
      func_vpu_dma(std::get<WorkItem_VPUDMA>(item));
    }

    if (std::holds_alternative<WorkItem_SetVPUGlobal>(item)) {
      func_vpu_set_global(std::get<WorkItem_SetVPUGlobal>(item));
    }

    if (std::holds_alternative<WorkItem_SetVPUShared>(item)) {
      func_vpu_set_shared(std::get<WorkItem_SetVPUShared>(item));
    }

    if (std::holds_alternative<WorkItem_VPULaunchArray>(item)) {
      func_vpu_launch_array(std::get<WorkItem_VPULaunchArray>(item));
    }

    worker_pop();
  }
}

void
GPU::ee_fifo_callback()
{
  const u64 EE_FIFO_TIME_SLICE_NANOS = 1000 * 1;

  switch (m_ee.state) {
    case EESMState::Idle:
      if (m_registers[CMD_BUF_EXEC]) {
        m_ee.fifo_address_current = m_registers[CMD_BUF_BEGIN];
        m_ee.fifo_address_end = m_registers[CMD_BUF_END];

        printf("Beginning GPU EE buffer [0x%08x, 0x%08x)\n",
               m_ee.fifo_address_current,
               m_ee.fifo_address_end);

        {
          std::lock_guard lock { m_command_list_mutex };
          m_command_list.base_address = m_ee.fifo_address_current;
          m_command_list.end_address = m_ee.fifo_address_end;
          m_command_list.commands.clear();
          m_command_list.id = ++m_command_list_counter;
          for (u32 addr = m_ee.fifo_address_current; addr < m_ee.fifo_address_end;
               addr += sizeof(Command)) {
            m_command_list.commands.push_back(
              Command { .raw = m_console->memory()->read<u64>(addr) });
          }
        }

        // GPU sets this back to 0, marking cmd parameters consumed. CPU can see this.
        m_registers[CMD_BUF_EXEC] = 0;

        m_ee.state = EESMState::Running;
        ee_tick_commands();
      }
      break;

    case EESMState::Running:
      ee_tick_commands();
      break;

    case EESMState::Waiting:
      ee_tick_wait();
      // If we finish waiting this time slice, go try to run some commands
      if (m_ee.state == EESMState::Running) {
        ee_tick_commands();
      }
      break;
    default:
      assert(false);
      break;
  }

  // TODO : Recompute cpu-visibile state like whether the gpu is busy or waiting etc.

  m_console->schedule_event_nanos(EE_FIFO_TIME_SLICE_NANOS, &m_ee_fifo_callback);
}

void
GPU::write_u32(u32 addr, u32 value)
{
  const u32 reg_index = (addr >> 2) & 0x1ff;
  assert(reg_index < GPU_TOTAL_REGISTERS);
  // printf("gpu: write_u32 0x%08x < 0x%x pc=0x%08x\n", addr, value,
  // m_console->cpu()->PC());

  switch (reg_index) {
    case Register::CMD_FIFO_START:
    case Register::CMD_FIFO_CLEAR:
    case Register::CMD_BUF_EXEC:
      m_registers[reg_index] = value & 1;
      break;

    case Register::CMD_BUF_BEGIN:
    case Register::CMD_BUF_END:
      m_registers[reg_index] = value;
      break;

    default:
      throw std::runtime_error("Unhandled gpu 32b write");
      break;
  }

  // At the moment an exec occurs, make cpu-visible the number of commands remaining
  if (reg_index == Register::CMD_BUF_EXEC) {
    const u32 start = m_registers[Register::CMD_BUF_BEGIN];
    const u32 end = m_registers[Register::CMD_BUF_END];
    m_registers[Register::CMD_FIFO_COUNT] = (end - start) / sizeof(Command);
  }
}

void
GPU::handle_command(u32 value)
{
}

void
GPU::register_regions(fox::MemoryTable *memory)
{
  memory->map_mmio(m_base_address, GPU_TOTAL_REGISTERS, "GPU", this);
}

} // namespace zoo::ps1
