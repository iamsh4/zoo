#pragma once

#include "systems/ps1/controllers/controller.h"

namespace zoo::ps1 {

class DigitalPad final : public Controller {
public:
  DigitalPad() : Controller(0x5a41) {}

  u8 handle_data(u8 data_in)
  {
    if (m_state == 0 && data_in == 0x01) {
      m_state++;
      return 0xff;
    } else if (m_state == 1 && data_in == 0x42) {
      // assert(false);
      m_state++;
      return m_device_id & 0xff;
    } else if (m_state == 2) {
      m_state++;
      return (m_device_id >> 8) & 0xff;
    } else if (m_state == 3) {
      m_state++;
      return (~m_digital_buttons) & 0xff;
    } else if (m_state == 4) {
      m_state++;
      return ((~m_digital_buttons) >> 8) & 0xff;
    } else {
      m_state = 0;
      return 0xff;
    }
  }
};

} // namespace zoo::ps1
