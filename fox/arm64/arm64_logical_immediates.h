#pragma once

#include "fox/fox_types.h"

namespace fox {
namespace codegen {
namespace arm64 {

class LogicalImmediates {
public:
  struct Encoding {
    u8 N;
    u8 immr;
    u8 imms;
  };

  LogicalImmediates();
  Encoding get_imm64(u64 value) const;
  bool has_imm64(u64 value) const;

  Encoding get_imm32(u32 value) const;
  bool has_imm32(u32 value) const;
};

}
}
}
