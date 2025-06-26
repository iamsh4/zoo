#pragma once

#include <string>
#include <vector>
#include <memory>

#include "shared/types.h"
#include "./disc.h"

namespace zoo::media {

/*!
 * @brief Implementation of a generic disc that could be loaded into the
 *        emulated GDROM drive. Uses the GDI file format (similar to bin/cue)
 */
class GDIDisc : public Disc {
  /*!
   * @brief Read a single sector's data section  from the disc, returning the
   *        number of bytes read.
   */
  size_t read_sector_data(u32 absolute_lba, u8 *buffer) const;

  struct TrackInfo {
    u32 lba_start;
    u32 lba_length;
    bool is_audio_track = false;
  };

  /*!
   * @brief Return the list of tracks present on the disc.
   */
  std::vector<TrackInfo> read_toc() const;

  std::vector<Session> m_sessions;
  std::vector<Track> m_tracks;
  std::vector<FILE*> m_track_files;

public:
  GDIDisc(const std::string &file_path);
  ~GDIDisc();

  const std::vector<Track> &tracks() const override;
  const std::vector<Session> &get_toc() const override;
  SectorReadResult read_sector(u32 fad, util::Span<u8> output) override;
};

}
