#include <fmt/core.h>
#include "shared/profiling.h"
#include "systems/ps1/hw/dma.h"
#include "systems/ps1/hw/disc.h"

namespace zoo::ps1 {

// https://psx-spx.consoledev.net/dmachannels/

// 1F80108xh DMA0 channel 0  MDECin  (RAM to MDEC)
// 1F80109xh DMA1 channel 1  MDECout (MDEC to RAM)
// 1F8010Axh DMA2 channel 2  GPU (lists + image data)
// 1F8010Bxh DMA3 channel 3  CDROM   (CDROM to RAM)
// 1F8010Cxh DMA4 channel 4  SPU
// 1F8010Dxh DMA5 channel 5  PIO (Expansion Port)
// 1F8010Exh DMA6 channel 6  OTC (reverse clear OT) (GPU related)
// 1F8010F0h DPCR - DMA Control register
// 1F8010F4h DICR - DMA Interrupt register

DMA::DMA(Console *console) : m_console(console)
{
  reset();

  auto reg = m_console->mmio_registry();
  reg->setup("DMA", "DPCR", &m_regs.DPCR);
  reg->setup("DMA", "DICR", &m_regs.DPCR);

  reg->setup("DMA", "MADR[MDEC-in]", &m_regs.channels[0].MADR);
  reg->setup("DMA", "CHCR[MDEC-in]", &m_regs.channels[0].CHCR);
  reg->setup("DMA", "BCR[MDEC-in]", &m_regs.channels[0].BCR);

  reg->setup("DMA", "MADR[MDEC-out]", &m_regs.channels[1].MADR);
  reg->setup("DMA", "CHCR[MDEC-out]", &m_regs.channels[1].CHCR);
  reg->setup("DMA", "BCR[MDEC-out]", &m_regs.channels[1].BCR);

  reg->setup("DMA", "MADR[GPU]", &m_regs.channels[2].MADR);
  reg->setup("DMA", "CHCR[GPU]", &m_regs.channels[2].CHCR);
  reg->setup("DMA", "BCR[GPU]", &m_regs.channels[2].BCR);

  reg->setup("DMA", "MADR[CDROM]", &m_regs.channels[3].MADR);
  reg->setup("DMA", "CHCR[CDROM]", &m_regs.channels[3].CHCR);
  reg->setup("DMA", "BCR[CDROM]", &m_regs.channels[3].BCR);

  reg->setup("DMA", "MADR[SPU]", &m_regs.channels[4].MADR);
  reg->setup("DMA", "CHCR[SPU]", &m_regs.channels[4].CHCR);
  reg->setup("DMA", "BCR[SPU]", &m_regs.channels[4].BCR);

  reg->setup("DMA", "MADR[PIO]", &m_regs.channels[5].MADR);
  reg->setup("DMA", "CHCR[PIO]", &m_regs.channels[5].CHCR);
  reg->setup("DMA", "BCR[PIO]", &m_regs.channels[5].BCR);

  reg->setup("DMA", "MADR[OTC]", &m_regs.channels[6].MADR);
  reg->setup("DMA", "CHCR[OTC]", &m_regs.channels[6].CHCR);
  reg->setup("DMA", "BCR[OTC]", &m_regs.channels[6].BCR);
}

void
DMA::reset()
{
  memset(&m_regs, 0, sizeof(m_regs));
  m_regs.DPCR.raw = 0x0765'4321;
}

u32
DMA::read_u32(u32 addr)
{
  ProfileZone;
  // printf("DMA read32 @ 0x%08x\n", addr);

  // Channel registers
  if (addr < 0x1f80'10f0) {
    // 0x1f80108x is first channel, 9x is second, ...
    const u32 chan = ((addr >> 4) & 0xF) - 8;

    switch (addr & 0xf) {
      case 0:
        return m_regs.channels[chan].MADR.raw;
      case 4:
        return m_regs.channels[chan].BCR.raw;
      case 8:
        printf("CHCR[%u] = 0x%08x\n", chan, m_regs.channels[chan].CHCR.raw);
        return m_regs.channels[chan].CHCR.raw;
      default:
        throw std::runtime_error("Unhandled DMA read");
    }
  }

  // Other
  switch (addr) {
    // DMA Control register
    case 0x1f80'10f0:
      return m_regs.DPCR.raw;

    // DMA Interrupt register
    case 0x1f80'10f4: {
      // Calculate master IRQ flag (which is read-only/computed from other states)
      m_regs.DICR.irq_master_flag = calc_master_flag();
      return m_regs.DICR.raw;
    }

    case 0x1f80'10f6: {
      // MDEC decoders seem to do a 32bit read to this, unaligned. Assuming this is the
      // correct behavior
      return read_u32(0x1f80'10f4) >> 16;
    }

    // Unknown
    case 0x1f80'10f8:
      return m_regs.unknown_1f8010f8;
    case 0x1f80'10fc:
      return m_regs.unknown_1f8010fc;
    default:
      throw std::runtime_error("DMA unhandled read_u32");
      break;
  }
}

u32
DMA::calc_master_flag()
{
  const u32 active_irqs = m_regs.DICR.irq_en & m_regs.DICR.irq_flags;
  return m_regs.DICR.irq_force || (m_regs.DICR.irq_master_en && (active_irqs != 0));
}

void
DMA::write_u32(u32 addr, u32 value)
{
  ProfileZone;
  // printf("DMA: write_u32(0x%08x) < 0x%08x\n", addr, value);

  // Channel registers
  if (addr < 0x1f80'10f0) {
    // 0x1f80108x is first channel, 9x is second, ...
    const u32 chan = ((addr >> 4) & 0xF) - 8;

    switch (addr & 0xf) {
      case 0:
        m_regs.channels[chan].MADR.raw = value & 0x00ff'ffff;
        // printf(" - DMA[%u] Base Address Write <- 0x%08x\n",
        //        chan,
        //        m_regs.channels[chan].MADR.raw);
        break;
      case 4:
        m_regs.channels[chan].BCR.raw = value;
        // printf(" - DMA[%u] Block Control Write <- 0x%08x\n",
        //        chan,
        //        m_regs.channels[chan].BCR.raw);
        break;
      case 8: {
        m_regs.channels[chan].CHCR.raw = value;
        const auto chcr = m_regs.channels[chan].CHCR;
        // printf(" - DMA[%u] Channel Control Write <- 0x%08x\n", chan, value);
        // printf("   - direction=%u sync_mode=%u trigger=%u start_busy=%u\n",
        //        chcr.direction,
        //        chcr.sync_mode,
        //        chcr.start_trigger,
        //        chcr.start_busy);

        if (chcr.start_busy && (chcr.sync_mode == TransferMode::BlockCopy)) {
          dma_block_copy(chan);
        } else if (chcr.start_busy && (chcr.sync_mode == TransferMode::LinkedList)) {
          dma_linked_list(chan);
        } else if (chcr.start_trigger && (chcr.sync_mode == TransferMode::AllAtOnce)) {
          dma_all_at_once(chan);
        }
        break;
      }
      default:
        throw std::runtime_error("Unhandled DMA read");
    }

    return;
  }

  // Others
  switch (addr) {
    // DMA Control register
    case 0x1f80'10f0:
      m_regs.DPCR.raw = value;
      printf("dma: updated dma control reg...\n");
      // clang-format off
      printf("  - DMA0 pri=%u en=%u...\n", m_regs.DPCR.dma0_pri, m_regs.DPCR.dma0_en);
      printf("  - DMA1 pri=%u en=%u...\n", m_regs.DPCR.dma1_pri, m_regs.DPCR.dma1_en);
      printf("  - DMA2 pri=%u en=%u...\n", m_regs.DPCR.dma2_pri, m_regs.DPCR.dma2_en);
      printf("  - DMA3 pri=%u en=%u...\n", m_regs.DPCR.dma3_pri, m_regs.DPCR.dma3_en);
      printf("  - DMA4 pri=%u en=%u...\n", m_regs.DPCR.dma4_pri, m_regs.DPCR.dma4_en);
      printf("  - DMA5 pri=%u en=%u...\n", m_regs.DPCR.dma5_pri, m_regs.DPCR.dma5_en);
      printf("  - DMA6 pri=%u en=%u...\n", m_regs.DPCR.dma6_pri, m_regs.DPCR.dma6_en);
      // clang-format on
      break;

    // DICR : DMA Interrupt register
    case 0x1f80'10f4: {
      // XXX : set irq flags on dma completion (b24+), and only if b16+ enabled
      // Bit31 is read-only. We allow overwrite since we re-compute it whenever its read.
      m_regs.DICR.raw = value;

      // Writing 1 to a flag acknowledges
      DMARegisters::DICR_Bits ack_bits;
      ack_bits.raw = value;

      m_regs.DICR.irq_flags &= ~ack_bits.irq_flags;
      break;
    }

    case 0x1f80'10f6: {
      // xxx : MDEC does unaligned 32bit reads/writes to this address. Kind of guessing on
      // behavior.
      m_regs.DICR.raw &= 0xffff;
      m_regs.DICR.raw |= ((value & 0xffff) << 16);
      break;
    };

    // Unknown
    case 0x1f80'10f8:
      m_regs.unknown_1f8010f8 = value;
      break;
    case 0x1f80'10fc:
      m_regs.unknown_1f8010fc = value;
      break;
    default:
      assert(false);
      break;
  }
}

void
DMA::dma_all_at_once(u32 chan)
{
  ProfileZone;
  printf("dma: all-at-once starting -- chan=%u base=0x%08x nWords=0x%x\n",
         chan,
         m_regs.channels[chan].MADR.dma_start_address,
         m_regs.channels[chan].BCR.syncmode0_num_words);
  fox::MemoryTable *memory = m_console->memory();

  const u32 transfer_size_words = m_regs.channels[chan].BCR.syncmode0_num_words;
  const i32 dst_address_increment = m_regs.channels[chan].CHCR.address_step == 0 ? 4 : -4;
  u32 dst_address = m_regs.channels[chan].MADR.dma_start_address;

  for (u32 i = transfer_size_words; i > 0; --i) {

    u32 src_word;
    if (chan == DMAChannel::OTC) {
      // For Order-Table Clear (Channel 6), the source word is 0 for every OT entry header
      // word except the last which is 0x00ff'ffff to signal the end of the OT linked list
      const i64 prev_address = i64(dst_address) + i64(dst_address_increment);
      src_word = i == 1 ? 0x00ff'ffff : (prev_address & 0x1fffff);
      assert(m_regs.channels[chan].CHCR.direction == 0);

    } else if (chan == DMAChannel::CDROM) {
      src_word = m_console->cdrom()->read_data_fifo();

    } else {
      throw std::runtime_error(
        fmt::format("Unsupported dma-all-at-once channel {}", chan));
    }

    // printf("dma: debug i=%u dst_address=0x%x src_word=0x%x\n", i, dst_address,
    // src_word);
    memory->dma_write(dst_address, &src_word, sizeof(src_word));

    i64 dst_address_new = i64(dst_address) + i64(dst_address_increment);
    dst_address = u32(dst_address_new);
  }

  dma_completed(chan);

  // Done with the DMA
  m_regs.channels[chan].CHCR.start_trigger = 0;
  m_regs.channels[chan].CHCR.start_busy = 0;
}

void
DMA::dma_block_copy(u32 chan)
{
  ProfileZone;
  printf("dma: block-copy starting -- chan=%u base=0x%08x block_size=%u num_blocks=%u\n",
         chan,
         m_regs.channels[chan].MADR.dma_start_address,
         m_regs.channels[chan].BCR.syncmode1_block_size_in_words,
         m_regs.channels[chan].BCR.syncmode1_num_blocks);

  fox::MemoryTable *memory = m_console->memory();

  // XXX : Below implementations treat the entire thing as a single transfer, but the docs
  // seem to suggest it should be one block at a time with ack from the CPU via
  // flags/interrupt mechanism? For now, following rustation guide

  if (chan == 2 && m_regs.channels[chan].CHCR.direction == 1) {
    const u32 transfer_size_words =
      m_regs.channels[chan].BCR.syncmode1_num_blocks *
      m_regs.channels[chan].BCR.syncmode1_block_size_in_words;
    const i32 address_increment = m_regs.channels[chan].CHCR.address_step == 0 ? 4 : -4;
    u32 addr = m_regs.channels[chan].MADR.dma_start_address;

    for (u32 i = transfer_size_words; i > 0; --i) {
      u32 src_word;
      memory->dma_read(&src_word, addr & 0x1f'fffc, sizeof(src_word));
      m_console->gpu()->gp0(src_word);

      i64 addr_new = i64(addr) + i64(address_increment);
      addr = u32(addr_new);
    }
  } else if (chan == 2 && m_regs.channels[chan].CHCR.direction == 0) {

    const u32 transfer_size_words =
      m_regs.channels[chan].BCR.syncmode1_num_blocks *
      m_regs.channels[chan].BCR.syncmode1_block_size_in_words;

    const i32 address_increment = m_regs.channels[chan].CHCR.address_step == 0 ? 4 : -4;
    u32 addr = m_regs.channels[chan].MADR.dma_start_address;

    // GPUREAD is setup by this point by the software to produce the correct amount of
    // data
    for (u32 i = transfer_size_words; i > 0; --i) {
      u32 word = m_console->gpu()->gpuread();
      memory->write<u32>(addr, word);

      i64 addr_new = i64(addr) + i64(address_increment);
      addr = u32(addr_new);
    }

    printf("dma: completed vram -> cpu\n");
  } else if (chan == 4 && m_regs.channels[chan].CHCR.direction == 1) {
    // Main RAM -> SPU RAM

    const u32 transfer_size_words =
      m_regs.channels[chan].BCR.syncmode1_num_blocks *
      m_regs.channels[chan].BCR.syncmode1_block_size_in_words;

    const i32 address_increment = m_regs.channels[chan].CHCR.address_step == 0 ? 4 : -4;
    u32 addr = m_regs.channels[chan].MADR.dma_start_address;

    for (u32 i = transfer_size_words; i > 0; --i) {
      u32 src_word;
      memory->dma_read(&src_word, addr & 0x1f'fffc, sizeof(src_word));
      m_console->spu()->push_dma_word(src_word);

      i64 addr_new = i64(addr) + i64(address_increment);
      addr = u32(addr_new);
    }

    printf("dma: main ram -> spu ram\n");

  } else if (chan == 0 && m_regs.channels[chan].CHCR.direction == 1) {
    // Main RAM -> MDEC

    const u32 transfer_size_words =
      m_regs.channels[chan].BCR.syncmode1_num_blocks *
      m_regs.channels[chan].BCR.syncmode1_block_size_in_words;

    const i32 address_increment = m_regs.channels[chan].CHCR.address_step == 0 ? 4 : -4;
    u32 addr = m_regs.channels[chan].MADR.dma_start_address;

    for (u32 i = transfer_size_words; i > 0; --i) {
      u32 src_word;
      memory->dma_read(&src_word, addr & 0x1f'fffc, sizeof(src_word));
      m_console->mdec()->handle_command(src_word);

      i64 addr_new = i64(addr) + i64(address_increment);
      addr = u32(addr_new);
    }

    printf("dma: main ram -> mdec-in\n");
  } else if (chan == 1 && m_regs.channels[chan].CHCR.direction == 0) {
    // MDEC-out -> Main RAM

    const u32 transfer_size_words =
      m_regs.channels[chan].BCR.syncmode1_num_blocks *
      m_regs.channels[chan].BCR.syncmode1_block_size_in_words;

    const i32 address_increment = m_regs.channels[chan].CHCR.address_step == 0 ? 4 : -4;
    u32 addr = m_regs.channels[chan].MADR.dma_start_address;

    for (u32 i = transfer_size_words; i > 0; --i) {
      const u32 word = m_console->memory()->read<u32>(0x1f80'1820);
      memory->write<u32>(addr, word);

      i64 addr_new = i64(addr) + i64(address_increment);
      addr = u32(addr_new);
    }

    printf("dma: mdec-out -> main ram\n");
  }

  else {
    assert(false);
  }

  // Done with the DMA
  dma_completed(chan);
  m_regs.channels[chan].CHCR.start_trigger = 0;
  m_regs.channels[chan].CHCR.start_busy = 0;
}

void
DMA::dma_linked_list(u32 chan)
{
  ProfileZone;
  printf("dma: linked-list starting -- chan=%u base=0x%08x\n",
         chan,
         m_regs.channels[chan].MADR.dma_start_address);

  assert(chan == DMAChannel::GPU &&
         "unknown case of linked-list DMA for non-GPU channel");

  fox::MemoryTable *memory = m_console->memory();
  u32 addr = m_regs.channels[chan].MADR.dma_start_address;

  // Keep track how many of these we've done, just as a safety thing.
  u32 i = 0;
  while (true) {

    u32 packet_header;
    memory->dma_read(&packet_header, addr, sizeof(packet_header));

    const u32 packet_data_words = packet_header >> 24;
    const u32 packet_next_addr = packet_header & 0x00ff'ffff;

    if (packet_data_words != 0) {
      // printf("GPU Packet (n_words=%u next_addr=0x%08x)\n",
      //        packet_data_words,
      //        packet_next_addr);
      for (u32 j = 0; j < packet_data_words; ++j) {
        u32 command;
        const u32 read_addr = addr + 4 * (j + 1);
        memory->dma_read(&command, read_addr, sizeof(command));
        // printf(" - GPU Command Word: 0x%08x\n", command);
        m_console->gpu()->gp0(command);
      }
    }

    // Last packet in the linked list?
    if (packet_next_addr == 0xff'ffff) {
      break;
    }

    addr = packet_next_addr & 0x1f'ffff;

    if (i++ > 50000) {
      assert("Hit a huge linked list display list (loop?)");
      break;
    }
  }

  // DMA complete
  dma_completed(chan);
  m_regs.channels[chan].CHCR.start_trigger = 0;
  m_regs.channels[chan].CHCR.start_busy = 0;
}

void
DMA::dma_completed(u32 chan)
{
  m_regs.channels[chan].CHCR.start_busy = false;
  m_regs.channels[chan].CHCR.start_trigger = false;

  // Apparently this flag is only set if it is also currently enabled.
  const bool enabled = (m_regs.DICR.irq_en >> chan) & 1;
  if (enabled) {
    m_regs.DICR.irq_flags |= 1 << chan;
    // if (!master_flag_before && calc_master_flag()) {
    m_console->irq_control()->raise(interrupts::DMA);
    // }
  }
}

void
DMA::register_regions(fox::MemoryTable *memory)
{
  memory->map_mmio(0x1f80'1080, 0x80, "DMA Control MMIO", this);
}

} // namespace zoo::ps1::mmio
