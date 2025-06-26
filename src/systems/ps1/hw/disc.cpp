#include <fmt/core.h>
#include <regex>
#include <fstream>

#include "shared/file.h"
#include "systems/ps1/hw/disc.h"

namespace zoo::ps1 {

static constexpr u32 kSectorSize = 2352;
static constexpr u32 kSectorsPerSecond = 75;

// Each bin file for a track skips 2 seconds of track.
static constexpr u32 kBinSectorsSkipped = 2 * kSectorsPerSecond;

Disc *
Disc::create(const char *path)
{
  if (strstr(path, ".cue") != nullptr) {
    return new CueBinDisc(path);
  } else if (strstr(path, ".bin") != nullptr) {
    return new CueBinDisc(path);
  } else {
    assert(false && "Unhandled Disc file extension");
    return nullptr;
  }
}

CueBinDisc::CueBinDisc(const char *path)
{
  if (strstr(path, ".cue") != nullptr) {
    init_from_cue(path);
  } else if (strstr(path, ".bin") != nullptr) {
    init_from_bin(path);
  } else {
    assert(false && "Invalid cue/bin file extension");
  }
}

void
CueBinDisc::init_from_cue(const char *cue_path)
{
  assert(std::filesystem::exists(cue_path));
  const auto cue_folder = std::filesystem::path(cue_path).parent_path();

  static const std::regex FILE_regex("\\s*FILE\\s+\"(.+)\"\\s+BINARY\\s*");
  static const std::regex TRACK_regex("\\s*TRACK\\s+(\\d+)\\s+(.+)\\s*");

  std::ifstream cue_file(cue_path);
  if (!cue_file.is_open()) {
    printf("disc: Could not open cue file '%s'\n", cue_path);
    return;
  }

  // Used to store parsed track data
  Track track = {};

  const auto close_pending_track = [&] {
    if (!track.file_path.empty()) {
      m_tracks.emplace_back(std::move(track));
    }
    track = {};
  };

  std::string line;
  std::smatch match;
  while (!cue_file.eof()) {
    std::getline(cue_file, line);

    if (std::regex_search(line, match, FILE_regex)) {
      close_pending_track();

      track.file_path = cue_folder / match[1].str();
      if (!std::filesystem::exists(track.file_path)) {
        fmt::print("disc: Reference bin file '{}' does not exist\n");
        m_tracks.clear();
        return;
      }

      std::ifstream track_file(track.file_path,
                               std::ifstream::ate | std::ifstream::binary);
      const size_t file_size = track_file.tellg();

      if (file_size % kSectorSize != 0) {
        fmt::print("disc: bin file '{}' has size {}, not a multiple of the expected "
                   "sector size {}\n",
                   track.file_path.string(),
                   file_size,
                   kSectorSize);
        m_tracks.clear();
        return;
      }

      track.num_sectors = kBinSectorsSkipped + file_size / kSectorSize;
    }

    else if (std::regex_search(line, match, TRACK_regex)) {
      const std::string track_num = match[1].str();
      const std::string track_type = match[2].str();

      fmt::print("disc: - Found track '{}', type '{}'\n", track_num, track_type);
      track.track_num = std::atoi(match[1].str().c_str());

      if (track_type == "AUDIO") {
        track.type = TrackType::Audio;
      } else if (track_type == "MODE2/2352") {
        track.type = TrackType::Mode2_2352;
      } else {
        fmt::print("disc: Unknown track type '{}'\n", track_type);
        m_tracks.clear();
        return;
      }
    }
  }

  // Add the last track we were parsing
  close_pending_track();

  // Ensure tracks are sorted
  std::sort(m_tracks.begin(), m_tracks.end(), [](const Track &a, const Track &b) {
    return a.track_num < b.track_num;
  });

  // Compute starting sector for each bin file
  u32 current_sector = 0;
  for (auto &t : m_tracks) {
    t.start_sector = current_sector;

    t.start_mm = t.start_sector / (60 * kSectorsPerSecond);

    const u32 track_minute_start_sector = t.start_mm * 60 * kSectorsPerSecond;
    const u32 sectors_into_minute = t.start_sector - track_minute_start_sector;
    t.start_ss = sectors_into_minute / kSectorsPerSecond;

    current_sector += t.num_sectors;
  }

  // Summary
  fmt::print("disc: Found {} tracks for '{}'\n", m_tracks.size(), cue_path);
  for (const auto &t : m_tracks) {
    fmt::print("disc: - '{}' (num={}, mode={}, sector_start={})\n",
               t.file_path.string(),
               t.track_num,
               static_cast<int>(t.type),
               t.start_sector);
  }
}

void
CueBinDisc::init_from_bin(const char *bin_path)
{
  assert(std::filesystem::exists(bin_path));
  m_tracks.clear();
  m_tracks.emplace_back(Track {
    .type = TrackType::Mode2_2352,
    .track_num = 1,
    .num_sectors = u32(get_file_size(bin_path)) / 2352,
    .start_sector = 0,
    .start_mm = 0,
    .start_ss = 0,
    .file_path = bin_path,
  });
}

void
CueBinDisc::read(Track &track, u32 offset, u32 size, u8 *dest)
{
  if (!track.file.is_open()) {
    track.file = std::ifstream(track.file_path, std::ios::binary);
  }
  track.file.seekg(offset);
  track.file.read((char *)dest, size);
}

void
CueBinDisc::read_sector_data(u8 minutes,
                             u8 seconds,
                             u8 sectors,
                             SectorReadMode sector_mode,
                             u8 *dest)
{
  // 74 minutes per disc
  // 60 seconds per minute
  // 75 sectors per second
  // 98 frames per sector
  // Each frame = 24B data + 1B subchannel + 8B error-correction

  // - We don't get access to subchannel/ecc data in bin/cue (or most other formats)
  // - So, one sector == (98*24 == 2352 bytes == 930h bytes)
  // - bin/cue format for data always stores these 930h byte representation.
  // - The beginning of each sector is actually some sync data, headers, etc. followed by
  //   the actual 800h bytes of real 'user' data.

  ////////////////////////////////////////////////

  const u32 sector_requested = (minutes * 60 + seconds) * 75 + sectors;

  Track *track = nullptr;
  for (auto &t : m_tracks) {
    if (sector_requested < t.start_sector + t.num_sectors) {
      track = &t;
      break;
    }
  }

  if (track == nullptr) {
    // Couldn't find a track containing this sector!
    // TODO
    assert(false);
  }

  // Debug: show sector header/subheader
  /*
  if (1) {
    fseek(m_file, address + 12, SEEK_SET);
    assert(8 == fread(dest, 1, 8, m_file));
    // clang-format off
    printf("cdrom: SECTOR: header=[");
    for(u32 i=0; i<4; ++i) printf("%02x,", dest[i]);
    printf("] sub-header=[");
    for(u32 i=4; i<8; ++i) printf("%02x,", dest[i]);
    printf("]\n");
    // clang-format on
  }
  */

  const u32 track_rel_sector = sector_requested - track->start_sector;

  if (track_rel_sector < kBinSectorsSkipped) {
    // The first 2 seconds of each track are not present in bin files, so we actually
    // don't have any data here...
    // XXX :
    memset(dest, 0, 0x800);
    return;
  }

  u32 read_offset = (track_rel_sector - kBinSectorsSkipped) * kSectorSize;
  u32 read_size = 0;

  if (sector_mode == SectorReadMode_800) {
    read_offset += 24;
    read_size = 0x800;
  } else if (sector_mode == SectorReadMode_924) {
    read_offset += 12;
    read_size = 0x924;
  } else {
    assert(false);
  }

  read(*track, read_offset, read_size, dest);

  fmt::print("QQQ : (mm,ss,sec)=({},{},{}) :: track {}\n",
             minutes,
             seconds,
             sectors,
             track->track_num);
}

u8
Track::start_mm_bcd() const
{
  return (start_mm / 10) * 16 + (start_mm % 10) * 1;
}

u8
Track::start_ss_bcd() const
{
  return (start_ss / 10) * 16 + (start_ss % 10) * 1;
}

} // namespace zoo::ps1
