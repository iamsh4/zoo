#pragma once

#include "disc.h"
#include <string>

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

struct GDROMDiscMetadata {
  std::string hardware_id;
  std::string maker_id;
  std::string device_info;
  std::string area_symbols;
  std::string peripherals;
  std::string product_number;
  std::string product_version;
  std::string release_date;
  std::string boot_filename;
  std::string company_name;
  std::string software_name;
};

GDROMDiscMetadata gdrom_disc_metadata(zoo::media::Disc *disc);

