#pragma once

#include <unordered_set>
#include <functional>

#include "fox/ir_assembler.h"
#include "fox/jit/cache.h"
#include "fox/memtable.h"
#include "fox/guest.h"
#include "shared/types.h"
#include "guest/r3000/decoder.h"

namespace gui {
class R3000CPUWindowGuest;
}

namespace guest::r3000 {

class Coprocessor {
protected:
  fox::ir::Assembler *a = nullptr;

public:
  virtual ~Coprocessor() {}
  void set_assembler(fox::ir::Assembler *assembler)
  {
    a = assembler;
  }
  virtual u32 handle_cop_ir(u32 cofun) = 0;
};

// Value used to signify that no writeback is pending for
// an instruction in the pipeline.
static constexpr u32 INVALID_WRITEBACK_INDEX = 0xFFFFFFFF;
static constexpr u32 INVALID_BRANCH_DELAY_ADDRESS = 0xFFFFFFFF;

namespace Exceptions {
enum Exception
{
  Interrupt = 0,
  TlbModified,
  TlbLoad,
  TlbStore,
  AddressErrorLoad,
  AddressErrorStore,
  BusErrorFetch,
  BusErrorDataLoadStore,
  Syscall,
  Breakpoint,
  ReservedInstruction,
  CoprocessorUnusable,
  ArithmeticOverflow,
};
} // namespace Exceptions

namespace Registers {

/* clang-format off */
enum RegisterEnum {
  /*
   * CPU registers
   */

  /* Main registers (typically using named aliases like s0, ra, etc) */
  R0 = 0,

  /* Result registers for division. */
  HI = R0 + 32,
  LO,

  /* Program counter */
  PC,

  /*
   * Coprocessor registers
   */

  /* Coprocessor 0 data and control registers */
  COP0_DATA,
  COP0_CTRL = COP0_DATA + 32,

  /* Coprocessor 1 data and control registers */
  COP1_DATA = COP0_CTRL + 32,
  COP1_CTRL = COP1_DATA + 32,

  /* Coprocessor 2 data and control registers */
  COP2_DATA = COP1_CTRL + 32,
  COP2_CTRL = COP2_DATA + 32,

  /* Coprocessor 3 data and control registers */
  COP3_DATA = COP2_CTRL + 32,
  COP3_CTRL = COP3_DATA + 32,

  /*
   * Virtual registers (used to implement CPU behavior quirks)
   */

  // Tracks the register index for a pending writeback in the
  // pipeline. If this == NO_PENDING_WRITEBACK then there is no
  // pending write-back.
  DELAYED_WRITEBACK_REG_INDEX = COP3_CTRL + 32,
  DELAYED_WRITEBACK_REG_VALUE,

  // Used to track the address for a branch, and whether or not to take that branch (if != 0)
  BRANCH_DELAY_ADDRESS,
  BRANCH_DELAY_DECISION,

  /*
   * Aliases of registers for readability
   */

  //////////////////////////////////////////////
  // !! EVERYTHING BELOW HERE ARE NOT REGISTERS

  /*! Number of registers (including virtual implementation registers) */
  NUM_REGS,

  RA = R0 + 31,
  SR = COP0_DATA + 12,
  CAUSE = COP0_DATA + 13,
  EPC = COP0_DATA + 14,
  SP = R0 + 29,
};
/* clang-format on */

/*!
 * @brief Total number of registers reserved for each coprocessor. There are 32
 *        data and 32 control registers per coprocessor.
 */
static constexpr unsigned NUM_REGS_PER_COP = 64;

struct CAUSE_Bits {
  static constexpr unsigned BD_bit = 31;

  union {
    struct {
      u32 _unused0 : 2;
      u32 ExcCode : 5; /*!< Exception Code. See R3000::Exception */
      u32 _unused1 : 1;
      u32 IP : 8; /*!< Interrupt Pending. */
      u32 _unused2 : 12;
      u32 CE : 2; /*!< Coprocessor error. Contains the coprocessor index if an instruction
                     is executed which refers to a non-existent coprocessor. */
      u32 _unused3 : 1;
      u32 BD : 1; /*!< Branch Delay. If Exception occurs in a branch delay slot, this
                     allows the handler to see that this happened.  */
    };
    u32 raw;
  };
};

/*
 * @brief R3000 cop0r12 aka Status Register
 */
struct SR_Bits {
  static constexpr unsigned BEV_bit = 22;

  union {
    struct {
      u32 IEc : 1; /*!< When set, interrupts are enabled */
      u32 KUc : 1; /*!< When set, CPU is in kernel mode */
      u32 IEp : 1; /*!< Original value of IEc before exception */
      u32 KUp : 1; /*!< Original value of KUc before exception */
      u32 IEo : 1; /*!< Original value of IEp before exception */
      u32 KUo : 1; /*!< Original value of KUp before exception */
      u32 _unused0 : 2;
      u32 IM : 8;  /*!< Interrupt mask */
      u32 IsC : 1; /*!< Isolate d-cache (writes do not get sent to memory) */
      u32 SwC : 1; /*!< Swap d-cache and i-cache */
      u32 PZ : 1;  /*!< Write cache parity bits as 0, disable checks */
      u32 CM : 1;  /*!< Last load from isolated d-cache was valid */
      u32 PE : 1;  /*!< Cache parity error has been detected */
      u32 TS : 1;  /*!< TLB shutdown. Two TLB entries were found at the same virtual
                      address */
      u32 BEV : 1; /*!< Boot exception vector (0 is RAM, 1 is ROM) */
      u32 _unused1 : 2;
      u32 RE : 1; /*!< Reverse endianness in user mode */
      u32 _unused2 : 2;
      u32 CU0 : 1; /*!< Enable coprocessor 0 */
      u32 CU1 : 1; /*!< Enable coprocessor 1 */
      u32 CU2 : 1; /*!< Enable coprocessor 2 */
      u32 CU3 : 1; /*!< Enable coprocessor 3 */
    };
    u32 raw;
  };

  SR_Bits(u32 _raw) : raw(_raw) {}
};

}

/*! Returns the human-readable name for a register. */
const char *get_gpr_name(unsigned index, bool use_register_mnemonics = false);

// MIPS R3000 (MIPS 1 ISA), intended to model the behavior of PS1.
class R3000 : public fox::Guest /*, serialization::Serializer */ {
private:
  void reset();

  void check_enter_irq();

  //
  u32 m_regs[Registers::NUM_REGS];

  fox::MemoryTable *const m_mem;

  fox::Value guest_register_read(unsigned index, size_t bytes) final;
  void guest_register_write(unsigned index, size_t bytes, fox::Value value) final;
  fox::Value guest_load(u32 address, size_t bytes) final;
  void guest_store(u32 address, size_t bytes, fox::Value value) final;

  enum class AddressType
  {
    Invalid,
    AccessViolation,
    Physical,
    Register,
  };
  std::pair<AddressType, u32> mem_region(u32 address, bool is_kernel_mode) const;

  /*!
   * @brief Read from a memory location from the viewpoint of the ARM
   *        core.
   */
  template<typename T>
  T mem_read(u32 address);

  /*!
   * @brief Write to a memory location from the viewpoint of the ARM
   *        core.
   */
  template<typename T>
  void mem_write(u32 address, T value);

  // Triggers a coprocessor-operation on
  void cop_operation(u32 cop_unit, u32 cop_function);

  Registers::SR_Bits &SR();

  bool external_irq = false;

  // Allow decoder access for IR guest calls
  friend class Decoder;

  friend class Assembler;

  friend class ::gui::R3000CPUWindowGuest;

  class BasicBlock;

  fox::jit::Cache m_jit_cache;

  std::vector<u32> exec_breakpoints;
  std::unordered_set<u32> write_breakpoints;

public:
  // Publicly-accessible functions

  R3000(fox::MemoryTable *memory_table);

  bool has_breakpoint(u32 address);

  //
  u32 step_instruction();
  //
  u32 step_block();

  u32 fetch_instruction(u32 address);

  u32 PC() const
  {
    return m_regs[Registers::PC];
  }

  void set_register(Registers::RegisterEnum reg, u32 value)
  {
    m_regs[reg] = value;
  }

  u32 const *registers() const
  {
    return &m_regs[0];
  }

  // Raise external interrupt
  void raise_irq();

  static const char *get_register_name(unsigned index, bool use_register_mnemonics);

  void dump();

  void set_external_irq(bool new_state);

  static constexpr u32 PHYSICAL_MASK = 0x1fff'ffff;

  void breakpoint_add(u32 address);
  void breakpoint_remove(u32 address);
  void breakpoint_list(std::vector<u32> *results);

  void add_mem_write_watch(u32 address);
  void remove_mem_write_watch(u32 address);
  void write_watch_list(std::vector<u32>* out);
  bool m_halted = false;

private:
  Coprocessor *m_coprocessors[4] = {};
  std::function<void(u32, u32)> m_write_watch_callback;

public:
  void set_coprocessor(u32 index, Coprocessor *coprocessor);
  void set_write_watch_callback(std::function<void(u32, u32)> func)
  {
    m_write_watch_callback = func;
  }
};

} // namespace guest::cpu
