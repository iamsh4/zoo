#pragma once

#include <functional>
#include <vector>
#include "shared/types.h"

namespace zoo {

struct MMIORegistry {
  using MessageFunc = std::function<bool(std::vector<char> *buffer)>;

  struct MMIORegister {
    const char *category;
    const char *name;
    u8 *host_ptr;
    u8 size;
    MessageFunc message;
  };

  std::vector<MMIORegister> m_registers;

  // void setup(const char *name, u32 guest_address, u8 *host_ptr, u8 size);
  template<typename T>
  void setup(const char *category,
             const char *name,
             T *host_ptr,
             MessageFunc message_func = {})
  {
    // XXX : assert isn't already registered
    m_registers.push_back({
      .category = category,
      .name = name,
      .host_ptr = (u8 *)host_ptr,
      .size = sizeof(T),
      .message = message_func,
    });
  }
};

} // namespace zoo::ps1
