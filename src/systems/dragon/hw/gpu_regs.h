#pragma once

#include "shared/types.h"

namespace zoo::dragon::gpu  {

enum Register : u8 {
    BUSY = 0,
    WAIT = 1,
    CMD_FIFO_START = 2,
    CMD_FIFO_CLEAR = 3,
    CMD_FIFO_COUNT = 4,

    CMD_BUF_BEGIN = 5,
    CMD_BUF_END = 6,
    CMD_BUF_EXEC = 7,

    EE_INTERRUPT = 8,

    EXEC_DRAW_TRIANGLES = 0x10,
    TRIANGLE_FORMAT = 0x11,
    TRIANGLE_INDEX_ADDR = 0x12,
    TRIANGLE_VERTEX_ADDR = 0x13,
    TRIANGLE_COUNT = 0x14,
    DRAW_BIN_XY = 0x15,

    EXEC_VPU0_DMA = 0x20,
    EXEC_VPU1_DMA = 0x21,
    VPU0_DMA_CONFIG = 0x22,
    VPU1_DMA_CONFIG = 0x23,
    VPU0_DMA_BUFFER_ADDR = 0x24,
    VPU1_DMA_BUFFER_ADDR = 0x25,
    VPU0_DMA_EXTERNAL_ADDR = 0x26,
    VPU1_DMA_EXTERNAL_ADDR = 0x27,

    VPU_REG_XY = 0x30,
    VPU_REG_ZW = 0x31,
    EXEC_WRITE_VPU_GLOBAL = 0x32,
    EXEC_WRITE_VPU_SHARED = 0x33,
    EXEC_VPU_LAUNCH_ARRAY = 0x34,
};

enum PerfCounter : u8 {
    GPU_CYCLE_COUNT = 0,
    GPU_FIFO_COMMANDS_PROCESSED = 1,
    GPU_VPU0_CYCLES_ACTIVE = 2,
    GPU_VPU1_CYCLES_ACTIVE = 3,
};

enum BusyBits {
    BUSY_BIT_DRAW = 1 << 0,
    BUSY_BIT_VPU0_DMA = 1 << 1,
    BUSY_BIT_VPU1_DMA = 1 << 2,
    BUSY_BIT_VPU0 = 1 << 3,
    BUSY_BIT_VPU1 = 1 << 4,
};

}
