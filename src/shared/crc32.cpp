#include "shared/crc32.h"

// https://create.stephan-brumme.com/crc32/#tableless
/// compute CRC32 (byte algorithm) without lookup tables
u32
crc32(const void *data, u32 length, u32 previousCrc32)
{
  uint32_t crc = ~previousCrc32;
  const u8 *current = (const u8 *)data;
  while (length-- != 0) {
    u8 s = u8(crc) ^ *current++;
    u32 low = (s ^ (s << 6)) & 0xFF;
    u32 a = (low * ((1 << 23) + (1 << 14) + (1 << 2)));
    crc = (crc >> 8) ^ (low * ((1 << 24) + (1 << 16) + (1 << 8))) ^ a ^ (a >> 1) ^
          (low * ((1 << 20) + (1 << 12))) ^ (low << 19) ^ (low << 17) ^ (low >> 2);
  }
  return ~crc; // same as crc ^ 0xFFFFFFFF
}
