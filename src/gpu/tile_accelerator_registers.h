
#include "shared/register_definition.h"

REGISTER(0x005F8124, TA_OL_BASE, "TA - Object List Base Address")
REGISTER(0x005F812C, TA_OL_LIMIT, "TA - Object List Limit Address")

REGISTER(0x005F8128, TA_ISP_BASE, "TA - ISP/TSP Base Address")
REGISTER(0x005F8130, TA_ISP_LIMIT, "TA - ISP/TSP Limit Address")

REGISTER(0x005F8138, TA_ITP_CURRENT, "TA - Current ITP Parameter Pointer")
REGISTER(0x005F8144, TA_LIST_INIT, "TA - Initialize Lists Command")

REGISTER(0x005F8134, TA_NEXT_OPB, "TA - Next OPB Address")
REGISTER(0x005F8160, TA_LIST_CONT, "TA - List Continuation")
REGISTER(0x005F8164, TA_NEXT_OPB_INIT, "TA - Next OPB Address Initialization")

REGISTER(0x005F813C, TA_GLOB_TILE_CLIP, "TA - Global Tile Clip Bounds")
REGISTER(0x005F8140, TA_ALLOC_CTRL, "TA - OPB Address Direction and Block Sizes")

REGISTER(0x005F8148, TA_YUV_TEX_BASE, "TA - YUV Texture Transfer Base Address")
REGISTER(0x005F814C, TA_YUV_TEX_CTRL, "TA - YUV Texture Transfer Control")
REGISTER(0x005F8150, TA_YUV_TEX_CNT, "TA - YUV Texture Macroblock Counter")

REGISTER(0x005F80E4, TEXT_CONTROL, "TA - Texture Control Word")
