#pragma once

#include <variant>

#include "dragon_sdk/floating.h"
#include "shared/types.h"

namespace vpu {

struct Vector {
  Float16 x;
  Float16 y;
  Float16 z;
  Float16 w;
};

struct AttributeGlobal {
  u32 index : 5;
  Vector value;
};

struct AttributeShared {
  u32 index : 4;
  u32 allocate : 1;
  Vector value;
};

struct AttributeLocal {
  u32 index : 4;
  Vector value;
};

struct AttributeLaunch {
  u32 pc_offset : 4;
  u32 position : 9;
};

/*!
 * @brief Entry type for the attribute queue. Each entry stores one work
 *        attribute with interpretation dependent on the attribute type.
 *
 * There are four entry types which are differentiated by the number of
 * trailing 0 bits in the 72 bit entry:
 *
 *  - Global Register Data (VPU_AttributeGlobal):
 *      Data to be stored into a global register. Global register data is not
 *      banked. All 32 global registers are available to all tasks in a wave.
 *      Global registers should be used to store program constants and other
 *      frequently used values like pi or 1.0.
 *
 *  - Shared Register Data (VPU_AttributeShared):
 *      Data to be stored into a shared register. Shared registers are
 *      read-only for tasks and all tasks enqueued after new shared register
 *      data is provided will have shared access to this data. Shared
 *      registers are used by the rasterizer to pass per-vertex attributes to
 *      fragment shaders.
 *
 *      A new shared register bank is allocated when the "allocate" bit is
 *      set. The new register bank will be used for all tasks launched after
 *      this allocation.
 *
 *  - Local Register Data (VPU_AttributeLocal):
 *      Data to be stored into a local register. This provides initial
 *      register state for a VPT. Local registers are both readable and
 *      writable by VPTs and are unique to each VPT. Local registers are used
 *      by the rasterizer to provide U/V/W ratios for each fragment.
 *
 *      A new local register bank is automatically allocated when a task is
 *      launched.
 *
 *  - Task Launch Data (VPU_AttributeLaunch):
 *      Task start / enqueue request. Provides non-vector data required for
 *      configuring a VPT before work can start. Triggers an enqueue to
 *      to the internal task scheduler.
 */
using AttributeEntry = std::
  variant<AttributeGlobal, AttributeShared, AttributeLocal, AttributeLaunch>;

// TODO? Attribute queues currently not modeled

/*!
 * @brief Calculation modes for instruction output flags.
 */
enum class FlagMode : uint8_t {
    Sign       = 0,
    Inverted   = 1,
    Zero       = 2,
    SignOrZero = 3
};

/*!
 * @brief Conditional writeback modes. If / IfNot refer to the thread's active
 *        flag bits.
 */
enum class WritebackMode : uint8_t {
    If     = 0b00,
    IfNot  = 0b01,
    Never  = 0b10,
    Always = 0b11
};

/*!
 * @brief Functional unit within the VPC. Used in the instruction encoding to
 *        determine which unit will activated.
 */
enum class SubUnit : uint8_t {
    ALU = 0,
    LUT = 1,
    MEM = 2,
    TEX = 3,
    PRG = 4

    /* Remaining units not defined yet. */
};

/*!
 * @brief Bitfield breakout of a VPU instruction encoding.
 *
 * Note: Order is reversed from HDL by verilator / C bitfield convention.
 */
union Encoding {
    struct {
        /* Operation details (9b) */
        uint64_t immediate:4;    /**< Opcode-dependent 5b immediate value */
        uint64_t opcode:2;       /**< Functional unit opcode */
        uint64_t subunit:3;      /**< Responsible function subunit */

        /* Outputs (18b) */
        uint64_t flag_mask:4;    /**< Per-component mask for updating flags */
        uint64_t flag_mode:2;    /**< Method for determining new flags */
        uint64_t result_mask:8;  /**< Per-component result conditional writeback mask */
        uint64_t result_index:4; /**< Output register index */

        /* Register input B configuration (19b) */
        uint64_t zero_b:4;       /**< Per-component zeroing */
        uint64_t shuffle_b:8;    /**< Per-component shuffling indexes */
        uint64_t invert_b:1;     /**< Invert sign bit of input B */
        uint64_t input_b:6;      /**< Input register B index (local, shared, or global) */

        /* Register input A configuration (18b) */
        uint64_t zero_a:4;       /**< Per-component zeroing */
        uint64_t shuffle_a:8;    /**< Per-component shuffling indexes */
        uint64_t invert_a:1;     /**< Invert sign bit of input A */
        uint64_t input_a:5;      /**< Input register A index (local or shared) */
    } __attribute__ ((packed));

    uint64_t raw;
};
static_assert(sizeof(Encoding) == 8);


}
