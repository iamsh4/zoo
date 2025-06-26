// vim: expandtab:ts=2:sw=2

#include <fmt/core.h>
#include <iomanip>
#include <bitset>
#include <cstring>
#include <cassert>
#include <type_traits>
#include <unordered_map>
#include <algorithm>

#include "shared/print.h"
#include "shared/bitmanip.h"
#include "shared/profiling.h"
#include "shared/error.h"
#include "shared/log.h"
#include "core/console.h"
#include "sh4.h"
#include "sh4_jit.h"

#if 0
#define DEBUG(args...) fmt::print(args)
#else
#define DEBUG(args...)
#endif

static Log::Logger<Log::LogModule::SH4> logger;

namespace cpu {

const u64 kNanosPerTmuUpdate = 50'000;

class sh4_exception {
public:
  enum Kind
  {
    DataTlbMiss
  };

  sh4_exception(Kind k) : kind(k)
  {
    return;
  }

  static const char *to_string(const Kind k)
  {
    switch (k) {
      case DataTlbMiss:
        return "Data TLB Miss";
      default:
        return "Unknown";
    }
  }

  const Kind kind;
};

SH4::SH4(Console *const console)
  : m_phys_mem(console->memory()),
    m_jit_cache(new fox::jit::Cache(m_phys_mem)),
    m_console(console),
    m_tmu_event(EventScheduler::Event("sh4.tmu.tcnt0",
                                      std::bind(&SH4::tick_tmu_channels, this),
                                      console->scheduler())),
    m_sampling_profiler(
      EventScheduler::Event("sh4.sampling-profiler",
                            std::bind(&SH4::handle_sampling_profiler_tick, this),
                            console->scheduler()))
{
  power_on_reset();
}

void
SH4::set_sampling_profiler_running(const bool should_run)
{
  if (should_run && !m_sampling_profiler.is_scheduled()) {
    m_console->schedule_event(100, &m_sampling_profiler);
  } else if (!should_run && m_sampling_profiler.is_scheduled()) {
    m_sampling_profiler.cancel();
  }
}

SH4::~SH4()
{
  m_tmu_event.cancel();
  m_sampling_profiler.cancel();
}

bool
SH4::check_interrupt(u32 *const out_interrupt_index, u32 *const out_priority)
{
  if (m_pending_interrupts == 0)
    return false;

  u32 highest_priority = 0;
  u32 index            = 0;
  for (u32 i = 0; i < m_interrupt_table.size(); i++) {
    if (m_pending_interrupts & (1u << i)) {
      if (m_interrupt_table[i].priority > highest_priority) {
        highest_priority = m_interrupt_table[i].priority;
        index            = i;
      }
    }
  }

  if (highest_priority > regs.SR.IMASK) {
    *out_interrupt_index = index;
    *out_priority        = highest_priority;
    return true;
  }

  return false;
}

u64
SH4::step()
{
  if (m_debug_watchpoint_triggered) {
    m_debug_watchpoint_triggered = false;
    throw BreakpointException();
  }

  if (m_pending_interrupts && !regs.SR.BL && !in_delay_slot()) {
    u32 index, priority;
    if (check_interrupt(&index, &priority) &&
        (!m_debug_mode || !m_debug_mask_interrupts)) {
      logger.debug(
        "[%09lu] SH4 handling interrupt with Priority=%u IMASK=%u From=0x%08x To=0x%08x",
        m_console->current_time(),
        priority,
        regs.SR.IMASK,
        regs.PC,
        regs.VBR + 0x600u);

      handle_interrupt(index);
    }
  }

  const u16 fetch = idata_read(regs.PC);
  const u16 opcode_id = decode_table[fetch];
  const Opcode &opcode = opcode_table[opcode_id];
  const bool delay_slot = in_delay_slot();
  m_executed_branch = false;

  if (opcode.execute == NULL) {
    throw BreakpointException();
  }

  try {
    (this->*opcode.execute)(fetch);
  }

  catch (const sh4_exception &exception) {
    logger.debug("CPU handling exception: %s", sh4_exception::to_string(exception.kind));
    throw std::runtime_error("CPU unhandled exception");
  }

  if (!m_executed_branch || m_branch_target != 0xFFFFFFFFu) {
    /* We need to run the instruction following PC; either we didn't branch
     * at all, or the branch has a delay slot */
    regs.PC += sizeof(u16);
  }

  if (delay_slot && m_branch_target != 0xFFFFFFFFu) {
    /* Previous instruction was a branch and we just ran the delay slot */
    regs.PC = m_branch_target;
    m_branch_target = 0xFFFFFFFFu;
  }

  if (m_debug_mode) {
    if (m_debug_breakpoints.count(regs.PC & 0x1FFFFFFFu)) {
      throw BreakpointException();
    }
  }

  return opcode.cycles;
}

u32 exception_return_pc = 0xFFFFFFFF;

u64
SH4::step_block()
{
  if (m_debug_watchpoint_triggered) {
    m_debug_watchpoint_triggered = false;
    throw BreakpointException();
  }

  m_console->memory_usage().ram->set(0x0C00'0000 | (regs.PC & ~0xF000'0000),
                                     dreamcast::SH4_Code);

  if (in_delay_slot() || m_debug_mode) {
    /* TODO : Add trace info while we're debugging. */
    return step();
  }

  if (m_pending_interrupts && !regs.SR.BL) {
    u32 index, priority;
    if (check_interrupt(&index, &priority)) {
      logger.debug(
        "[%09lu] SH4 handling interrupt with Priority=%u IMASK=%u From=0x%08x To=0x%08x",
        m_console->current_time(),
        priority,
        regs.SR.IMASK,
        regs.PC,
        regs.VBR + 0x600u);

      exception_return_pc = regs.PC;
      handle_interrupt(index);
    }
  }

  if (regs.PC == exception_return_pc) {
    exception_return_pc = 0xFFFFFFFF;
  }

  if (m_last_block) {
    if (!m_last_block->is_invalidated() && m_last_block->virtual_address() == regs.PC) {
      return execute_block(static_cast<BasicBlock *>(m_last_block));
    }

    fox::ref<fox::jit::CacheEntry> &next = m_last_block->next_block;
    if (next && !next->is_invalidated() && next->virtual_address() == regs.PC) {
      m_last_block = next.get();
      return execute_block(static_cast<BasicBlock *>(m_last_block));
    }
  }

  fox::jit::CacheEntry *entry = m_jit_cache->lookup(regs.PC);
  if (!entry) {
    /* Cache will maintain a reference on it until we call garbage_collect(). */
    fox::ref<fox::jit::CacheEntry> ref_entry = jit_create_unit(regs.PC);
    m_jit_cache->insert(ref_entry.get());
    entry = ref_entry.get();
  }

  if (m_last_block) {
    if (m_last_block->is_invalidated()) {
      m_last_block->next_block = nullptr;
    } else if (m_last_block->next_block.get() != entry) {
      m_last_block->next_block = entry;
    }
  }

  const u64 cycles_executed = execute_block(static_cast<BasicBlock *>(entry));
  m_last_block = entry;

  if (m_jit_cache->garbage_collect()) {
    m_last_block = nullptr;
  }

  return cycles_executed;
}

u64
SH4::execute_block(BasicBlock *block)
{
  /* This value is incremented by basic block execution. Reset to record only
   * the cycle count of the current block. */
  m_jit_cycles = 0;
  block->execute(this);

#if 0
  const u32 pc_before = regs.PC;
  if (!m_execution_graph.has_code_region(pc_before) &&
      exception_return_pc == 0xFFFFFFFF) {
    const auto &unit = block->unit();

    if (unit) {
      const std::string disassembly(unit->disassemble());
      const u32 top_of_call_stack = !m_call_stack.empty() ? m_call_stack.back() : 0;
      m_execution_graph.add_code_region(ExecutionGraph::Node {
        .start_pc = pc_before,
        .code_length = (u32)unit->instructions().size(),
        .top_of_call_stack = top_of_call_stack,
        .label = std::move(disassembly),
      });
    }
  }

  if (exception_return_pc == 0xFFFFFFFF) {
    m_execution_graph.increment_edge(pc_before, regs.PC);
  }
#endif

  return m_jit_cycles;
}

void
SH4::reset()
{
  power_on_reset();
}

void
SH4::latch_irq(u32 irq_line)
{
  m_pending_interrupts |= (1u << (uint32_t(IRQ0) + irq_line));
}

void
SH4::cancel_irq(u32 irq_line)
{
  m_pending_interrupts &= ~(1u << (uint32_t(IRQ0) + irq_line));
}

void
SH4::jmp_delay(u32 address)
{
  if (m_branch_target != 0xFFFFFFFFu) {
    throw std::runtime_error("Unhandled jmp_delay() in delay slot!");
  }

  m_branch_target = address;
  m_executed_branch = true;
}

void
SH4::jmp_nodelay(u32 address)
{
  if (m_branch_target != 0xFFFFFFFFu) {
    throw std::runtime_error("Unhandled jmp_nodelay() in delay slot!");
  }

  regs.PC = address;
  m_executed_branch = true;
}

bool
SH4::in_delay_slot() const
{
  return m_branch_target != 0xFFFFFFFFu;
}

void *
SH4::get_operand_cache_pointer(const u32 address)
{
  /*
   * OIX = 0
   * H'7C00 0000 to H'7C00 0FFF (4 kB): Corresponds to RAM area 1
   * H'7C00 1000 to H'7C00 1FFF (4 kB): Corresponds to RAM area 1
   * H'7C00 2000 to H'7C00 2FFF (4 kB): Corresponds to RAM area 2
   * H'7C00 3000 to H'7C00 3FFF (4 kB): Corresponds to RAM area 2
   * H'7C00 4000 to H'7C00 4FFF (4 kB): Corresponds to RAM area 1
   * ...
   *
   * OIX = 1
   * H'7C00 0000 to H'7C00 0FFF (4 kB): Corresponds to RAM area 1
   * H'7C00 1000 to H'7C00 1FFF (4 kB): Corresponds to RAM area 1
   * H'7C00 2000 to H'7C00 2FFF (4 kB): Corresponds to RAM area 1
   * ...
   * H'7DFF F000 to H'7DFF FFFF (4 kB): Corresponds to RAM area 1
   * H'7E00 0000 to H'7E00 0FFF (4 kB): Corresponds to RAM area 2
   * H'7E00 1000 to H'7E00 1FFF (4 kB): Corresponds to RAM area 2
   */

  /* CCR OIX indexing mode causes different wrapping behaviors ^^^ */
  const u32 area_select = m_mmio.CCR.OIX ? (address >> 25) & 1 : (address >> 13) & 1;
  return &m_operand_cache[area_select * 4096 + (address & 0xfff)];
}

/* TODO: TLB, CPU MMIO, etc. */
template<typename T>
void
SH4::mem_write(u32 raw_dst, T value)
{
  if ((raw_dst & (sizeof(T) - 1u)) != 0u) {
    throw std::runtime_error(std::string("CPU write not aligned to type size @ 0x") +
                             hex_format(raw_dst).str() + " size " +
                             std::to_string(sizeof(T)));
  }

  if (m_debug_write_watchpoints.count(raw_dst) > 0) {
    printf("Write watch @ 0x%08x triggered at SH4 PC 0x%08x\n", raw_dst, regs.PC);
    m_debug_watchpoint_triggered = true;
  }

  const auto dst = mem_region(raw_dst, true /* TODO */);
  switch (dst.first) {
    case AddressType::Invalid:
    case AddressType::AccessViolation:
      /* TODO: Throw exception if access violation */
      throw std::runtime_error(std::string("Invalid or violating write access @ 0x") +
                               hex_format(raw_dst).str());

    case AddressType::OperandCache:
      memcpy(get_operand_cache_pointer(dst.second), &value, sizeof(T));
      return;

    case AddressType::Register:
      mmio_write(dst.second, value, sizeof(T));
      return;

    case AddressType::Physical:
      m_phys_mem->write(dst.second, value);
      return;

    case AddressType::StoreQueue:
      memcpy(&SQ[dst.second], &value, sizeof(T));
      return;

    case AddressType::Virtual:
      throw sh4_exception(sh4_exception::DataTlbMiss);

      /* Force data TLB miss exception */
      m_mmio.PTEH.raw = raw_dst >> 10u;
      m_mmio.TEA.raw = raw_dst;
      m_mmio.EXPEVT.raw = 0x60u;

      if (regs.SSR.RB != regs.SR.RB) {
        gpr_swap_bank();
      }

      /* XXX Not sure about this */
      regs.SPC        = in_delay_slot() ? (regs.PC - 2u) : regs.PC;
      regs.SSR        = regs.SR;
      regs.SGR        = regs.general_registers[15];
      regs.SR.MD      = 1u;
      regs.SR.BL      = 1u;
      regs.SR.RB      = 1u;
      regs.PC         = regs.VBR + 0x0400u;
      m_branch_target = 0xFFFFFFFFu;
      return;
  }

  UNREACHABLE();
}

void
SH4::sq_flush(u32 address)
{
  try {
    if ((address & 0x20) == 0x00) {
      /* SQ0 specification */
      const u32 physical = (address & 0x03ffffe0u) | (m_mmio.QACR0.raw << 24u);
      m_phys_mem->dma_write(physical, &SQ[0], 32u);
    } else {
      /* SQ1 specification */
      const u32 physical = (address & 0x03ffffe0u) | (m_mmio.QACR1.raw << 24u);
      m_phys_mem->dma_write(physical, &SQ[32u], 32u);
    }
  } catch (...) {
    /* Errors in prefetch are treated as no-ops. */
  }
}

u16
SH4::idata_read(u32 raw_src) const
{
  if (raw_src & 0x1u) {
    throw std::runtime_error(
      std::string("CPU instruction read not aligned to type size @ 0x") +
      hex_format(raw_src).str());
  }

  // printf("READ FROM %x\n", (unsigned) raw_src);

  const auto src = mem_region(raw_src, true /* TODO */);
  switch (src.first) {
    case AddressType::Physical:
      /* TODO: CPU exception */
      return m_phys_mem->read<u16>(src.second);

    case AddressType::Invalid:
    case AddressType::AccessViolation:
    case AddressType::Register:
    case AddressType::StoreQueue:
      /* TODO: Throw exception if access violation */
      throw std::runtime_error(
        std::string("Invalid or violating instruction read access @ 0x") +
        hex_format(raw_src).str());

    case AddressType::Virtual:
      throw std::runtime_error("TLB not implemented! (idata read)");

    default:
      throw std::runtime_error("Not a valid address type.");
  }
}

/* TODO TLB, CPU MMIO, etc. */
template<typename T>
T
SH4::mem_read(u32 raw_src)
{
  if ((raw_src & (sizeof(T) - 1u)) != 0u) {
    throw std::runtime_error(std::string("CPU read not aligned to type size @ 0x") +
                             hex_format(raw_src).str() + " size " +
                             std::to_string(sizeof(T)));
  }

  if (m_debug_read_watchpoints.count(raw_src) > 0) {
    printf("Read watch @ 0x%08x triggered at SH4 PC 0x%08x\n", raw_src, regs.PC);
    m_debug_watchpoint_triggered = true;
  }

  const auto src = mem_region(raw_src, true /* TODO */);
  switch (src.first) {
    case AddressType::Invalid:
    case AddressType::AccessViolation:
    case AddressType::StoreQueue:
      /* TODO Throw exception if access violation */
      throw std::runtime_error(std::string("Invalid or violating read access @ 0x") +
                               hex_format(raw_src).str());

    case AddressType::OperandCache: {
      T result = 0;
      memcpy(&result, get_operand_cache_pointer(src.second), sizeof(T));
      return result;
    }

    case AddressType::Register:
      if (sizeof(T) == 4u) {
        return mmio_read_long(src.second);
      } else if (sizeof(T) == 2u) {
        return mmio_read_word(src.second);
      } else {
        if (src.second == 0xFFD80004) {
          return mmio_read_long(src.second) & 0xFF;
        }

        logger.error("Unhandled register mmio read @ 0x%08x -> src.second 0x%08x\n",
                     raw_src,
                     src.second);
        return 0u;
      }

    case AddressType::Physical:
      /* TODO CPU exception */
      return m_phys_mem->read<T>(src.second);

    case AddressType::Virtual:
      throw std::runtime_error("TLB not implemented (read)!");

    default:
      throw std::runtime_error("Invalid address type.");
  }
}

u32
SH4::mmio_read_long(u32 address) const
{
  /* Store queue */
  if (address >= 0xFF001000u && address <= 0xFF00103Fu) {
    u32 result;
    memcpy(&result, &SQ[address & 0x3fu], sizeof(u32));
    return result;
  }

  if (address >= 0xF4000000u && address < 0xF5000000) {
    /* OC Address Array */
    return 0;
  }

  if (address >= 0xF6000000u && address < 0xF7000000) {
    /* UTLB address array */
    return 0;
  }

  switch (address) {
    case 0xFFA00040:
      return 0x8201u;
    /******** Exception registers ********/
    case MMIO::TRA_bits::address:
      return m_mmio.TRA.raw;

    case MMIO::EXPEVT_bits::address:
      return m_mmio.EXPEVT.raw;

    case MMIO::INTEVT_bits::address:
      return m_mmio.INTEVT.raw;

    /******** MMU registers ********/
    case MMIO::PTEH_bits::address:
      return m_mmio.PTEH.raw;

    case MMIO::PTEL_bits::address:
      return m_mmio.PTEL.raw;

    case MMIO::TTB_bits::address:
      return m_mmio.TTB.raw;

    case MMIO::TEA_bits::address:
      return m_mmio.TEA.raw;

    case MMIO::MMUCR_bits::address:
      logger.debug("Read from MMUCR u32");
      return m_mmio.MMUCR.raw;

    /******** Cache registers ********/
    case MMIO::CCR_bits::address:
      return m_mmio.CCR.raw;

    case MMIO::QACR0_bits::address:
      return m_mmio.QACR0.raw;

    case MMIO::QACR1_bits::address:
      return m_mmio.QACR1.raw;

    case MMIO::RAMCR_bits::address:
      return m_mmio.RAMCR.raw;

    /******** DMA registers ********/
    case MMIO::SARn_bits::address + (MMIO::SARn_bits::stride * 0u):
    case MMIO::SARn_bits::address + (MMIO::SARn_bits::stride * 1u):
    case MMIO::SARn_bits::address + (MMIO::SARn_bits::stride * 2u):
    case MMIO::SARn_bits::address + (MMIO::SARn_bits::stride * 3u): {
      const u32 index = (address - MMIO::SARn_bits::address) / MMIO::SARn_bits::stride;
      return m_mmio.SARn[index].raw;
      break;
    }

    case MMIO::DARn_bits::address + (MMIO::DARn_bits::stride * 0u):
    case MMIO::DARn_bits::address + (MMIO::DARn_bits::stride * 1u):
    case MMIO::DARn_bits::address + (MMIO::DARn_bits::stride * 2u):
    case MMIO::DARn_bits::address + (MMIO::DARn_bits::stride * 3u): {
      const u32 index = (address - MMIO::DARn_bits::address) / MMIO::DARn_bits::stride;
      return m_mmio.DARn[index].raw;
      break;
    }

    case MMIO::DMATCRn_bits::address + (MMIO::DMATCRn_bits::stride * 0u):
    case MMIO::DMATCRn_bits::address + (MMIO::DMATCRn_bits::stride * 1u):
    case MMIO::DMATCRn_bits::address + (MMIO::DMATCRn_bits::stride * 2u):
    case MMIO::DMATCRn_bits::address + (MMIO::DMATCRn_bits::stride * 3u): {
      const u32 index =
        (address - MMIO::DMATCRn_bits::address) / MMIO::DMATCRn_bits::stride;
      return m_mmio.DMATCRn[index].raw;
      break;
    }

    case MMIO::CHCRn_bits::address + (MMIO::CHCRn_bits::stride * 0u):
    case MMIO::CHCRn_bits::address + (MMIO::CHCRn_bits::stride * 1u):
    case MMIO::CHCRn_bits::address + (MMIO::CHCRn_bits::stride * 2u):
    case MMIO::CHCRn_bits::address + (MMIO::CHCRn_bits::stride * 3u): {
      const u32 index = (address - MMIO::CHCRn_bits::address) / MMIO::CHCRn_bits::stride;
      return m_mmio.CHCRn[index].raw;
      break;
    }

    /******** Interrupt priority control ********/
    case MMIO::IPRA_bits::address:
      return m_mmio.IPRA.raw;
      break;

    case MMIO::IPRB_bits::address:
      return m_mmio.IPRB.raw;
      break;

    case MMIO::IPRC_bits::address:
      return m_mmio.IPRC.raw;
      break;

    /******** Timer registers ********/
    case MMIO::TOCR_bits::address:
      DEBUG("Read from CPU timer register unimplemented address=0x{:08x}", address);
      return m_mmio.TOCR.raw;

    case MMIO::TSTR_bits::address:
      return m_mmio.TSTR.raw;

    case MMIO::TCOR_bits::address + (MMIO::TCOR_bits::stride * 0u):
    case MMIO::TCOR_bits::address + (MMIO::TCOR_bits::stride * 1u):
    case MMIO::TCOR_bits::address + (MMIO::TCOR_bits::stride * 2u): {
      const u32 index = (address - MMIO::TCOR_bits::address) / MMIO::TCOR_bits::stride;
      return m_mmio.TCOR[index].raw;
    }

    case MMIO::TCNT_bits::address + (MMIO::TCNT_bits::stride * 0u):
    case MMIO::TCNT_bits::address + (MMIO::TCNT_bits::stride * 1u):
    case MMIO::TCNT_bits::address + (MMIO::TCNT_bits::stride * 2u): {
      const u32 index = (address - MMIO::TCNT_bits::address) / MMIO::TCNT_bits::stride;
      return m_mmio.TCNT[index].raw;
    }

    /******** Bus State Controller Registers ********/
    case MMIO::PCTRA_bits::address:
      return m_mmio.PCTRA.raw | 0x300;

    /******** Other ********/
    case 0xFF000030u:
      /* Poorly documented SH4 Version Register XXX */
      return 0x040205C1u;

    case 0xFF800030:
      printf("watwat\n");
      return 0x00000000u;

    case 0xFF800044:
      return 0xffffffffu;

    default:
      logger.warn("Read from unimplemented u32 CPU MMIO address=%08x returns 0u",
                  address);
      logger.error("CPU unhandled read_32 0x%08x (PC=0x%08x)\n", address, regs.PC);
      return 0u;
  }
}

u16
SH4::mmio_read_word(u32 address) const
{
  switch (address) {
    case 0xFF800028u:
      return 0xA400u; /* RFCR */

    /******** Bus State Controller Registers ********/
    case MMIO::PDTRA_bits::address: {
      m_mmio.PDTRA.raw = m_mmio.PDTRA.raw ? 0u : 3u;
      // return m_mmio.PDTRA.raw | 0x300u /* Cable Type: NTSC */;
      return m_mmio.PDTRA.raw | 0x000u /* Cable Type: VGA */;
    }

    /******** TMU Control Registers ********/
    case MMIO::TCR_bits::address + (MMIO::TCR_bits::stride * 0u):
    case MMIO::TCR_bits::address + (MMIO::TCR_bits::stride * 1u):
    case MMIO::TCR_bits::address + (MMIO::TCR_bits::stride * 2u): {
      const u32 index = (address - MMIO::TCR_bits::address) / MMIO::TCR_bits::stride;
      return m_mmio.TCR[index].raw;
    }

    case MMIO::IPRA_bits::address:
      return m_mmio.IPRA.raw;
      break;

    case MMIO::IPRB_bits::address:
      return m_mmio.IPRB.raw;
      break;

    case MMIO::IPRC_bits::address:
      return m_mmio.IPRC.raw;
      break;

    default:
      logger.debug("Read from unimplemented u16 CPU MMIO address=%08x returns 0u",
                   address);
      logger.error("CPU unhandled read_16 0x%08x (PC=0x%08x)\n", address, regs.PC);
      printf("CPU unhandled mmio read_16 0x%08x (PC=0x%08x)\n", address, regs.PC);
      throw std::runtime_error("Unhandled cpu mmio read_16");
      return 0u;
  }
}

void
SH4::mmio_write(u32 address, u32 value, u32 size)
{
  /******** Operand Cache ********/
  if (address >= 0xF4000000u && address <= 0xF4FFFFFFu) {
    if (value != 0u) {
      logger.warn("Unhandled write to CPU operand cache address=0x%08x value=0x%08x",
                  address,
                  value);

      printf("0xF4.. write 0x%08x PC=0x%08x < val 0x%x\n", address, regs.PC, value);
    }

    return;
  }

  if ((address & 0xffff0000) == 0xff940000) {
    /* SDMR3 Syncronous DRAM Mode Register 3 */
    return;
  }

  switch (address) {
    case 0xffe0000c:
      printf("SERIAL TRANSMIT : '%c'\n", value);
      break;

    /******** MMU registers ********/
    case MMIO::PTEH_bits::address:
      m_mmio.PTEH.raw = value & m_mmio.PTEH.mask;
      break;

    case MMIO::PTEL_bits::address:
      m_mmio.PTEL.raw = value & m_mmio.PTEL.mask;
      break;

    case MMIO::TTB_bits::address:
      m_mmio.TTB.raw = value & m_mmio.TTB.mask;
      break;

    case MMIO::TEA_bits::address:
      m_mmio.TEA.raw = value & m_mmio.TEA.mask;
      break;

    case MMIO::MMUCR_bits::address:
      logger.debug("Wrote to MMUCR u32 value %08x", value);
      m_mmio.MMUCR.raw = value & m_mmio.MMUCR.mask;
      if (m_mmio.MMUCR.AT) {
        logger.error("Enabled MMU (AT bit set), but not supported!");
      }
      break;

    /******** Exception registers ********/
    case MMIO::TRA_bits::address:
      m_mmio.TRA.raw = value & m_mmio.TRA.mask;
      break;

    case MMIO::EXPEVT_bits::address:
      m_mmio.EXPEVT.raw = value & m_mmio.EXPEVT.mask;
      break;

    case MMIO::INTEVT_bits::address:
      m_mmio.INTEVT.raw = value & m_mmio.INTEVT.mask;
      break;

    /******** Cache registers ********/
    case MMIO::CCR_bits::address:
      m_mmio.CCR.raw = value & m_mmio.CCR.mask;
      break;

    case MMIO::QACR0_bits::address:
      m_mmio.QACR0.raw = value & m_mmio.QACR0.mask;
      break;

    case MMIO::QACR1_bits::address:
      m_mmio.QACR1.raw = value & m_mmio.QACR1.mask;
      break;

    case MMIO::RAMCR_bits::address:
      m_mmio.RAMCR.raw = value & m_mmio.RAMCR.mask;
      break;

    /******** DMA registers ********/
    case MMIO::SARn_bits::address + (MMIO::SARn_bits::stride * 0u):
    case MMIO::SARn_bits::address + (MMIO::SARn_bits::stride * 1u):
    case MMIO::SARn_bits::address + (MMIO::SARn_bits::stride * 2u):
    case MMIO::SARn_bits::address + (MMIO::SARn_bits::stride * 3u): {
      const u32 index = (address - MMIO::SARn_bits::address) / MMIO::SARn_bits::stride;
      m_mmio.SARn[index].raw = value & MMIO::SARn_bits::mask;
      break;
    }

    case MMIO::DARn_bits::address + (MMIO::DARn_bits::stride * 0u):
    case MMIO::DARn_bits::address + (MMIO::DARn_bits::stride * 1u):
    case MMIO::DARn_bits::address + (MMIO::DARn_bits::stride * 2u):
    case MMIO::DARn_bits::address + (MMIO::DARn_bits::stride * 3u): {
      const u32 index = (address - MMIO::DARn_bits::address) / MMIO::DARn_bits::stride;
      m_mmio.DARn[index].raw = value & MMIO::DARn_bits::mask;
      break;
    }

    case MMIO::DMATCRn_bits::address + (MMIO::DMATCRn_bits::stride * 0u):
    case MMIO::DMATCRn_bits::address + (MMIO::DMATCRn_bits::stride * 1u):
    case MMIO::DMATCRn_bits::address + (MMIO::DMATCRn_bits::stride * 2u):
    case MMIO::DMATCRn_bits::address + (MMIO::DMATCRn_bits::stride * 3u): {
      const u32 index =
        (address - MMIO::DMATCRn_bits::address) / MMIO::DMATCRn_bits::stride;
      m_mmio.DMATCRn[index].raw = value & MMIO::DMATCRn_bits::mask;
      break;
    }

    case MMIO::CHCRn_bits::address + (MMIO::CHCRn_bits::stride * 0u):
    case MMIO::CHCRn_bits::address + (MMIO::CHCRn_bits::stride * 1u):
    case MMIO::CHCRn_bits::address + (MMIO::CHCRn_bits::stride * 2u):
    case MMIO::CHCRn_bits::address + (MMIO::CHCRn_bits::stride * 3u): {
      const u32 index = (address - MMIO::CHCRn_bits::address) / MMIO::CHCRn_bits::stride;
      m_mmio.CHCRn[index].raw = value & MMIO::CHCRn_bits::mask;
      break;
    }

    case 0xFFA00040:
      /* DMAOR */
      /* We expect this to be in the format "0xyyyy8201" which basically enables
       * everything and sets the normal channel priorities for Dreamcast */
      break;

    /******** Interrupt priority control ********/
    case MMIO::IPRA_bits::address:
      m_mmio.IPRA.raw = value;

      m_interrupt_table[Interrupt::TUNI0].priority = (value >> 12) & 0xf;
      m_interrupt_table[Interrupt::TUNI1].priority = (value >> 8) & 0xf;
      m_interrupt_table[Interrupt::TUNI2].priority = (value >> 4) & 0xf;
      break;

    case MMIO::IPRB_bits::address:
      m_mmio.IPRB.raw = value;
      break;

    case MMIO::IPRC_bits::address:
      m_mmio.IPRC.raw = value;

      /* All the DMAC interrupts share the same priority */
      m_interrupt_table[Interrupt::DMTE0].priority = (value >> 8) & 0xf;
      m_interrupt_table[Interrupt::DMTE1].priority = (value >> 8) & 0xf;
      m_interrupt_table[Interrupt::DMTE2].priority = (value >> 8) & 0xf;
      m_interrupt_table[Interrupt::DMTE3].priority = (value >> 8) & 0xf;
      break;

    /******** Timer registers ********/
    case MMIO::TOCR_bits::address: {
      m_mmio.TOCR.raw = value;
      break;
    }

    case MMIO::TSTR_bits::address: {
      handle_tstr_write(value);
      break;
    }

    case MMIO::TCNT_bits::address + (MMIO::TCNT_bits::stride * 0u):
    case MMIO::TCNT_bits::address + (MMIO::TCNT_bits::stride * 1u):
    case MMIO::TCNT_bits::address + (MMIO::TCNT_bits::stride * 2u): {
      const u32 index = (address - MMIO::TCNT_bits::address) / MMIO::TCNT_bits::stride;
      handle_tcnt_write(index, value);
      break;
    }

    case MMIO::TCOR_bits::address + (MMIO::TCOR_bits::stride * 0u):
    case MMIO::TCOR_bits::address + (MMIO::TCOR_bits::stride * 1u):
    case MMIO::TCOR_bits::address + (MMIO::TCOR_bits::stride * 2u): {
      const u32 index = (address - MMIO::TCOR_bits::address) / MMIO::TCOR_bits::stride;
      DEBUG("TCOR write index={} value=0x{:08x}\n", index, value);
      m_mmio.TCOR[index].raw = value;
      break;
    }

    case MMIO::TCR_bits::address + (MMIO::TCR_bits::stride * 0u):
    case MMIO::TCR_bits::address + (MMIO::TCR_bits::stride * 1u):
    case MMIO::TCR_bits::address + (MMIO::TCR_bits::stride * 2u): {
      const u32 index = (address - MMIO::TCR_bits::address) / MMIO::TCR_bits::stride;
      handle_tcr_write(index, value);
      break;
    }

    /******** Bus State Controller ********/
    case MMIO::PCTRA_bits::address:
      m_mmio.PCTRA.raw = value;
      break;

    case MMIO::BCR1_bits::address:
    case MMIO::BCR2_bits::address:
    case MMIO::WCR1_bits::address:
    case MMIO::WCR2_bits::address:
    case MMIO::WCR3_bits::address:
      /* Don't appear to be important for emulation */
      break;

    /******** Memory Controller ********/
    case MMIO::MCR_bits::address:
      /* Doesn't appear to be important for emulation */
      break;

    case 0xff80001c:
      /* RTCSR Refresh timing and control */
      break;

    case 0xff800024:
      /* RTCSR Refresh counter */
      break;

    case 0xff800028:
      /* RFCR Refresh count */
      break;

    case 0xff800018:
      /* PCMCIA control */
      break;

    case 0xff800030:
      /* PDTRA */
      break;

    case 0xff800040:
      /* PCTRB */
      break;

    case 0xff800044:
      /* PDTRB */
      break;

    case 0xff800048:
      /* GPIO Interrupt control */
      break;

    default:
      DEBUG("Write to unimplemented CPU MMIO address=0x{:08x} value=0x{:08x}\n",
            address,
            value);
      logger.warn(
        "Write to unimplemented CPU MMIO address=%08x value=%08x", address, value);

      // throw std::runtime_error("Unhandled write to CPU MMIO");
  }
}

std::pair<SH4::AddressType, u32>
SH4::mem_region(u32 address, bool is_supervisor) const
{
  if (!is_supervisor) {
    /* TODO */
    return std::make_pair(AddressType::AccessViolation, 0u);
  }

  /* CPU memory mapped registers */
  if (address >= 0xFF000000u) {
    return std::make_pair(AddressType::Register, address);
  }

  /* P0 / U0 Area: Cached if CCR is set, TLB used if enabled */
  if (address <= 0x7c00'0000u) {
    /* Physical RAM */
    return std::make_pair(AddressType::Physical, address & 0x1FFFFFFFu);
  } else if (address <= 0x8000'0000) {
    /* Operand Cache acting as RAM */
    if (m_mmio.CCR.ORA) {
      return std::make_pair(AddressType::OperandCache, address);
    } else {
      throw std::runtime_error("Access to Operand Cache area but CCR.ORA=0");
    }
  } else {
    /*
     * P1 Area: Cached if CCR is set, no TLB, supervisor only
     * P2 Area: Not cacheable, no TLB, supervisor only
     */
    if (address >= 0x80000000u && address < 0xC0000000u) {
      return std::make_pair(AddressType::Physical, address & 0x1FFFFFFFu);
    } else {
      /* P3 Area: Cacheable, TLB used if enabled, supervisor only */
      if (address >= 0xC0000000u && address < 0xE0000000u) {
        return std::make_pair(AddressType::Virtual, address);
      } else {
        /* P4 Store Queue: Not cacheable, TLB used if enabled, supervisor */
        if (address >= 0xE0000000u && address < 0xE4000000u) {
          if (m_mmio.MMUCR.AT) {
            logger.error("Wrote to store queue with AT bit, not supported!");
          }

          if ((address & 0x20) == 0u) {
            /* SQ0 specification */
            return std::make_pair(AddressType::StoreQueue, (address & 0x3fu));
          } else {
            /* SQ1 specification */
            return std::make_pair(AddressType::StoreQueue, (address & 0x3fu));
          }
        } else {
          /* P4 Area: Not cacheable, mostly no TLB, supervisor only */
          if (address >= 0xE0000000u && address <= 0xFFFFFFFFu) {
            return std::make_pair(AddressType::Register, address);
          }
        }
      }
    }
  }

  /* Address not yet handled */
  throw std::runtime_error(std::string("Access to unimplemented CPU memory region: ") +
                           hex_format(address).str());
}

void
SH4::handle_interrupt(const unsigned id)
{
  if (regs.SR.RB != 1u) {
    gpr_swap_bank();
  }

  m_pending_interrupts &= ~(1u << id);

  m_mmio.INTEVT.raw = m_interrupt_table[id].evt;
  regs.SSR = regs.SR;
  regs.SPC = regs.PC;
  regs.SGR = regs.general_registers[15];
  regs.SR.BL = 1u;
  regs.SR.MD = 1u;
  regs.SR.RB = 1u;
  regs.PC = regs.VBR + 0x600u;
}

bool
SH4::execute_dmac(unsigned channel, const u32 external_target, const u32 length)
{
  assert(channel < 4u);

  if (!m_mmio.CHCRn[channel].DE) {
    logger.warn("DMAC execute request to channel %u, which is disabled", channel);
    return false;
  }

  const uint32_t transfer_size = 32u;
  if (m_mmio.CHCRn[channel].TS != 4u) {
    /* TODO */
    logger.error("DMAC execute request with unit != 32 bytes (TS=%u)",
                 m_mmio.CHCRn[channel].TS);
    return false;
  }

  if (m_mmio.CHCRn[channel].RS != 2u) {
    /* TODO What are the other resources we need to support? */
    logger.error("DMAC execute request with unsupported resource %u",
                 m_mmio.CHCRn[channel].TS);
    return false;
  }

  if ((m_mmio.DMATCRn[channel].raw * transfer_size) != length) {
    logger.warn("DMAC execute request with non-matching transfer sizes! %u != %u",
                m_mmio.DMATCRn[channel].raw * transfer_size,
                length);
  }

  logger.debug("SH4 Execute DMAC Channel=%u Target=0x%08x Source=0x%08x ReqLength=%u",
               channel,
               external_target,
               m_mmio.SARn[channel].raw,
               length);

  const auto source_region = mem_region(m_mmio.SARn[channel].raw, true);
  if (source_region.first != AddressType::Physical) {
    /* XXX Assumes that we don't cross into a new region */
    printf("!!!! DMAC execute request to non-physical RAM region (0x%08x)",
           m_mmio.SARn[channel].raw);
    return false;
  }

  u32 src_addr = source_region.second;
  u32 dst_addr = external_target;

  uint8_t buffer[32];
  bool error = false;
  const size_t transfer_count = length / transfer_size;

  for (unsigned i = 0u; i < transfer_count; ++i) {
    m_phys_mem->dma_read(buffer, src_addr, transfer_size);
    m_phys_mem->dma_write(dst_addr, buffer, transfer_size);

    switch (m_mmio.CHCRn[channel].SM) {
        /* clang-format off */
      case 0: /* no increment */ break;
      case 1: src_addr += transfer_size; m_mmio.SARn[channel].raw += transfer_size; break;
      case 2: src_addr -= transfer_size; m_mmio.SARn[channel].raw -= transfer_size; break;
      case 3: throw std::runtime_error("DMAC SM=3 illegal"); break;
        /* clang-format on */
    }

    if (channel == 2) {
      /* Channel 2 on dreamcast is exclusively DDT/external-to-external transfer
       * which uses increment destination mode HACK: Software is supposed to be
       * setting this. If DM!=1 then we have all kinds of problems. */
      m_mmio.CHCRn[channel].DM = 1;
    }

    switch (m_mmio.CHCRn[channel].DM) {
        /* clang-format off */
      case 0: /* no increment */ break;
      case 1: dst_addr += transfer_size; m_mmio.DARn[channel].raw += transfer_size; break;
      case 2: dst_addr -= transfer_size; m_mmio.DARn[channel].raw -= transfer_size; break;
      case 3: throw std::runtime_error("DMAC DM=3 illegal"); break;
        /* clang-format on */
    }
  }

  /* Transfer end - update error and remaining counts */
  m_mmio.CHCRn[channel].TE    = error ? 0u : 1u;
  m_mmio.DMATCRn[channel].raw = 0;

  /* If requested, raise the DMTEn interrupt within SH4 */
  if (m_mmio.CHCRn[channel].IE)
    m_pending_interrupts |= 1u << (Interrupt::DMTE0 + channel);

  return true;
}

void
SH4::debug_enable(const bool enable)
{
  m_debug_mode = enable;
}

void
SH4::debug_breakpoint_add(u32 address)
{
  address = address & 0x1FFFFFFEu;
  if (m_debug_breakpoints.count(address) == 0) {
    m_debug_breakpoints.insert(address);
  }
}

void
SH4::debug_breakpoint_remove(u32 address)
{
  address = address & 0x1FFFFFFEu;
  if (m_debug_breakpoints.count(address) > 0) {
    m_debug_breakpoints.erase(m_debug_breakpoints.find(address));
  }
}

void
SH4::debug_breakpoint_list(std::vector<u32> *const out_result)
{
  for (const u32 entry : m_debug_breakpoints) {
    out_result->push_back(entry);
  }
}

void
SH4::debug_watchpoint_add(const u32 address, const zoo::WatchpointOperation op)
{
  if (op == zoo::WatchpointOperation::Read) {
    m_debug_read_watchpoints.insert(address);
  } else if (op == zoo::WatchpointOperation::Write) {
    m_debug_write_watchpoints.insert(address);
  } else {
    assert(0);
  }
}

void
SH4::debug_watchpoint_remove(const u32 address, const zoo::WatchpointOperation op)
{
  if (op == zoo::WatchpointOperation::Read) {
    m_debug_read_watchpoints.erase(address);
  } else if (op == zoo::WatchpointOperation::Write) {
    m_debug_write_watchpoints.erase(address);
  } else {
    assert(0);
  }
}

bool
SH4::debug_watchpoint_check(const u32 address, const zoo::WatchpointOperation op)
{
  if (op == zoo::WatchpointOperation::Read) {
    return m_debug_read_watchpoints.count(address) > 0;
  } else if (op == zoo::WatchpointOperation::Write) {
    return m_debug_write_watchpoints.count(address) > 0;
  } else {
    _check(false, "Invalid watch operation");
  }
}

void
SH4::debug_mask_interrupts(const bool masked)
{
  m_debug_mask_interrupts = masked;
}

bool
SH4::is_debug_enabled() const
{
  return m_debug_mode;
}

void
SH4::power_on_reset()
{
  regs.clear();
  regs.PC = 0xA0000000u;
  m_branch_target = 0xFFFFFFFFu;
  m_pending_interrupts = 0u;

  regs.SR.raw = 0x00000000u;
  regs.SR.MD = 1u;
  regs.SR.RB = 1u;
  regs.SR.BL = 1u;
  regs.SR.IMASK = 0xf;

  regs.general_registers[15] = 0x8c00f400;

  m_mmio.TOCR.raw = 1;
  m_mmio.TSTR.raw = 0;
  for (unsigned ch = 0; ch < MMIO::NUM_TMU_CHANNELS; ++ch) {
    m_mmio.TCNT[ch].raw = 0xFFFFFFFF;
    m_mmio.TCOR[ch].raw = 0xFFFFFFFF;
  }

  regs.VBR = 0x00000000u;
  memset(&FPU, 0, sizeof(FPU));
  FPU.FPSCR.raw = 0x00040001u;

  m_mmio = { { 0 } };
  m_mmio.INTEVT.raw = 0x00000000u;
  m_mmio.EXPEVT.raw = 0x00000000u;
  m_mmio.MMUCR.raw = 0x00000000u;
  m_mmio.QACR0.raw = 0x00000000u;
  m_mmio.QACR1.raw = 0x00000000u;
  m_mmio.PTEH.raw = 0x00000000u;
  m_mmio.PTEL.raw = 0x00000000u;
  m_mmio.TTB.raw = 0x00000000u;
  m_mmio.TEA.raw = 0x00000000u;
  m_mmio.TRA.raw = 0x00000000u;
  m_mmio.CCR.raw = 0x00000000u;
  m_mmio.RAMCR.raw = 0x00000000u;
  m_mmio.PDTRA.raw = 0x00000000u;
  m_mmio.IPRA.raw = 0x00000000u;
  m_mmio.IPRB.raw = 0x00000000u;
  m_mmio.IPRC.raw = 0x00000000u;
  memset(SQ, 0u, sizeof(SQ));

  m_tmu_event.cancel();
  m_console->schedule_event(kNanosPerTmuUpdate, &m_tmu_event);
}

std::ostream &
operator<<(std::ostream &out, const SH4::Registers::Status &sr)
{
  if (sr.MD) {
    out << "(MD)";
  }

  if (sr.RB) {
    out << "(RB)";
  }

  if (sr.BL) {
    out << "(BL)";
  }

  if (sr.FD) {
    out << "(FD)";
  }

  if (sr.M) {
    out << "(M)";
  }

  if (sr.Q) {
    out << "(Q)";
  }

  if (sr.IMASK) {
    out << "(IMASK)";
  }

  if (sr.S) {
    out << "(S)";
  }

  if (sr.T) {
    out << "(T)";
  }

  return out;
}

void
SH4::Registers::clear()
{
  memset(this, 0u, sizeof(*this));
}

void
SH4::FPUState::write(std::ostream &out) const
{
  out << "FPSCR: " << hex_format(FPSCR.raw) << std::endl << std::endl;

  out << "                0         1         2         3";
  out << "         4         5         6         7" << std::endl;

  /* Bank 1 */
  out << "Bank 1:" << std::endl;

  out << "FR0-7: ";
  for (unsigned i = 0u; i < 8u; ++i) {
    out << "  " << format_string("%8.3f", banks[0].sp[i]);
  }
  out << std::endl;

  out << "DR0-3: ";
  for (unsigned i = 0u; i < 4u; ++i) {
    out << "  " << format_string("%8.3lf", banks[0].dp[i]) << "          ";
  }
  out << std::endl;

  out << "FR7-15:";
  for (unsigned i = 0u; i < 8u; ++i) {
    out << "  " << format_string("%8.3f", banks[0].sp[i + 8u]);
  }
  out << std::endl;

  out << "DR4-7: ";
  for (unsigned i = 0u; i < 4u; ++i) {
    out << "  " << format_string("%8.3lf", banks[0].dp[i + 4u]) << "          ";
  }
  out << std::endl << std::endl;

  /* Bank 2 */
  out << "Bank 2:" << std::endl;

  out << "FR0-7: ";
  for (unsigned i = 0u; i < 8u; ++i) {
    out << "  " << format_string("%8.3f", banks[1].sp[i]);
  }
  out << std::endl;

  out << "DR0-3: ";
  for (unsigned i = 0u; i < 4u; ++i) {
    out << "  " << format_string("%8.3lf", banks[1].dp[i]) << "          ";
  }
  out << std::endl;

  out << "FR7-15:";
  for (unsigned i = 0u; i < 8u; ++i) {
    out << "  " << format_string("%8.3f", banks[1].sp[i + 8u]);
  }
  out << std::endl;

  out << "DR4-7: ";
  for (unsigned i = 0u; i < 4u; ++i) {
    out << "  " << format_string("%8.3lf", banks[1].dp[i + 4u]) << "          ";
  }
  out << std::endl << std::endl;
}

void
SH4::Registers::write(std::ostream &out) const
{
  out << "       SR:" << SR << "           SSR:" << SSR
      << "           PC: " << hex_format(PC) << "      PR: " << hex_format(PR)
      << std::endl;

  out << "      GBR:" << hex_format(GBR) << " ";
  out << "     VBR:" << hex_format(VBR) << " ";
  out << "    MACH:" << hex_format(MACH) << " ";
  out << "    MACL:" << hex_format(MACL) << std::endl;

  out << "Bank0 0-7 ";
  for (auto i = 0; i < 8; ++i)
    out << hex_format(general_registers[i]) << " ";
  out << std::endl;

  out << "Bank1 0-7 ";
  for (auto i = 16; i < 24; ++i)
    out << hex_format(general_registers[i]) << " ";
  out << std::endl;

  out << "GPR  8-15 ";
  for (auto i = 8; i < 16; ++i)
    out << hex_format(general_registers[i]) << " ";
  out << std::endl;
}

/* Instantiation for the only allowed CPU memory access types */
template void SH4::mem_write(u32, u8);
template void SH4::mem_write(u32, u16);
template void SH4::mem_write(u32, u32);
template void SH4::mem_write(u32, u64);

template u8 SH4::mem_read(u32);
template u16 SH4::mem_read(u32);
template u32 SH4::mem_read(u32);
template u64 SH4::mem_read(u32);

void
SH4::serialize(serialization::Snapshot &snapshot)
{
  static_assert(sizeof(regs) == 144);
  snapshot.add_range("sh4.regs", sizeof(regs), &regs);

  static_assert(sizeof(FPU) == 136);
  snapshot.add_range("sh4.FPU", sizeof(FPU), &FPU);

  static_assert(sizeof(m_mmio) == 200);
  snapshot.add_range("sh4.mmio", sizeof(m_mmio), &m_mmio);

  static_assert(sizeof(m_executed_branch) == 4);
  snapshot.add_range(
    "sh4.executed_branch", sizeof(m_executed_branch), &m_executed_branch);

  static_assert(sizeof(m_branch_target) == 4);
  snapshot.add_range("sh4.branch_target", sizeof(m_branch_target), &m_branch_target);

  static_assert(sizeof(m_execution_mode) == 4);
  snapshot.add_range("sh4.execution_mode", sizeof(m_execution_mode), &m_execution_mode);

  static_assert(sizeof(m_operand_cache) == 8*1024);
  snapshot.add_range("sh4.operand_cache", sizeof(m_operand_cache), m_operand_cache);

  static_assert(sizeof(SQ) == 64);
  snapshot.add_range("sh4.store_queue", sizeof(SQ), SQ);

  /* TODO save breakpoint data / mode */

  const u32 pending_interrupts_val = m_pending_interrupts.load();
  static_assert(sizeof(pending_interrupts_val) == 4);
  snapshot.add_range(
    "sh4.pending_interrupts", sizeof(pending_interrupts_val), &pending_interrupts_val);

  m_tmu_event.serialize(snapshot);
}

void
SH4::deserialize(const serialization::Snapshot &snapshot)
{
  logger.info("Deserializing...");
  m_jit_cache->invalidate_all();
  m_last_block = nullptr;

  snapshot.apply_all_ranges("sh4.regs", &regs);
  snapshot.apply_all_ranges("sh4.FPU", &FPU);
  snapshot.apply_all_ranges("sh4.mmio", &m_mmio);
  snapshot.apply_all_ranges("sh4.executed_branch", &m_executed_branch);
  snapshot.apply_all_ranges("sh4.branch_target", &m_branch_target);
  snapshot.apply_all_ranges("sh4.execution_mode", &m_execution_mode);
  snapshot.apply_all_ranges("sh4.operand_cache", &m_operand_cache);
  snapshot.apply_all_ranges("sh4.store_queue", SQ);

  /* TODO load breakpoint data / mode */

  uint pending_interrupts_val;
  snapshot.apply_all_ranges("sh4.pending_interrupts", &pending_interrupts_val);
  m_pending_interrupts = pending_interrupts_val;

  m_tmu_event.deserialize(snapshot);
}

void
SH4::handle_tstr_write(u8 value)
{
  /* TSTR update. Potentially start/stop counters. */
  m_mmio.TSTR.raw = value & MMIO::TSTR_bits::mask;
  logger.verbose("Write TSTR < 0x{:02x}\n", value);
}

void
SH4::tick_tmu_channels()
{
  for (u32 ch = 0; ch < MMIO::NUM_TMU_CHANNELS; ++ch) {
    const bool is_running = m_mmio.TSTR.raw & (1 << ch);
    if (!is_running) {
      continue;
    }

    /* The bottom three bits of FRQCR set the peripheral clock control a
     * divider for the CPU main clock. */
    // const u64 FRQCR_PFC                 = 4; // TODO
    // const u64 cpu_cycles_per_tmu_update = kNanosPerTmuUpdate / kNanosPerCycle;
    // const u64 p_cycles_per_tmu_update   = cpu_cycles_per_tmu_update / FRQCR_PFC;

    /* On the dreamcast the peripheral clock is 50Mhz. FRQCR.PFC = 010 is the
     * appropriate value according to the docs. This means that the peripheral
     * clock is 50Mhz / 4 = 12.5Mhz which is 80ns per cycle. */
    const u64 p_cycle_nanos           = 20; // ch == 0 ? 20 : 60;
    const u64 p_cycles_per_tmu_update = kNanosPerTmuUpdate / p_cycle_nanos;

    /*
     * Notes:
     *   - The dreamcast architecture doc says that the peripheral clock is
     *     50Mhz, but the SH4 manual says that it's a divider on the CPU clock?
     *   - Re-volt will only display its opening logo/message if this timing is
     *     not too small and not too large.
     */

    /* Pre-scaler (TCRn.TPSC bits) divides the peripheral clock by a configured
     * amount. */
    static const u64 clock_dividers[8] = { 4, 16, 64, 256, 1024, 1024, 1024, 1024 };
    const unsigned prescaler_index = m_mmio.TCR[ch].TPSC;
    const u32 tcnt_delta = p_cycles_per_tmu_update / clock_dividers[prescaler_index];

    if (tcnt_delta > m_mmio.TCNT[ch].raw) {
      m_mmio.TCR[ch].UNF = 1;
      m_mmio.TCNT[ch].raw = m_mmio.TCOR[ch].raw;
      if (m_mmio.TCR[ch].UNF && m_mmio.TCR[ch].UNIE) {
        m_pending_interrupts |= 1u << (Interrupt::TUNI0 + ch);
      }
    } else {
      m_mmio.TCNT[ch].raw -= tcnt_delta;
    }
  }

  m_console->schedule_event(kNanosPerTmuUpdate, &m_tmu_event);
}

void
SH4::handle_tcnt_write(const unsigned id, const u32 value)
{
  assert(id < MMIO::NUM_TMU_CHANNELS);
  if (id == 2) {
    DEBUG("Write to TCNT2: 0x%08x value=0x%08x PC=0x%08x\n",
          m_mmio.TCNT[id].raw,
          value,
          regs.PC);
  }
  m_mmio.TCNT[id].raw = value;
}

void
SH4::handle_tcr_write(const unsigned id, const u16 value)
{
  assert(id < MMIO::NUM_TMU_CHANNELS);
  m_mmio.TCR[id].raw = value;
}

std::unordered_map<u32, std::string> sh4pc_to_string;

std::string &
get_string_for_sh4pc(const u32 pc)
{
  const auto it = sh4pc_to_string.find(pc);
  if (it != sh4pc_to_string.end()) {
    return it->second;
  }

  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%08x", pc);
  sh4pc_to_string.insert({ pc, std::string(buffer) });
  return sh4pc_to_string.at(pc);
}

#ifdef TRACY_ENABLE
std::vector<TracyCZoneCtx> tracy_zone_stack;
std::unordered_map<u32, u64> tracy_srcloc_by_sh4_pc;

u64
get_sh4pc_to_tracy_srcloc(const u32 sh4_pc)
{
  const auto it = tracy_srcloc_by_sh4_pc.find(sh4_pc);
  if (it != tracy_srcloc_by_sh4_pc.end()) {
    return it->second;
  }

  const char *buffer = get_string_for_sh4pc(sh4_pc).c_str();
  const u64 srcloc   = ___tracy_alloc_srcloc(0, buffer, 8, buffer, 8);
  tracy_srcloc_by_sh4_pc.insert({ sh4_pc, srcloc });
  return srcloc;
}
#endif

void
SH4::push_call_address(u32 new_address)
{
  std::lock_guard lock(m_call_stack_mutex);

#if 0
  TracyFiberEnter("SH4");
  const auto &tracy_srcloc = tracy_srcloc_by_sh4_pc.at(new_address);
  auto zone = ___tracy_emit_zone_begin_alloc(tracy_srcloc, true);
  tracy_zone_stack.push_back(zone);
  TracyFiberLeave;
#endif

  if (m_call_stack.size() < 256) {
    m_call_stack.push_back(new_address);
  }
}

void
SH4::pop_call_address()
{
  std::lock_guard lock(m_call_stack_mutex);

#if 0
  TracyFiberEnter("SH4");
  auto zone = tracy_zone_stack.back();
  TracyCZoneEnd(zone);
  tracy_zone_stack.pop_back();
  TracyFiberLeave;
#endif

  if (!m_call_stack.empty()) {
    m_call_stack.pop_back();
  }
}

void
SH4::copy_call_stack(std::vector<u32> &output)
{
  std::lock_guard lock(m_call_stack_mutex);
  output.resize(m_call_stack.size());
  for (u32 i = 0; i < m_call_stack.size(); ++i) {
    output[i] = m_call_stack[i];
  }
}

void
SH4::handle_sampling_profiler_tick()
{
  std::lock_guard lock(m_call_stack_mutex);

#ifdef TRACY_ENABLE
  static const char *fiber_name = "SH4 (sampled, 5us guest)";
  static std::vector<u32> previous_tracy_zone_stack;
  static std::vector<TracyCZoneCtx> local_tracy_zone_stack;

  /* Is the call stack now smaller than it was before? */
  if (m_call_stack.size() < previous_tracy_zone_stack.size()) {
    const u32 pop_count = previous_tracy_zone_stack.size() - m_call_stack.size();
    for (u32 i = 0; i < pop_count; ++i) {
      ProfilePopFiberZone(fiber_name, local_tracy_zone_stack.back());
      local_tracy_zone_stack.pop_back();
    }
  }

  /* Or, is the stack larger than it was last time? */
  else if (m_call_stack.size() > previous_tracy_zone_stack.size()) {
    for (u32 i = previous_tracy_zone_stack.size(); i < m_call_stack.size(); ++i) {
      const u32 sh4_pc = m_call_stack[i];
      const u64 srcloc = get_sh4pc_to_tracy_srcloc(sh4_pc);

      TracyFiberEnter(fiber_name);
      auto zone = ___tracy_emit_zone_begin_alloc(srcloc, true);
      local_tracy_zone_stack.push_back(zone);
      TracyFiberLeave;
    }
  }

  previous_tracy_zone_stack = m_call_stack;
#endif

  m_console->schedule_event(5 * 1000, &m_sampling_profiler);
}

}
