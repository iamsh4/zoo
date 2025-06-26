#pragma once

#include <string>
#include <memory>
#include "peripherals/device.h"

namespace maple {

class Keyboard : public Device {
public:
  Keyboard();
  ~Keyboard();

  ssize_t identify(const Header *in, Header *out, u8 *buffer) override;
  ssize_t run_command(const Packet *in, Packet *out) override;
  void reset() override;
};

}
