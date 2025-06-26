#include <cstdlib>
#include <cstring>
#include <cassert>

#include "./gdi_disc.h"
#include "shared/log.h"
#include "shared/types.h"
#include "shared/error.h"

namespace zoo::media {

static Log::Logger<Log::LogModule::GDROM> log;

u32
get_track_file_sector_count(FILE *fp, u32 sector_size)
{
  fseek(fp, 0, SEEK_END);
  const u64 file_size = ftell(fp);
  assert(file_size % sector_size == 0);
  assert(file_size < UINT32_MAX);
  return file_size / sector_size;
}

#if 0
void
DiscTrack::read_sector_data(u32 absolute_lba, u8 *buffer) const
{
  printf("read_sector_data lba=%u\n", absolute_lba);
  assert(absolute_lba >= m_lba_start);
  assert(absolute_lba < (m_lba_start + m_lba_count));

  if (m_track_type == TrackType::Data) {
    const u32 file_offset =
      (absolute_lba - m_lba_start) * m_sector_size + m_sector_header_size;
    const int seek_result = fseek(m_fp, file_offset, SEEK_SET);
    assert(seek_result == int(file_offset) || seek_result == 0);

    /* The GDROM sector itself is 2352 = (16 byte header + 2048 byte data + 288 byte ECC).
     * We just need the data portion here for a read. The rest is transparent to the
     * system when reading a sector. */
    const size_t read_result = fread(buffer, 1, m_sector_data_size, m_fp);
    assert(read_result == m_sector_data_size);
  } else if (m_track_type == TrackType::Audio) {
    const u32 file_offset = (absolute_lba - m_lba_start) * m_sector_size;
    const int seek_result = fseek(m_fp, file_offset, SEEK_SET);
    assert(seek_result == int(file_offset) || seek_result == 0);

    // In Red Book Audio tracks, the entire 2352 bytes of a sector contribute to the audio
    // samples, with no ECC etc.
    const size_t read_result = fread(buffer, 1, m_sector_size, m_fp);
    assert(read_result == m_sector_size);
  } else
    assert(0);
}
#endif

/******************************************************************************/

GDIDisc::GDIDisc(const std::string &file_path)
{
  log.info("Loading GDI format disc '%s'", file_path.c_str());

  /* Determine parent directory of the file */
  const size_t last_slash = file_path.find_last_of("/\\");
  std::string parent_directory;
  if (last_slash != std::string::npos) {
    parent_directory = file_path.substr(0, last_slash + 1);
  }

  FILE *const fp = fopen(file_path.c_str(), "r");
  if (fp == NULL) {
    log.error("Could not open file '%s'", file_path.c_str());
    return;
  }

  /* First line: Number of track entries in file */
  u32 total_track_count;
  if (fscanf(fp, "%u\n", &total_track_count) < 0)
    throw std::runtime_error("Failed to read track count");

  /*
   * Remaining lines:
   *   <tracknum> <lba_start> <type> <sector_size> <filename> <file_offset>
   */
  while (!feof(fp)) {
    char buffer[1024];
    if (fgets(buffer, sizeof(buffer), fp) == nullptr)
      break;

    /* Empty line */
    if (strlen(buffer) < 5) {
      continue;
    }

    const char *const track_number_str = strtok(buffer, "\r\n\t ");
    assert(track_number_str != NULL);
    const u32 track_number = atoi(track_number_str);
    (void)track_number;

    const char *const lba_start_str = strtok(NULL, "\r\n\t ");
    assert(lba_start_str != NULL);
    const u32 track_fad = atoi(lba_start_str) + 150;

    const char *const type_str = strtok(NULL, "\r\n\t ");
    assert(type_str != NULL);
    const u32 track_type_index = atoi(type_str);

    SectorLayout sector_layout;
    switch (track_type_index) {
      case 0:
        sector_layout =
          SectorLayout { .mode = SectorMode::SectorMode_Audio, .size = SectorSize_2352 };
        break;
      case 4:
        sector_layout =
          SectorLayout { .mode = SectorMode::SectorMode_Mode1, .size = SectorSize_2352 };
        break;
      default:
        printf("Unhandled GDI track sector type %u\n", track_type_index);
        abort();
        break;
    }

    const char *const sector_size_str = strtok(NULL, "\r\n\t ");
    assert(sector_size_str != NULL);
    const u32 sector_size = atoi(sector_size_str);

    /* Filename is tricky.. can contain spaces if quoted */
    char *latter_buffer = strtok(NULL, "");
    while (*latter_buffer) {
      const char c = *latter_buffer;
      if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
        break;
      }
      ++latter_buffer;
    }
    assert(*latter_buffer != '\0');

    const char *filename;
    if (*latter_buffer == '"') {
      ++latter_buffer;

      filename = latter_buffer;
      while (*latter_buffer) {
        /* TODO can there be escaped quotes inside quotes? */
        if (*latter_buffer == '"') {
          *latter_buffer = '\0';
          ++latter_buffer;
          break;
        }

        ++latter_buffer;
      }
    } else {
      filename = strtok(latter_buffer, "\r\n\t ");
      latter_buffer = NULL;
    }

    /* TODO handle non-zero offsets */
    const char *const file_offset_str = strtok(latter_buffer, "\r\n\t ");
    assert(file_offset_str != NULL);
    const u32 file_offset = atoi(file_offset_str);
    assert(file_offset == 0); (void)file_offset;

    const std::string track_file_path = parent_directory + filename;
    FILE *track_file = fopen(track_file_path.c_str(), "rb");
    m_track_files.push_back(track_file);

    Track track = {};
    track.backing_fad = 0;
    track.fad = track_fad;
    track.num_sectors = get_track_file_sector_count(track_file, sector_size);
    track.number = m_tracks.size() + 1;
    track.sector_layout = sector_layout;
    m_tracks.push_back(track);

    printf("Track '%s' loaded: sector size %u, start fad %u, type %u\n",
           filename,
           sector_size,
           track_fad,
           track_type_index);
  }

  assert(total_track_count == m_tracks.size());
}

GDIDisc::~GDIDisc()
{
  for (FILE *fp : m_track_files) {
    if (fp) {
      fclose(fp);
    }
  }
}

const std::vector<Track> &
GDIDisc::tracks() const
{
  return m_tracks;
}

const std::vector<Session> &
GDIDisc::get_toc() const
{
  return m_sessions;
}

SectorReadResult
GDIDisc::read_sector(u32 fad, util::Span<u8> output)
{
  for (const Track &track : m_tracks) {
    const u32 track_start = track.fad;
    const u32 track_end = track_start + track.num_sectors;
    if (fad >= track_start && fad < track_end) {

      const u32 track_file_index = track.number - 1;
      assert(track_file_index < m_track_files.size());

      const u32 track_offset = fad - track_start;
      // printf("read_sector(fad=%u) track_file_index %u track_start %u track_end %u "
      //        "track_offset %u\n",
      //        fad,
      //        track_file_index,
      //        track_start,
      //        track_end,
      //        track_offset);

      FILE *fp = m_track_files[track_file_index];
      fseek(fp, track.sector_layout.size * track_offset, SEEK_SET);
      fread(output.ptr, track.sector_layout.size, 1, fp);

      SectorReadResult result = {};
      result.bytes_read       = track.sector_layout.size;
      result.track_num        = track.number;
      result.sector_layout    = track.sector_layout;
      return result;
    }
  }

  return SectorReadResult {0};
}

}
