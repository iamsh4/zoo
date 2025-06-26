#pragma once

#include <vector>

#include "fox/mmio_device.h"
#include "core/console.h"

namespace gpu {

class TextureFIFO : public fox::MMIODevice {
public:
  TextureFIFO(Console *console);

  void register_regions(fox::MemoryTable *memory) override;

  void write_dma(u32 addr, u32 length, const uint8_t *src) override;

private:
  static Log::Logger<Log::LogModule::GRAPHICS> log;
  Console *const m_console;
};

}
