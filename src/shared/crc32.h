#pragma once

#include "shared/types.h"

// https://create.stephan-brumme.com/crc32/#tableless
/// compute CRC32 (byte algorithm) without lookup tables
u32 crc32(const void *data, u32 length, u32 previousCrc32 = 0);
