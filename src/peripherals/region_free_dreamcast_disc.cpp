
#include "region_free_dreamcast_disc.h"

using namespace zoo::media;

RegionFreeDreamcastDisc::RegionFreeDreamcastDisc(std::shared_ptr<zoo::media::Disc> disc)
  : m_underlying(disc)
{
}

const std::vector<zoo::media::Track> &
RegionFreeDreamcastDisc::tracks() const
{
  return m_underlying->tracks();
}

const std::vector<zoo::media::Session> &
RegionFreeDreamcastDisc::get_toc() const
{
  return m_underlying->get_toc();
}

#include <algorithm>

SectorReadResult
RegionFreeDreamcastDisc::read_sector(u32 fad, util::Span<u8> output)
{
  SectorReadResult result = m_underlying->read_sector(fad, output);
  // u32 result = m_underlying->read_sector(fad, output);

  const u32 sector_header_size = 16;

  // Patch the area symbols to mimic that this disc is compatible with all regions.
  // http://mc.pp.se/dc/ip.bin.html

  // On GDROM Discs (which are all we support at the moment), there are 45000 sectors
  // comprising the first session, and 150 of "pre-gap" before useful data begins in the
  // high-density second session. See the GDROM specification for more detail. The
  // IP0000.BIN Metadata starts at the beginning of the high density session's first
  // track.

  // In the IP0000.BIN data, there are eight total characters that determine playable
  // region for this disc. There are only three valid ones however: Japan, USA, Europe.
  // Mark them all enabled.
  const u32 hd_session_start = 45000 + 150;
  if (fad == hd_session_start) {
    memcpy(&output.ptr[sector_header_size + 0x30], "JUE", 3);
  }

  // We also need to patch the "area symbols" which come after the boot code. There are
  // three 32-byte slots. Each 32 slot has 4 bytes of instructions
  // followed by a 28 character string which must match the console region in order to
  // boot. Here we patch all three so that it would be accepted in any region bios.
  const u32 area_data_offset = 0x3700;
  const u32 sector_data_size = 2048;

  const u32 area_sector_num = area_data_offset / sector_data_size;
  const u32 gdrom_area_symbols_fad = hd_session_start + area_sector_num;

  if (fad == gdrom_area_symbols_fad) {
    const u32 area_sector_offset =
      (area_data_offset % sector_data_size) + sector_header_size;
    char *area_data = (char *)&output.ptr[area_sector_offset];
    memcpy(&area_data[0 * 32 + 4], "For JAPAN,TAIWAN,PHILIPINES.", 28);
    memcpy(&area_data[1 * 32 + 4], "For USA and CANADA.         ", 28);
    memcpy(&area_data[2 * 32 + 4], "For EUROPE.                 ", 28);
  }

  return result;
}
