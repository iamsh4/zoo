#pragma once

#include "fox/fox_types.h"
#include "fox/memtable.h"

namespace fox {

/*!
 * @class fox::MMIODevice
 * @brief Virtual base class for implementing MMIO devices. The instance can
 *        be registered with a fox::MemoryTable and the virtual methods will
 *        be dispatched to handle memory reads / writes in the assigned region.
 */
class MMIODevice {
public:
  MMIODevice();
  virtual ~MMIODevice();

  virtual u8 read_u8(u32 addr);
  virtual u16 read_u16(u32 addr);
  virtual u32 read_u32(u32 addr);
  virtual u64 read_u64(u32 addr);

  virtual void write_u8(u32 addr, u8 value);
  virtual void write_u16(u32 addr, u16 value);
  virtual void write_u32(u32 addr, u32 value);
  virtual void write_u64(u32 addr, u64 value);

  virtual void read_dma(u32 addr, u32 length, uint8_t *dst);
  virtual void write_dma(u32 addr, u32 length, const uint8_t *src);

  /*!
   * @brief Wrapper for read callbacks from templated context
   */
  template<typename T>
  T read(uint32_t address);

  /*!
   * @brief Wrapper for write callbacks from templated context
   */
  template<typename T>
  void write(uint32_t address, T value);

  virtual void register_regions(MemoryTable *memory) = 0;
};

}
