#pragma once

#include "systems/ps1/console.h"
#include "fox/mmio_device.h"
#include "shared/error.h"
#include "shared/types.h"

namespace zoo::ps1 {

// The PS1 has 7 DMA Channels:
// 0) Media Decoder Input
// 1) Media Decoder Output
// 2) GPU
// 3) CDROM
// 4) SPU
// 5) Extension Port
// 6) Connected to RAM, for clearing Ordering Tables

namespace DMAChannel {
enum
{
  MDecIn = 0, /*!< RAM -> MDEC */
  MDecOut,    /*!< MDEC -> RAM */
  GPU,        /*!< GPU (lists, image data, etc.) */
  CDROM,      /*!< CDROM -> RAM */
  SPU,        /*!< SPU */
  PIO,        /*!< Expansion port */
  OTC,        /*!< Reverse-clear Order Table (GPU stuff) */
  NumChannels,
};
};

namespace TransferMode {
enum
{
  AllAtOnce = 0,
  BlockCopy,
  LinkedList,
  Reserved,
};
};

class DMA final : public fox::MMIODevice {
private:
  struct ChannelRegisters {
    /*! Base address */
    union {
      struct {
        u32 dma_start_address : 24;
        u32 _unused : 8;
      };
      u32 raw;
    } MADR;

    /*! Block Control */
    union {
      struct {
        u32 syncmode0_num_words : 16;
        u32 syncmode0_unused : 16;
      };
      struct {
        /*! BS */
        u32 syncmode1_block_size_in_words : 16;
        /*! BA */
        u32 syncmode1_num_blocks : 16;
      };
      struct {
        u32 syncmode2_unused : 32;
      };
      u32 raw;
    } BCR;

    /*! Channel Control */
    union {
      struct {
        /*! Transfer Direction (0=To Main RAM, 1=From Main RAM) */
        u32 direction : 1;
        /*! (0=Forward,+4, 1=Backward,-4) */
        u32 address_step : 1;
        u32 _unused0 : 6;
        /*! (0=Normal, 1=Chopping, run CPU during DMA gaps) */
        u32 chopping_enabled : 1;
        /*! Transfer/Synchronization Mode */
        u32 sync_mode : 2;
        u32 _unused1 : 5;
        /*! Chopping DMA Window Size (1 << N words) */
        u32 chopping_dma_win_size : 3;
        u32 _unused2 : 1;
        /*! Chopping CPU Window Size (1 << N words) */
        u32 chopping_cpu_win_size : 3;
        u32 _unused3 : 1;
        /*! (0=Stop/Completed, 1=Start/Enable/Busy) */
        u32 start_busy : 1;
        u32 _unused4 : 3;
        /*! (0=Normal, 1=Manual Start; used for SyncMode=0) */
        u32 start_trigger : 1;
        /*! nocash: =1 on syncmode=0 may cause a DMA pause? */
        u32 _unknown0 : 1;
        u32 _unknown1 : 1;
        u32 _unused5 : 1;
      };
      u32 raw;
    } CHCR;
  };

  struct DMARegisters {
    /*! DMA Channel registers (start at 0x1F801080 + N*0x10) */
    ChannelRegisters channels[DMAChannel::NumChannels];

    union DICR_Bits {
      struct {
        u32 _unknown : 6;
        u32 _unused : 9;
        u32 irq_force : 1;
        u32 irq_en : 7;
        u32 irq_master_en : 1;
        u32 irq_flags : 7;
        u32 irq_master_flag : 1;
      };
      u32 raw;
    };

    union DPCR_Bits {
      struct {
        /*! Priority (0..7; 0=Highest, 7=Lowest) */
        u32 dma0_pri : 3;
        /*! Master Enable (0=Disable, 1=Enable) */
        u32 dma0_en : 1;
        /*! Priority (0..7; 0=Highest, 7=Lowest) */
        u32 dma1_pri : 3;
        /*! Master Enable (0=Disable, 1=Enable) */
        u32 dma1_en : 1;
        /*! Priority (0..7; 0=Highest, 7=Lowest) */
        u32 dma2_pri : 3;
        /*! Master Enable (0=Disable, 1=Enable) */
        u32 dma2_en : 1;
        /*! Priority (0..7; 0=Highest, 7=Lowest) */
        u32 dma3_pri : 3;
        /*! Master Enable (0=Disable, 1=Enable) */
        u32 dma3_en : 1;
        /*! Priority (0..7; 0=Highest, 7=Lowest) */
        u32 dma4_pri : 3;
        /*! Master Enable (0=Disable, 1=Enable) */
        u32 dma4_en : 1;
        /*! Priority (0..7; 0=Highest, 7=Lowest) */
        u32 dma5_pri : 3;
        /*! Master Enable (0=Disable, 1=Enable) */
        u32 dma5_en : 1;
        /*! Priority (0..7; 0=Highest, 7=Lowest) */
        u32 dma6_pri : 3;
        /*! Master Enable (0=Disable, 1=Enable) */
        u32 dma6_en : 1;
        u32 _unknown : 4;
      };
      u32 raw;
    };

    /*! DMA Control Register (0x1F8010F0) */
    DPCR_Bits DPCR;

    /*! DMA Interrupt Register (0x1F8010F4) */
    DICR_Bits DICR;

    u32 unknown_1f8010f8;
    u32 unknown_1f8010fc;
  };

  DMARegisters m_regs;

  Console *m_console;

  void dma_block_copy(u32 chan);
  void dma_all_at_once(u32 chan);
  void dma_linked_list(u32 chan);

  void dma_completed(u32 chan);

  u32 calc_master_flag();

public:
  DMA(Console *console);

  void reset();

  // clang-format off
  u8 read_u8(u32 addr) override { return read_u32(addr); }
  u16 read_u16(u32) override { _check(false, "invalid dma read"); }
  void write_u8(u32 addr, u8 val) override { write_u32(addr, val); }
  void write_u16(u32, u16) override { _check(false, "invalid dma write"); }
  // clang-format on

  u32 read_u32(u32 addr) override;
  void write_u32(u32 addr, u32 val) override;
  void register_regions(fox::MemoryTable *memory) override;
};

}
