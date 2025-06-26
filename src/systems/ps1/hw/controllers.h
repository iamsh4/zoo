#pragma once

#include <deque>

#include "fox/mmio_device.h"
#include "shared/scheduler.h"
#include "shared/types.h"

namespace zoo::ps1 {
class Console;

class Controllers : public fox::MMIODevice {
private:
  Console *m_console;

  // Eve

  u16 m_joy_ctrl = 0;
  u16 m_joy_baud = 0;
  u16 m_joy_mode = 0;

  // Used to receive data from controllers
  std::deque<u8> m_rx_fifo;
  // Used to transmit data to controllers
  std::deque<u8> m_tx_fifo;

  // TODO: this doesn't support various controllers well. abstraction would be good. We
  // should be able to assign different devices to port 0 or 1 and get the right data
  // piped through

  // Sequence number for a basic digital controller (5 bytes total)
  u8 m_controller_seq = 0;

  enum class CurrentDevice
  {
    None,
    Controller,
    MemoryCard,
  };
  CurrentDevice m_current_device = CurrentDevice::None;
  u8 m_data = 0xff;
  u8 m_ack = 0;
  u8 m_rx_pending = 0;

  bool m_irq = false;
  EventScheduler::Event m_irq_event;
  void irq_event();

  void handle_input(u8 value);

public:
  Controllers(Console *console);

  u8 read_u8(u32 addr) override;
  u16 read_u16(u32 addr) override;
  u32 read_u32(u32 addr) override;

  void write_u8(u32 addr, u8 value) override;
  void write_u16(u32 addr, u16 value) override;
  void write_u32(u32 addr, u32 value) override;

  void register_regions(fox::MemoryTable *memory) override;
};
} // namespace zoo::ps1
