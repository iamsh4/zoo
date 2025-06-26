#pragma once

#include <libchdr/chd.h>
#include "media/disc.h"

namespace zoo::media {

class CHDDisc final : public Disc {
private:
  chd_file* m_chd_file;

  std::vector<Track> m_tracks;
  std::vector<Session> m_sessions;

  /** Cache currently loaded hunk. */
  i32 m_hunknum;

  /** Currently loaded hunk data. */
  std::vector<u8> m_hunk_data;

public:
  CHDDisc(const char *path);
  ~CHDDisc();

  u32 get_chd_hunk_count() const;
  u32 read_chd_hunk(u32 hunknum, util::Span<u8> output) const;

  const std::vector<Track> &tracks() const final;
  const std::vector<Session> &get_toc() const final;
  SectorReadResult read_sector(u32 fad, util::Span<u8> output) final;
};

} // namespace zoo::media
