#pragma once

#include "fox/mmio_device.h"
#include "shared/types.h"

namespace zoo::ps1 {

class Console;

class MDEC : public fox::MMIODevice {
private:
  union Status {
    u32 raw;
  } m_status = {};

  enum class Command {
    None,
    DecodeMacroblocks,
    SetQuantTables,
    SetScaleTable
  };
  Command m_current_command = Command::None;
  std::vector<u32> m_command_data;
  u32 m_remaining_params = 0;

  Console *m_console;

public:
  MDEC(Console *console);

  u8 read_u8(u32 addr) override;
  u16 read_u16(u32 addr) override;
  u32 read_u32(u32 addr) override;

  void write_u8(u32 addr, u8 value) override;
  void write_u16(u32 addr, u16 value) override;
  void write_u32(u32 addr, u32 value) override;

  void handle_command(u32 value);

  void register_regions(fox::MemoryTable *memory) override;
};

} // namespace zoo::ps1
