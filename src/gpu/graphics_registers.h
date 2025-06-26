#pragma once

#include "shared/register_definition.h"

REGISTER(0x005F8000, DEVICE_ID, "Device ID")
REGISTER(0x005F8004, DEVICE_REVISION, "Device Revision")
REGISTER(0x005F8014, STARTRENDER, "Start Rendering")

REGISTER(0x005F8020, PARAM_BASE, "CORE Parameter Base Address")
REGISTER(0x005F802C, REGION_BASE, "CORE Region Array Base Address")

REGISTER(0x005F80A8, SDRAM_CFG, "Texture Memory Control")
REGISTER(0x005F80A0, SDRAM_REFRESH, "Texture Memory Refresh Counter")
REGISTER(0x005F8008, SOFTRESET, "Core and TA - Software Reset")

REGISTER(0x005F80D0, SPG_CONTROL, "Sync Pulse Generator Control")
REGISTER(0x005F810C, SPG_STATUS, "Sync Pulse Generator Status")
REGISTER(0x005F80D8, SPG_LOAD, "HV Counter Load Value")
REGISTER(0x005F80E0, SPG_WIDTH, "Sync Width Control")
REGISTER(0x005F80D4, SPG_HBLANK, "H-Blank Control")
REGISTER(0x005F80DC, SPG_VBLANK, "V-Blank Control")
REGISTER(0x005F80C8, SPG_HBLANK_INT, "H-Blank Interrupt Control")
REGISTER(0x005F80CC, SPG_VBLANK_INT, "V-Blank Interrupt Control")

REGISTER(0x005F80E8, VO_CONTROL, "Video Output Control")
REGISTER(0x005F80EC, VO_STARTX, "Video Output Start X")
REGISTER(0x005F80F0, VO_STARTY, "Video Output Start Y")
REGISTER(0x005F8040, VO_BORDER_COLOR, "Border Area Color")

REGISTER(0x005F80F4, SCALER_CTL, "X & Y Scaler Control")

REGISTER(0x005F8044, FB_R_CTRL, "Frame Buffer Read Control")
REGISTER(0x005F8048, FB_W_CTRL, "Frame Buffer Write Control")
REGISTER(0x005F804C, FB_W_LINESTRIDE, "Frame Buffer Write Line Stride")
REGISTER(0x005F8050, FB_R_SOF1, "Frame Buffer Read Field 1")
REGISTER(0x005F8054, FB_R_SOF2, "Frame Buffer Read Field 2")
REGISTER(0x005F805C, FB_R_SIZE, "Frame Buffer Size")
REGISTER(0x005F8060, FB_W_SOF1, "Frame Buffer Write Field 1")
REGISTER(0x005F8064, FB_W_SOF2, "Frame Buffer Write Field 2")
REGISTER(0x005F8068, FB_X_CLIP, "Frame Buffer X Clip Bounds")
REGISTER(0x005F806C, FB_Y_CLIP, "Frame Buffer Y Clip Bounds")

REGISTER(0x005F8078, FPU_CULL_VAL, "FP Culling Value")
REGISTER(0x005F807C, FPU_PARAM_CFG, "Parameter Read Config")
REGISTER(0x005F8088, ISP_BACKGND_D, "Background Plane Depth")
REGISTER(0x005F808C, ISP_BACKGND_T, "Background Surface Tag")

REGISTER(0x005F8108, PAL_RAM_CTRL, "Palette RAM Control")

REGISTER(0x005F80B0, FOG_COL_RAM, "Fog Color for Look-up Table mode")
REGISTER(0x005F80B4, FOG_COL_VERT, "Fog Color for Per-Vertex mode")
REGISTER(0x005F80B8, FOG_DENSITY, "Fog Scale Value for Look-up Table mode")
REGISTER(0x005F80BC, FOG_CLAMP_MAX, "Fog Max value for color clamping")
REGISTER(0x005F80C0, FOG_CLAMP_MIN, "Fog Min value for color clamping")
