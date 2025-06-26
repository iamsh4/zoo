#include <array>
#include "gdrom_utilities.h"
#include "shared/string.h"

GDROMDiscMetadata
gdrom_disc_metadata(zoo::media::Disc *disc)
{
  GDROMDiscMetadata meta = {};
  if (!disc) {
    return meta;
  }

  std::array<u8, 2048 * 16> buffer;
  const u32 bytes_read = disc->read_bytes(150, buffer.size(), buffer);
  if (bytes_read != buffer.size()) {
    throw std::runtime_error("Failed to read GDROM metadata");
  }

  const auto read_string = [](const u8 *data, u8 offset, u8 len) {
    char tmp[1024] = { 0 };
    memcpy(tmp, data + offset, len);
    std::string result(tmp);
    rtrim(result);
    return result;
  };

  // 000-00F 	Hardware ID (always "SEGA SEGAKATANA ")
  // 010-01F 	Maker ID (always "SEGA ENTERPRISES")
  // 020-02F 	Device Information (see below)
  // 030-037 	Area Symbols (see below)
  // 038-03F 	Peripherals (see below)
  // 040-049 	Product number ("HDR-nnnn" etc.)
  // 04A-04F 	Product version
  // 050-05F 	Release date (YYYYMMDD)
  // 060-06F 	Boot filename (usually "1ST_READ.BIN")
  // 070-07F 	Name of the company that produced the disc
  // 080-0FF 	Name of the software

  meta.hardware_id     = read_string(buffer.data(), 0x00, 16);
  meta.maker_id        = read_string(buffer.data(), 0x10, 16);
  meta.device_info     = read_string(buffer.data(), 0x20, 16);
  meta.area_symbols    = read_string(buffer.data(), 0x30, 8);
  meta.peripherals     = read_string(buffer.data(), 0x38, 8);
  meta.product_number  = read_string(buffer.data(), 0x40, 10);
  meta.product_version = read_string(buffer.data(), 0x4A, 6);
  meta.release_date    = read_string(buffer.data(), 0x50, 8);
  meta.boot_filename   = read_string(buffer.data(), 0x60, 16);
  meta.company_name    = read_string(buffer.data(), 0x70, 16);
  meta.software_name   = read_string(buffer.data(), 0x80, 128);

  return meta;
}