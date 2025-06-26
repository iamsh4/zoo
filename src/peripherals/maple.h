#pragma once

#include <thread>
#include <memory>

#include "fox/mmio_device.h"
#include "guest/sh4/sh4.h"
#include "shared/fifo_engine.h"
#include "shared/log.h"
#include "peripherals/device.h"

class Console;

struct VMU_LCD {
  u8 lcd_data[48 * 32];                   // 48 cols * 32 rows, top-down-left-right order
  static const u8 lcd_dot_max_levels = 1; // Max gradations
  static constexpr int n_rows = 32;
  static constexpr int n_cols = 48;

  // TODO : Handle multiple planes/gradients
  float get_dot_level(u8 row, u8 col) const
  {
    assert(row <= n_rows && col <= n_cols && lcd_dot_max_levels == 1);
    u8 byte = lcd_data[row * n_cols + col];
    return byte ? 1.0f : 0.0f;
  }
};

class Maple : public fox::MMIODevice {
public:
  Maple(Console *console);
  ~Maple();

  void reset();
  void register_regions(fox::MemoryTable *memory) override;

  void add_device(unsigned port, std::shared_ptr<maple::Device> device);

private:
  enum FifoCommands
  {
    ButtonDown = 100,
    ButtonUp = 101,
    TriggerLeft = 102,
    TriggerRight = 103,
    JoystickX = 104,
    JoystickY = 105,
  };

  Log::Logger<Log::LogModule::MAPLE> log;

  /* Emulator State */
  Console *const m_console;
  fox::MemoryTable *const m_memory;
  FifoEngine<u32> *m_engine;

  /* Registers */
  std::atomic<u32> m_MDST;
  u32 m_MDSTAR;

  /* Attached Devices */
  std::shared_ptr<maple::Device> m_devices[4];

  u32 read_u32(u32 addr) override;
  void write_u32(u32 addr, u32 val) override;

  bool read_command_file(fox::MemoryTable *mem, u32 *address);

  void engine_callback(u32 address, const u32 &value);
};
