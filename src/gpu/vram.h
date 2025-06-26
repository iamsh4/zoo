#pragma once

#include "shared/types.h"

namespace gpu {
class VRAMAddress32;
class VRAMAddress64;

class VRAMAddress32 {
public:
  explicit VRAMAddress32(u32 addr) : _addr(addr) {}
  u32 get() const
  {
    return _addr;
  }
  VRAMAddress32 &operator+=(const u32 offset)
  {
    _addr += offset;
    return *this;
  }
  VRAMAddress32 operator+(const u32 offset) const
  {
    return VRAMAddress32(_addr + offset);
  }
  VRAMAddress64 to64() const;

private:
  u32 _addr;
};

class VRAMAddress64 {
public:
  explicit VRAMAddress64(u32 addr) : _addr(addr) {}
  u32 get() const
  {
    return _addr;
  }
  VRAMAddress64 &operator+=(const u32 offset)
  {
    _addr += offset;
    return *this;
  }
  VRAMAddress64 operator+(const u32 offset) const
  {
    return VRAMAddress64(_addr + offset);
  }
  VRAMAddress32 to32() const;

private:
  u32 _addr;
};
}