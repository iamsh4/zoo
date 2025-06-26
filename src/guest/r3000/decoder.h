#pragma once

#include "shared/types.h"
#include "guest/r3000/r3000.h"

namespace guest::r3000 {

class R3000;

/*!
 * @brief Bitfields representing the various instruction encodings used by the
 *        R3000 CPU.
 */
struct Instruction {
  const union {
    struct {
      u32 imm : 16;
      u32 rt : 5;
      u32 rs : 5;
      u32 op : 6;
    };

    struct {
      u32 target : 26;
      u32 _0 : 6; /* op */
    };

    struct {
      u32 function : 6;
      u32 shamt : 5;
      u32 rd : 5;
      u32 _2 : 5; /* rt */
      u32 _3 : 5; /* rs */
      u32 _1 : 6; /* op */
    };

    u32 raw;
  };

  Instruction() = delete;
  explicit Instruction(const u32 raw) : raw(raw) {}

  /*!
   * @brief Return the 16 bit immediate value after sign extension to 32 bits
   *        and casting back to a u32.
   */
  u32 imm_se() const;

  bool is_i_type() const;
  bool is_j_type() const;
  bool is_r_type() const;
};

/*!
 * @class guest::r3000:Decoder
 * @brief Simple instruction decoder for the R3000. Can be used to follow a
 *        sequence of instructions and lookup their basic attributes.
 */
class Decoder final {
public: /* Types */
  /*!
   * @brief Flags that define R3000 instruction attributes.
   */
  enum class Flag {
    Branch,         /*!< Can change PC */
    Conditional,    /*!< Changes PC conditionally */
    Relative,       /*!< Changes PC with a relative constant */
    MemoryStore,    /*!< Writes to memory */
    MemoryLoad,     /*!< Reads from memory */
    HasDelaySlot,   /*!< For branches, they branch after a 1 instruction delay */
    Exception,      /*!< The instruction can raise an exception */
    SourceS,        /*!< Uses source register Rs */
    SourceT,        /*!< Uses source register Rt */
    NoForwardDelay, /*!< Source operands are read after delayed writes. */
  };

  struct Info {
    bitflags<Flag> flags;
  };

public: /* Public API */
  Decoder(R3000 *cpu);
  ~Decoder();

  Info decode(u32 address);

private: /* Internal data */
  R3000 *const m_cpu;
};

}
