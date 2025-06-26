namespace cpu {

/*!
 * @struct SH4::MMIO
 * @brief State of memory mapped CPU registers
 */
struct MMIO {
  /******** Exception handling registers ********/

  /*!
   * @brief Exception: TRAPA exception register @ H'FF00 0020
   *
   * Access:
   *   ReadWrite 32-bit (but masked to 0x000003FC)
   * Initialization:
   *   Power-on Reset - Undefined
   *   Manual Reset   - Undefined
   *   Sleep          - Retained
   *   Standby        - Retained
   */
  union TRA_bits {
    static constexpr u32 address = 0xFF000020u;
    static constexpr u32 mask    = 0x000003FC;
    u32 raw;
  };

  TRA_bits TRA;

  /*!
   * @brief Exception: Exception event register @ H'FF00 0024
   *
   * Access:
   *   ReadOnly 32-bit (but masked to 0x00000FFF)
   * Initialization:
   *   Power-on Reset - H'0000 0000
   *   Manual Reset   - H'0000 0020
   *   Sleep          - Retained
   *   Standby        - Retained
   * Value:
   *   H'000 - [Priority 1] Power on reset
   *           [Priority 1] H-UDI reset
   *   H'020 - [Priority 2] Manual reset
   *   H'140 - [Priority 3] Instruction TLB multiple hit
   *           [Priority 4] Data TLB multiple hit
   */
  union EXPEVT_bits {
    static constexpr u32 address = 0xFF000024u;
    static constexpr u32 mask    = 0x0000FFFFu;
    u32 raw;
  };

  EXPEVT_bits EXPEVT;

  /*!
   * @brief Exception: Interrupt event register @ H'FF00 0028
   *
   * Access:
   *   ReadWrite 32-bit (but masked to 0x00003FFF)
   * Initialization:
   *   Power-on Reset - Undefined
   *   Manual Reset   - Undefined
   *   Sleep          - Retained
   *   Standby        - Retained
   */
  union INTEVT_bits {
    static constexpr u32 address = 0xFF000028u;
    static constexpr u32 mask    = 0x00003FFFu;
    u32 raw;
  };

  INTEVT_bits INTEVT;

  /******** MMU registers ********/

  /*!
   * @brief MMU: Page table entry high register @ H'FF00 0000
   *
   * Access:
   *  ReadWrite 32-bit (but masked to 0xFFFFFCFF)
   * Initialization:
   *   Power-on Reset - Undefined
   *   Manual Reset   - Undefined
   *   Sleep          - Retained
   *   Standby        - Retained
   */
  union PTEH_bits {
    static constexpr u32 address = 0xFF000000u;
    static constexpr u32 mask    = 0xFFFFFCFFu;
    u32 raw;
  };

  PTEH_bits PTEH;

  /*!
   * @brief MMU: Page table entry low register @ H'FF00 0004
   *
   * Access:
   *  ReadWrite 32-bit (but masked to 0x1FFFFDFF)
   * Initialization:
   *   Power-on Reset - Undefined
   *   Manual Reset   - Undefined
   *   Sleep          - Retained
   *   Standby        - Retained
   */
  union PTEL_bits {
    static constexpr u32 address = 0xFF000004u;
    static constexpr u32 mask    = 0x1FFFFDFFu;
    u32 raw;
  };

  PTEL_bits PTEL;

  /*!
   * @brief MMU: Translation table base register @ H'FF00 0008
   *
   * Access:
   *  ReadWrite 32-bit
   * Initialization:
   *   Power-on Reset - Undefined
   *   Manual Reset   - Undefined
   *   Sleep          - Retained
   *   Standby        - Retained
   */
  union TTB_bits {
    static constexpr u32 address = 0xFF000008u;
    static constexpr u32 mask    = 0xFFFFFFFFu;
    u32 raw;
  };

  TTB_bits TTB;

  /*!
   * @brief MMU: TLB exception address register @ H'FF00 000C
   *
   * Access:
   *  ReadWrite 32-bit (but masked to 0x0000000F)
   * Initialization:
   *   Power-on Reset - Undefined
   *   Manual Reset   - Retained
   *   Sleep          - Retained
   *   Standby        - Retained
   */
  union TEA_bits {
    static constexpr u32 address = 0xFF00000Cu;
    static constexpr u32 mask    = 0x0000000Fu;
    u32 raw;
  };

  TEA_bits TEA;

  /*!
   * @brief MMU: MMU control register @ H'FF00 0010
   *
   * Access:
   *  ReadWrite 32-bit (but masked to 0xFCFCFF05)
   *
   * Initialization:
   *  Power-on Reset - H'0000 0000
   *  Manual Reset   - H'0000 0000
   *  Sleep          - Retained
   *  Standby        - Retained
   */
  union MMUCR_bits {
    static constexpr u32 address = 0xFF000010u;
    static constexpr u32 mask    = 0xFCFCFF05u;
    u32 raw;

    struct {
      u32 AT : 1;     // Enable MMU
      u32 _rsvd0 : 1; // Read-only 0
      u32 TI : 1;     // TLB Invalidate
      u32 _rsvd1 : 5; // Read-only 0
      u32 SV : 1;     // Single / Multiple Virtual Memory Mode
      u32 SQMD : 1;   // Store Queue Mode Bit
      u32 URC : 6;    // UTLB Replace Counter
      u32 _rsvd2 : 2; // Read-only 0
      u32 URB : 6;    // UTLB Replace Boundary
      u32 _rsvd3 : 2; // Read-only 0
      u32 LRUI : 6;   // Least recently used ITLB entry
    };
  };

  MMUCR_bits MMUCR;

  /******** Cache Registers ********/

  /*!
   * @brief Cache: Cache control register @ H'FF00 001C
   *
   * Access:
   *  ReadWrite 32-bit (but masked to 0x0000090F)
   *
   * Initialization:
   *  Power-on Reset - H'0000 0000
   *  Manual Reset   - H'0000 0000
   *  Sleep          - Retained
   *  Standby        - Retained
   */
  union CCR_bits {
    static constexpr u32 address = 0xFF00001Cu;
    static constexpr u32 mask    = 0x000089AF;
    u32 raw;
    struct {
      u32 OCE : 1;
      u32 WT : 1;
      u32 CB : 1;
      u32 OCI : 1;
      u32 _rsvd0 : 1;
      u32 ORA : 1;
      u32 _rsvd1 : 1;
      u32 OIX : 1;
      u32 ICE : 1;
      u32 _rsvd2 : 2;
      u32 ICI : 1;
      u32 _rsvd3 : 3;
      u32 IIX : 1;
      u32 _rsvd4 : 16;
    };
  };

  CCR_bits CCR;

  /*!
   * @brief Cache: Queue address control register 0 @ H'FF00 0038
   *
   * Access:
   *  ReadWrite 32-bit (but masked to 0x0000001C)
   *
   * Initialization:
   *  Power-on Reset - Undefined
   *  Manual Reset   - Undefined
   *  Sleep          - Retained
   *  Standby        - Retained
   */
  union QACR0_bits {
    static constexpr u32 address = 0xFF000038u;
    static constexpr u32 mask    = 0x0000001Cu;
    u32 raw;
  };

  QACR0_bits QACR0;

  /*!
   * @brief Cache: Queue address control register 1 @ H'FF00 003C
   *
   * Access:
   *  ReadWrite 32-bit (but masked to 0x0000001Cu)
   *
   * Initialization:
   *  Power-on Reset - Undefined
   *  Manual Reset   - Undefined
   *  Sleep          - Retained
   *  Standby        - Retained
   */
  union QACR1_bits {
    static constexpr u32 address = 0xFF00003Cu;
    static constexpr u32 mask    = 0x0000001Cu;
    u32 raw;
  };

  QACR1_bits QACR1;

  /*!
   * @brief Cache: On-chip memory control register @ H'FF00 0074
   *
   * Access:
   *  ReadWrite 32-bit (but masked to 0x000003C0u)
   *
   * Initialization:
   *  Power-on Reset - H'0000 0000
   *  Manual Reset   - H'0000 0000
   *  Sleep          - Retained
   *  Standby        - Retained
   */
  union RAMCR_bits {
    static constexpr u32 address = 0xFF000074u;
    static constexpr u32 mask    = 0x000003C0u;
    u32 raw;
  };

  RAMCR_bits RAMCR;

  /******** Bus State Controller Registers ********/

  /*!
   *
   * Access:
   *
   * Initialization:
   *  Power-on Reset -
   *  Manual Reset   -
   *  Sleep          -
   *  Standby        -
   */
  union PCTRA_bits {
    static constexpr u32 address = 0xFF80002Cu;
    static constexpr u32 mask    = 0xFFFFFFFFu;
    mutable u32 raw;
  };

  PCTRA_bits PCTRA;

  /*!
   * @brief BSC: Port data register A @ H'FF80 0030
   *
   * Access:
   *  ReadWrite 16-bit (but masked to 0x000003C0u)
   *
   * Initialization:
   *  Power-on Reset - Undocumented (but using 0)
   *  Manual Reset   - Undocumented
   *  Sleep          - Undocumented
   *  Standby        - Undocumented
   */
  union PDTRA_bits {
    static constexpr u32 address = 0xFF800030u;
    static constexpr u32 mask    = 0xFFFFu;
    mutable u32 raw;
  };

  PDTRA_bits PDTRA;

  /*!
   * @brief BSC: Port data register B @ H'FF80 0030
   *
   * Access:
   *  ReadWrite 16-bit (but masked to 0x000003C0u)
   *
   * Initialization:
   *  Power-on Reset - Undocumented (but using 0)
   *  Manual Reset   - Undocumented
   *  Sleep          - Undocumented
   *  Standby        - Undocumented
   */
  union PDTRB_bits {
    static constexpr u32 address = 0xFF800034u;
    static constexpr u32 mask    = 0xFFFFu;
    mutable u32 raw;
  };

  PDTRB_bits PDTRB;

  /******** DMA Control Registers ********/

  /*!
   * @brief SARn: DMA Source Address N
   *
   * Access:
   *  ReadWrite 32-bit
   *
   * Initialization:
   *  Power-on Reset - XXX
   *  Manual Reset   - XXX
   *  Sleep          - XXX
   *  Standby        - XXX
   */
  union SARn_bits {
    static constexpr u32 address = 0xFFA00000u;
    static constexpr u32 stride  = 0x00000010u;
    static constexpr u32 mask    = 0xFFFFFFFFu;
    u32 raw;
  };

  SARn_bits SARn[4];

  /*!
   * @brief DARn: DMA Destination Address N
   *
   * Access:
   *  ReadWrite 32-bit
   *
   * Initialization:
   *  Power-on Reset - XXX
   *  Manual Reset   - XXX
   *  Sleep          - XXX
   *  Standby        - XXX
   */
  union DARn_bits {
    static constexpr u32 address = 0xFFA00004u;
    static constexpr u32 stride  = 0x00000010u;
    static constexpr u32 mask    = 0xFFFFFFFFu;
    u32 raw;
  };

  DARn_bits DARn[4];

  /*!
   * @brief DMATCRn: DMA Transfer Count N
   *
   * Access:
   *  ReadWrite 32-bit
   *
   * Initialization:
   *  Power-on Reset - XXX
   *  Manual Reset   - XXX
   *  Sleep          - XXX
   *  Standby        - XXX
   */
  union DMATCRn_bits {
    static constexpr u32 address = 0xFFA00008u;
    static constexpr u32 stride  = 0x00000010u;
    static constexpr u32 mask    = 0xFFFFFFFFu;
    u32 raw;
  };

  DMATCRn_bits DMATCRn[4];

  /*!
   * @brief CHCRn: DMA Channel Control N
   *
   * Access:
   *  ReadWrite 32-bit
   *
   * Initialization:
   *  Power-on Reset - XXX
   *  Manual Reset   - XXX
   *  Sleep          - XXX
   *  Standby        - XXX
   */
  union CHCRn_bits {
    static constexpr u32 address = 0xFFA0000Cu;
    static constexpr u32 stride  = 0x00000010u;
    static constexpr u32 mask    = 0x00FFF7FFu; // XXX
    u32 raw;

    struct {
      u32 DE : 1;      // DMAC enable
      u32 TE : 1;      // Transfer End
      u32 IE : 1;      // Interrupt Enable
      u32 _rsvd0 : 1;  // Undocumented
      u32 TS : 3;      // Transfer Size
      u32 TM : 1;      // Cycle Steal Mode
      u32 RS : 4;      // Resource Select
      u32 SM : 2;      // Source Address Mode
      u32 DM : 2;      // Destination Address Mode
      u32 _rsvd1 : 15; // Ignored, but have meaning
    };
  };

  CHCRn_bits CHCRn[4];

  /******** TMU (Timer Management) ********/

  /** There are 3 32-bit timers in the SH4. Each TCNT[n] register counts
   * down according to a set clock source.
   *
   * When a 1 is present in the STR[n] bits of TMU.TSTR, then that
   * timer is counting. When a TCNT[n] underflows, the UNF flag is
   * set in the corresponding TCR[n] control register. If the UNIE[n]
   * bit of TCR[n] is set, an interrupt request is sent to the CPU.
   * At the same time, the value from TCOR is copied to TCNT
   * (which is called "auto-reload" function).
   */

  static constexpr unsigned NUM_TMU_CHANNELS = 3;

  /** Timer output control */
  union TOCR_bits {
    static constexpr u32 address = 0xFFD80000;
    static constexpr u16 mask    = 0xFFu; /* XXX */
    u32 raw                      = 0;
  };
  TOCR_bits TOCR;

  /** Timer start register */
  union TSTR_bits {
    static constexpr u32 address = 0xFFD80004;
    static constexpr u32 mask    = 0b111; /* XXX */
    u32 raw                      = 0;
  };
  TSTR_bits TSTR;

  /** Timer constant registers */
  union TCOR_bits {
    static constexpr u32 address = 0xFFD80008;
    static constexpr u32 stride  = 0x0000000Cu;
    static constexpr u32 mask    = 0xFFFFFFFFu; /* XXX */
    u32 raw                      = 0xFFFFFFFFu;
  };
  TCOR_bits TCOR[NUM_TMU_CHANNELS];

  /** Timer counter registers */
  union TCNT_bits {
    static constexpr u32 address = 0xFFD8000Cu;
    static constexpr u32 stride  = 0x0000000Cu;
    static constexpr u32 mask    = 0xFFFFFFFFu; /* XXX */
    u32 raw                      = 0xFFFFFFFFu;
  };
  TCNT_bits TCNT[NUM_TMU_CHANNELS];

  /** Timer control registers */
  union TCR_bits {
    static constexpr u32 address = 0xFFD80010;
    static constexpr u32 stride  = 0x0000000Cu;
    static constexpr u32 mask    = 0xFFFFu; /* XXX */
    struct {
      /** Timer prescaler */
      u16 TPSC : 3;
      /** Clock edge */
      u16 CKEG : 2;
      /** Underflow interrupt enable*/
      u16 UNIE : 1;
      u16 _rsvd0 : 2;
      /** Underflow flag */
      u16 UNF : 1;
      u16 _rsvd1 : 7;
    };
    u16 raw;
  };
  TCR_bits TCR[4];

  /******** Bus State Controller Registers ********/

  /*!
   * @brief BCR1: Control register 1
   *
   * Access:
   *  ReadWrite 32-bit
   */
  union BCR1_bits {
    static constexpr u32 address = 0xFF800000u;
    static constexpr u32 mask    = 0xFFFFFFFFu;
    u32 raw;
  };

  BCR1_bits BCR1;

  /*!
   * @brief BCR1: Control register 2
   *
   * Access:
   *  ReadWrite 16-bit
   */
  union BCR2_bits {
    static constexpr u32 address = 0xFF800004u;
    static constexpr u32 mask    = 0xFFFFu;
    u32 raw;
  };

  BCR2_bits BCR2;

  /*!
   * @brief WCR1: Wait control register 1
   *
   * Access:
   *  ReadWrite 32-bit
   */
  union WCR1_bits {
    static constexpr u32 address = 0xFF800008u;
    static constexpr u32 mask    = 0xFFFFFFFFu;
    u32 raw;
  };

  WCR1_bits WCR1;

  /*!
   * @brief WCR2: Wait control register 2
   *
   * Access:
   *  ReadWrite 32-bit
   */
  union WCR2_bits {
    static constexpr u32 address = 0xFF80000Cu;
    static constexpr u32 mask    = 0xFFFFFFFFu;
    u32 raw;
  };

  WCR2_bits WCR2;

  /*!
   * @brief WCR3: Wait control register 3
   *
   * Access:
   *  ReadWrite 32-bit
   */
  union WCR3_bits {
    static constexpr u32 address = 0xFF800010u;
    static constexpr u32 mask    = 0xFFFFFFFFu;
    u32 raw;
  };

  WCR3_bits WCR3;

  /******** System Memory Control Registers ********/

  /*!
   * @brief MCR: Individual Memory Control
   *
   * Access:
   *  ReadWrite 32-bit
   */
  union MCR_bits {
    static constexpr u32 address = 0xFF800014u;
    static constexpr u32 mask    = 0xFFFFFFFFu;
    u32 raw;
  };

  MCR_bits MCR;

  /*!
   * @brief IPRA: Interrupt Priority Register A
   *
   * Access:
   *  ReadWrite 16-bit
   */
  union IPRA_bits {
    static constexpr u32 address = 0xFFD00004u;
    u32 raw;
  };

  IPRA_bits IPRA;

  /*!
   * @brief IPRB: Interrupt Priority Register B
   *
   * Access:
   *  ReadWrite 16-bit
   */
  union IPRB_bits {
    static constexpr u32 address = 0xFFD00008u;
    u32 raw;
  };

  IPRB_bits IPRB;

  /*!
   * @brief IPRC: Interrupt Priority Register C
   *
   * Access:
   *  ReadWrite 16-bit
   */
  union IPRC_bits {
    static constexpr u32 address = 0xFFD0000Cu;
    u32 raw;
  };

  IPRC_bits IPRC;
};

}
