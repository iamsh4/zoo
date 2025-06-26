#pragma once

#include <memory>
#include <vector>

#include "shared/span.h"
#include "shared/types.h"

namespace zoo::media {

enum SectorSize
{
  // Data-only
  SectorSize_2048 = 2048,
  // Entire Sector, composed of 98 frames * 24B of user data in each frame
  SectorSize_2352 = 2352,
};

enum SectorMode
{
  SectorMode_Audio,
  SectorMode_Mode0,
  SectorMode_Mode1,
};

struct SectorLayout {
  u32 mode;
  u32 size;

  u32 header_size() const
  {
    if (size == SectorSize_2048) {
      return 0;
    } else if (size == SectorSize_2352) {
      return 16;
    } else {
      throw std::runtime_error("Unsupported sector size");
    }
  }
};

struct Track {
  u32 number;

  /** Frame address for first sector of this track within the entire disc. */
  u32 fad;

  u32 num_sectors;

  SectorLayout sector_layout;

  /** Sector offset into the backing file where this track begins. */
  u32 backing_fad;

  bool is_audio_track() const
  {
    return sector_layout.mode == SectorMode_Audio;
  }
};

struct Session {
  u32 track_first;
  u32 track_last;
  u32 fad_leadin;
  u32 fad_leadout;
};

struct MSF {
  u8 minutes;
  u8 seconds;
  u8 frames;
};

struct SectorReadResult {
  u32 track_num;
  SectorLayout sector_layout;
  u32 bytes_read;
};

class Disc {
public:
  virtual ~Disc() = default;
  
  virtual const std::vector<Track> &tracks() const                     = 0;
  virtual const std::vector<Session> &get_toc() const                  = 0;
  virtual SectorReadResult read_sector(u32 fad, util::Span<u8> output) = 0;

  static std::shared_ptr<Disc> open(const char *path);

  u32 read_bytes(u32 sector, u32 num_bytes, util::Span<u8> output);

  u32 load_file(const char* file_name, util::Span<u8> destination);
};

} // namespace zoo::media
