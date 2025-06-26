#include <memory>
#include <unordered_map>
#include <fmt/core.h>

#include "arm64/arm64_logical_immediates.h"
#include "utils/error.h"

namespace fox {
namespace codegen {
namespace arm64 {

// In "arm64"/aarch64, it's possible to encode immediate values for many instructions. The
// set of valid immediates is very limited though. The purpose of this file is to generate
// all valid encodings once during startup, and provide the mapping of immediate values to
// the various encoding fields that are packed into instruction data.

std::unique_ptr<std::unordered_map<u64, LogicalImmediates::Encoding>>
  logical_imm_encodings;

u8
get_imms(unsigned size, unsigned length)
{
  return ((0b111100 << size) & 0xb111111) | length;
}

void
init_mapping()
{
  if (logical_imm_encodings)
    return;

  logical_imm_encodings =
    std::make_unique<std::unordered_map<u64, LogicalImmediates::Encoding>>();

  // https://gist.github.com/dinfuehr/9e1c2f28d0f912eae5e595207cb835c2
  for (u64 size_ = 1; size_ <= 6; ++size_) {
    u64 size = 1 << size_;

    for (u64 length = 0; length < size - 1; ++length) {
      u64 result = (1llu << (length + 1)) - 1;

      u64 e = size;
      while (e < 64) {
        result = result | (result << e);
        e *= 2;
      }

      for (u64 rotation = 0; rotation < size; ++rotation) {
        u8 n    = size == 64 ? 1 : 0;
        u8 immr = rotation;
        u8 imms = get_imms(size_, length);

        logical_imm_encodings->insert({ result, { .N = n, .immr = immr, .imms = imms } });
        result = (((result & 1) << 63) | (result >> 1)) & 0xFFFFFFFFFFFFFFFF;
      }
    }
  }
}

LogicalImmediates::LogicalImmediates()
{
  init_mapping();
}

LogicalImmediates::Encoding
LogicalImmediates::get_imm64(u64 value) const
{
  const auto &table = *logical_imm_encodings;
  const auto it     = table.find(value);
  if (it == table.end()) {
    fmt::print("arm64: invalid immediate 0x{:016x}\n", value);
    assert(false && "Invalid immediate constant");
  }
  return it->second;
}

LogicalImmediates::Encoding
LogicalImmediates::get_imm32(u32 value) const
{
  u64 search = u64(value) | (u64(value) << 32);
  return get_imm64(search);
}

bool
LogicalImmediates::has_imm64(u64 value) const
{
  return logical_imm_encodings->find(value) != logical_imm_encodings->end();
}

bool
LogicalImmediates::has_imm32(u32 value) const
{
  u64 search = u64(value) | (u64(value) << 32);
  return logical_imm_encodings->find(search) != logical_imm_encodings->end();
}

}
}
}
