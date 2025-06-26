#pragma once

#include "shared/types.h"

template<typename T = u32>
struct RegisterState {
  const char *name;
  const u32 address;
  const T value;
  const char *description;
};
