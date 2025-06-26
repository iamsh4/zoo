#pragma once

#include <memory>
#include <cassert>
#include "peripherals/device.h"

namespace maple {

class Controller : public Device {
public:
  enum class Button
  {
    A = 0,
    B,
    X,
    Y,
    Start,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    N_Buttons
  };

  Controller();
  ~Controller();

  void button_down(Button button);
  void button_up(Button button);

  void trigger_left(float value);
  void trigger_right(float value);

  void joystick_x(float value);
  void joystick_y(float value);

  void add_device(unsigned slot, std::shared_ptr<Device> device);

  std::shared_ptr<Device> get_device(const unsigned slot)
  {
    /* TODO Locking */
    assert(slot < 2u);
    return m_slots[slot];
  }

  ssize_t identify(const Header *in, Header *out, u8 *buffer) override;
  ssize_t run_command(const Packet *in, Packet *out) override;
  void reset() override;

private:
  union {
    struct {
      /* Button Bitmask */
      uint16_t reserved1 : 1;
      uint16_t button_b : 1;
      uint16_t button_a : 1;
      uint16_t button_start : 1;
      uint16_t dpad_up : 1;
      uint16_t dpad_down : 1;
      uint16_t dpad_left : 1;
      uint16_t dpad_right : 1;
      uint16_t reserved2 : 1;
      uint16_t button_y : 1;
      uint16_t button_x : 1;
      uint16_t reserved3 : 5;

      /* Fixed Offset (0x80 = center) byte fields */
      u8 trigger_right;
      u8 trigger_left;
      u8 joystick_x;
      u8 joystick_y;
      u8 altjoystick_x;
      u8 altjoystick_y;
    };

    uint8_t raw[8];
  } m_status_data;

  std::shared_ptr<Device> m_slots[2];
};

}
