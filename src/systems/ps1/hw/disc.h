#pragma once

#include <fstream>
#include <filesystem>

#include "shared/types.h"
#include "shared/file.h"

// TODO : This is really generic. We should ideally have Bin/Cue support zoo-wide.

namespace zoo::ps1 {

enum SectorReadMode
{
  // Data-only
  SectorReadMode_800,
  // Skip sync, everything else is given back
  SectorReadMode_924,
};

enum class TrackType
{
  Mode2_2352,
  Audio,
};

struct SectorAddress {
  u8 minute;
  u8 second;
  u8 sector;
};

struct Track {
  TrackType type;
  u8 track_num;
  u32 num_sectors;

  // Starting absolute sector on disc
  u32 start_sector;
  // Starting minute disc
  u32 start_mm;
  // Starting second within minute
  u32 start_ss;

  std::filesystem::path file_path;
  std::ifstream file;

  u8 start_mm_bcd()const;
  u8 start_ss_bcd()const;
};

class Disc {
protected:
  std::vector<Track> m_tracks;

public:
  static Disc *create(const char *path);

  const std::vector<Track> &tracks() const
  {
    return m_tracks;
  }

  virtual void read_sector_data(u8 minute,
                                u8 second,
                                u8 sector,
                                SectorReadMode,
                                u8 *dest) = 0;
};

class CueBinDisc final : public Disc {
private:
  void init_from_cue(const char *path);
  void init_from_bin(const char *path);

  void read(Track &, u32 offset, u32 size, u8 *dest);

public:
  CueBinDisc(const char *path);
  void read_sector_data(u8 minute, u8 second, u8 sector, SectorReadMode, u8 *dest);
};

};
