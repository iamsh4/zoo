#pragma once

#include "shared/types.h"

namespace gpu_debug {

// GPU Commands are made of a command word (e.g. mmio register index) and a value word.

// GPU Debugging is done by expressing 'intent' in the top 8 registers
// of the user/perf area in the gpu.

// 0x80xx01yy <- value32

// gpu command word : 80xx01yy (xx = debug word type, yy = user area index in [f8,ff])
// DebugWord always written to 80xx01f8, additional args in following registers

enum DebugWord : u32 {
    DEBUG_WORD_NOP = 0,

    // Sections provide a 'nesting' for debug information.

    // Pushes new section to logical stack. Value is sysmem address of section name.
    DEBUG_WORD_PUSH_SECTION,
    // Pop section from stack
    DEBUG_WORD_POP_SECTION,

    // "Intents" express a high level operation that following command buffer entries
    // are intended to achieve. Additional information may be written

    // DMA sysmem area to tile (both VPUs)
    DEBUG_WORD_INTENT_DMA_SYSMEM_TO_TILE,
    // DMA tile to sysmem area (both VPUs)
    DEBUG_WORD_INTENT_DMA_TILE_TO_SYSMEM,
};

}
