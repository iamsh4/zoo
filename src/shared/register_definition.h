#pragma once

#include "shared/types.h"

#define REGISTER(_address, _name, _description)                                          \
  struct _name {                                                                         \
    static constexpr u32 address = _address;                                             \
    static const constexpr char *name = #_name;                                          \
    static const constexpr char *description = _description;                             \
  };
