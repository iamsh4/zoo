#include "gpu/texture_fifo.h"

using namespace gpu;

Log::Logger<Log::LogModule::GRAPHICS> TextureFIFO::log;

TextureFIFO::TextureFIFO(Console *console) : m_console(console)
{
  return;
}

void
TextureFIFO::register_regions(fox::MemoryTable *const memory)
{
  // memory->map_mmio(0x11000000u, 0x01000000u, "TA Texture FIFO", this);
}

void
TextureFIFO::write_dma(u32 addr, u32 length, const uint8_t *src)
{
  m_console->memory()->dma_write(0x04000000u + (addr & 0x00ffffff), src, length);
}
