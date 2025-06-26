#pragma once

#include "shared/types.h"

namespace zoo::ps1 {

class Controller {
protected:
  const u16 m_device_id;

  u8 m_state = 0;

  // 1=pressed, 0=not pressed. Note this is opposite the transfer value
  u32 m_digital_buttons = 0;

public:
  Controller(u16 device_id) : m_device_id(device_id) {}
  virtual ~Controller() {}

  void reset_state()
  {
    m_state = 0;
  }

  // Controller always goes into 0 state after unexpected/bad input, so after handle_data,
  // ack can be called to see if the controller ackwnowledges.
  bool ack()
  {
    return m_state != 0;
  }

  virtual u8 handle_data(u8 data_in) = 0;

  enum Button
  {
    Select,
    L3,
    R3,
    Start,
    JoypadUp,
    JoypadRight,
    JoypadDown,
    JoypadLeft,
    L2,
    R2,
    L1,
    R1,
    Triangle,
    Circle,
    Cross,
    Square,
  };

  void set_button(Button button, bool is_pressed)
  {
    const u32 bit = 1u << button;
    m_digital_buttons &= ~bit;
    m_digital_buttons |= is_pressed ? bit : 0;
  }
};

} // namespace zoo::ps1
