#pragma once

#include <string>
#include <memory>
#include "peripherals/device.h"

namespace maple {

class VMU : public Device {
public:
  static constexpr size_t LCD_WIDTH = 48u;
  static constexpr size_t LCD_HEIGHT = 32u;

  VMU(const std::string &filename);
  ~VMU();

  const u8 *lcd_pixels() const
  {
    return m_lcd_pixels.get();
  }

  ssize_t identify(const Header *in, Header *out, u8 *buffer) override;
  ssize_t run_command(const Packet *in, Packet *out) override;
  void reset() override;

private:
  MediaInfo m_info;
  std::unique_ptr<u8[]> m_lcd_pixels;
  u8 *m_flash;
};

}
