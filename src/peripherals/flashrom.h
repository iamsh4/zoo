#pragma once

#include <array>
#include <filesystem>

#include "fox/mmio_device.h"
#include "serialization/serializer.h"
#include "shared/types.h"

class Console;

class FlashROM : public fox::MMIODevice, public serialization::Serializer {
public:

FlashROM(Console*, std::filesystem::path);
 ~FlashROM();

 void reset();

  u8 read_u8(u32 addr) override;
  u16 read_u16(u32 addr) override;
  u32 read_u32(u32 addr) override;
  u64 read_u64(u32 addr) override;
  void write_u8(u32 addr, u8 value) override;
  void write_u16(u32 addr, u16 value) override;
  void write_u32(u32 addr, u32 value) override;
  void write_u64(u32 addr, u64 value) override;
  void read_dma(u32 addr, u32 length, uint8_t *dst) override;
  void write_dma(u32 addr, u32 length, const uint8_t *src) override;

  void serialize(serialization::Snapshot &snapshot) override;
  void deserialize(const serialization::Snapshot &snapshot) override;

private:
  void register_regions(fox::MemoryTable *memory) override;

  void load_from_file();
  void save_to_file();

  Console *m_console;
  std::filesystem::path m_file_path;

  enum class Mode : u8 {
    Normal,
    Program,
  };

  static const size_t kDataSize = 0x20000;
  std::array<u8, kDataSize> m_data;
  u8 m_write_cycle;
  Mode m_mode;
};
