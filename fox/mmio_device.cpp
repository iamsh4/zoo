#include <stdexcept>
#include <fmt/core.h>
#include "fox/mmio_device.h"

namespace fox {

MMIODevice::MMIODevice()
{
  return;
}

MMIODevice::~MMIODevice()
{
  return;
}

void
MMIODevice::write_u8(const u32 address, const u8 value)
{
  fmt::print("Unhandled MMIODevice::write_u8 at address 0x{:08x}, value 0x{:02x}\n", address, value);
  throw std::runtime_error("Unhandled MMIODevice::write_u8");
}

void
MMIODevice::write_u16(const u32 address, const u16 value)
{
  fmt::print("Unhandled MMIODevice::write_u16 at address 0x{:08x}, value 0x{:04x}\n", address, value);
  throw std::runtime_error("Unhandled MMIODevice::write_u16");
}

void
MMIODevice::write_u32(const u32 address, const u32 value)
{
  fmt::print("Unhandled MMIODevice::write_u32 at address 0x{:08x}, value 0x{:08x}\n", address, value);
  throw std::runtime_error("Unhandled MMIODevice::write_u32");
}

void
MMIODevice::write_u64(const u32 address, const u64 value)
{
  fmt::print("Unhandled MMIODevice::write_u64 at address 0x{:08x}, value 0x{:016x}\n", address, value);
  throw std::runtime_error("Unhandled MMIODevice::write_u64");
}

u8
MMIODevice::read_u8(const u32 address)
{
  fmt::print("Unhandled MMIODevice::read_u8 at address 0x{:08x}\n", address);
  throw std::runtime_error("Unhandled MMIODevice::read_u8");
}

u16
MMIODevice::read_u16(const u32 address)
{
  fmt::print("Unhandled MMIODevice::read_u16 at address 0x{:08x}\n", address);
  throw std::runtime_error("Unhandled MMIODevice::read_u16");
}

u32
MMIODevice::read_u32(const u32 address)
{
  fmt::print("Unhandled MMIODevice::read_u32 at address 0x{:08x}\n", address);
  throw std::runtime_error("Unhandled MMIODevice::read_u32");
}

u64
MMIODevice::read_u64(const u32 address)
{
  fmt::print("Unhandled MMIODevice::read_u64 at address 0x{:08x}\n", address);
  throw std::runtime_error("Unhandled MMIODevice::read_u64");
}

void
MMIODevice::read_dma(const u32 address, const u32 length, u8 *const dst)
{
  throw std::runtime_error("Attempt to read_dma from register-only device");
}

void
MMIODevice::write_dma(const u32 address, const u32 length, const u8 *const src)
{
  throw std::runtime_error("Attempt to write_dma to register-only device");
}

template<>
u8
MMIODevice::read<u8>(const u32 address)
{
  return read_u8(address);
}

template<>
u16
MMIODevice::read<u16>(const u32 address)
{
  return read_u16(address);
}

template<>
u32
MMIODevice::read<u32>(const u32 address)
{
  return read_u32(address);
}

template<>
u64
MMIODevice::read<u64>(const u32 address)
{
  return read_u64(address);
}

template<>
void
MMIODevice::write<u8>(const u32 address, u8 value)
{
  write_u8(address, value);
}

template<>
void
MMIODevice::write<u16>(const u32 address, u16 value)
{
  write_u16(address, value);
}

template<>
void
MMIODevice::write<u32>(const u32 address, u32 value)
{
  write_u32(address, value);
}

template<>
void
MMIODevice::write<u64>(const u32 address, u64 value)
{
  write_u64(address, value);
}

}
