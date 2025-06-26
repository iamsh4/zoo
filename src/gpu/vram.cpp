#include "vram.h"

namespace gpu {

VRAMAddress32
VRAMAddress64::to32() const
{
  u32 offset = 0;

  // Honestly, just stare at figure 2-7 on page 56 for a while.
  if (_addr & 0x0000'0004) {
    offset = 0x0040'0000 + ((_addr - 4u) / 2);
  } else {
    offset = _addr / 2;
  }

  return VRAMAddress32 { offset };
}

VRAMAddress64
VRAMAddress32::to64() const
{
  u32 offset = 0;

  // Honestly, just stare at figure 2-7 on page 56 for a while.
  if (_addr >= 0x0040'0000) {
    offset = 0x0000'0004 + (_addr - 0x0040'0000) * 2;
  } else {
    offset = _addr * 2;
  }

  return VRAMAddress64 { offset };
}
}