#pragma once

#include "shared/types.h"

namespace Interrupts {

// See definitions in SB_ISTNRM (0x005f6900)
namespace Normal {
enum Type : u32
{
  EndOfPunchThroughList = 21,
  EndOfDMA_Sort = 20,
  EndOfDMA_CH2 = 19,
  EndOfDMA_G2Channel3_EXT3 = 18,
  EndOfDMA_G2Channel2_EXT2 = 17,
  EndOfDMA_G2Channel1_EXT1 = 16,
  EndOfDMA_G2Channel0_AICA = 15,
  EndOfDMA_GD = 14,
  MapleVBlankOver = 13,
  EndOfDMA_Maple = 12,
  EndOfDMA_PVR = 11,
  EndOfTransfer_TranslucentModifierVolume = 10,
  EndOfTransfer_Translucent = 9,
  EndOfTransfer_OpaqueModifierVolume = 8,
  EndOfTransfer_Opaque = 7,
  EndOfTransfer_YUV = 6,
  HBlankIn = 5,
  VBlankOut = 4,
  VBlankIn = 3,
  EndOfRender_TSP = 2,
  EndOfRender_ISP = 1,
  EndOfRender_Video = 0,
};

inline Type
get_end_of_dma_for_g2_channel(int channel)
{
  assert(channel >= 0 && channel <= 4);
  return (Type)(EndOfDMA_G2Channel0_AICA + channel);
}
}

// See definitions in SB_ISTEXT (0x005f6904)
namespace External {
enum Type : u32
{
  ExternalDevice = 3,
  Modem = 2,
  AICA = 1,
  GDROM = 0
};
}

// See definitions in SB_ISTERR (0x005f6908)
namespace Error {
enum Type : u32
{
  SH4_InhibitedArea = 31,
  __Reserved_30 = 30,
  __Reserved_29 = 29,
  DDT_SortDMA = 28,
  G2_TimeOutInCPUAccess = 27,
  G2_DMATimeout_Channel3 = 26,
  G2_DMATimeout_Channel2 = 25,
  G2_DMATimeout_Channel1 = 24,
  G2_DMATimeout_Channel0 = 23,
  G2_DMAOverrun_Channel3 = 22,
  G2_DMAOverrun_Channel2 = 21,
  G2_DMAOverrun_Channel1 = 20,
  G2_DMAOverrun_Channel0 = 19,
  G2_IllegalAddress_Channel3 = 18,
  G2_IllegalAddress_Channel2 = 17,
  G2_IllegalAddress_Channel1 = 16,
  G2_IllegalAddress_Channel0 = 15,
  G1_ROMFLASH_AccessForGDDMA = 14,
  G1_GDDMA_Overrun = 13,
  G1_IllegalAddress = 12,
  Maple_IllegalCommand = 11,
  Maple_WriteFIFOOverrun = 10,
  Maple_DMAOverrun = 9,
  Maple_IllegalAddress = 8,
  PVR_DMAOverrun = 7,
  PVR_IllegalAddress = 6,
  TA_FIFOOverflow = 5,
  TA_IllegalParameter = 4,
  TA_ObjectListPointerOverflow = 3,
  TA_ISPTSPParameterOverflow = 2,
  RENDER_HazardProcessingStripBuffer = 1,
  RENDER_ISPOutOfCache = 0
};
}

}