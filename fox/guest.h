// vim: expandtab:ts=2:sw=2

#pragma once

#include "fox/fox_types.h"

namespace fox {

/*!
 * @class fox::Guest
 * @brief Virtual interface that should be implemented for CPUs using foxjit.
 */
class Guest {
public:
  virtual ~Guest();

  /* Register indexes are limited to the range of u16. */
  virtual Value guest_register_read(unsigned index, size_t bytes) = 0;
  virtual void guest_register_write(unsigned index, size_t bytes, Value value) = 0;

  /* Loads and stores are always 1/2/4/8 bytes. */
  virtual Value guest_load(u32 address, size_t bytes) = 0;
  virtual void guest_store(u32 address, size_t bytes, Value value) = 0;
};

}
