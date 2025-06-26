#include "media/chd_disc.h"
#include "shared/utils.h"

#include "iso9660.h"

namespace zoo::media {

CHDDisc::CHDDisc(const char *path)
{
  chd_error err;
  err = chd_open(path, CHD_OPEN_READ, NULL, &m_chd_file);
  if (err) {
    printf("Failed to open CHD: %s\n", chd_error_string(err));
    throw std::exception(); // XXX
  }

  // Determine hunk size in this CHD, allocate space.
  {
    const chd_header *header = chd_get_header(m_chd_file);
    m_hunk_data.resize(header->hunkbytes);
    m_hunknum = -1;
  }

  // The first track will start after a 2 second pregap
  const u32 gdrom_pregap_sectors = 150;
  u32 track_start_fad            = gdrom_pregap_sectors;

  // The CHD file itself starts at the beginning of the available data
  u32 backing_fad = 0;

  char tmp[512], type[64], subtype[64], pgtype[64], pgsub[64];
  for (u32 tracki = 0; tracki < 100; ++tracki) {

    err = chd_get_metadata(m_chd_file,
                           GDROM_TRACK_METADATA_TAG,
                           tracki,
                           tmp,
                           sizeof(tmp),
                           nullptr,
                           nullptr,
                           nullptr);

    if (err == CHDERR_NONE) {
      int meta_track, pad, pregap, postgap, num_sectors;
      sscanf(tmp,
             GDROM_TRACK_METADATA_FORMAT,
             &meta_track,
             type,
             subtype,
             &num_sectors,
             &pad,
             &pregap,
             pgtype,
             pgsub,
             &postgap);

      Track track {};

      if (!strcmp(type, "AUDIO")) {
        track.sector_layout = SectorLayout {
          .mode = SectorMode_Audio,
          .size = 2352,
        };
      } else if (!strcmp(type, "MODE1_RAW")) {
        track.sector_layout = SectorLayout {
          .mode = SectorMode_Mode1,
          .size = 2352,
        };
      } else {
        // XXX : error unhandled type
        printf("Unhandled CHD track type %s\n", type);
        exit(1);
      }

      track.num_sectors = num_sectors;
      track.fad         = track_start_fad;
      track.number      = meta_track;
      track.backing_fad = backing_fad;
      m_tracks.push_back(track);

      assert((tracki + 1) == track.number);
      // printf("- Track %2u, fad %u backing_fad %u Type %-9s Sector Count %6u\n",
      //        track.number,
      //        track.fad,
      //        track.backing_fad,
      //        type,
      //        track.num_sectors);

      // CHD always stores tracks in multiple of 4 sectors (TODO: really?)
      backing_fad += round_up(num_sectors, 4);
      track_start_fad += num_sectors;

    } else {
      // End of disc
      break;
    }
  }

  // Relevant details of the GDROM Disc format can be found in Gdfm_k214e.doc

  // The fist session on the disc is low-density, ALWAYS contains one data track
  // and one audio track warning the user not to try to listen to the disc in
  // a typical CD player.
  Session low_density_session;
  low_density_session.track_first = 0;
  low_density_session.track_last  = 1;
  low_density_session.fad_leadin  = 0;
  low_density_session.fad_leadout = 0; // TODO

  // At MSF 10:00:00 the high density data starts, it always begins with a data
  // track, but can also be followed by redbook audio, and then a data track at
  // the end.
  Session high_density_session;
  high_density_session.track_first = 2;
  high_density_session.track_last  = m_tracks.size() - 1;
  high_density_session.fad_leadin =
    150 + m_tracks[0].num_sectors + m_tracks[1].num_sectors;
  high_density_session.fad_leadout = 0; // TODO

  m_sessions = { low_density_session, high_density_session };
}

CHDDisc::~CHDDisc()
{
  chd_close((chd_file *)m_chd_file);
}

const std::vector<Track> &
CHDDisc::tracks() const
{
  return m_tracks;
}

const std::vector<Session> &
CHDDisc::get_toc() const
{
  return m_sessions;
}

u32
CHDDisc::get_chd_hunk_count() const
{
  return chd_get_header((chd_file *)m_chd_file)->hunkcount;
}

u32
CHDDisc::read_chd_hunk(u32 hunknum, util::Span<u8> output) const
{
  chd_read((chd_file *)m_chd_file, hunknum, output.ptr);
  return chd_get_header((chd_file *)m_chd_file)->hunkbytes;
}

SectorReadResult
CHDDisc::read_sector(u32 fad, util::Span<u8> output)
{
  const chd_header *const header = chd_get_header(m_chd_file);

  for (const Track &track : m_tracks) {

    const u32 track_fad_first = track.fad;
    const u32 track_fad_last  = track_fad_first + track.num_sectors;

    if (fad >= track_fad_first && fad < track_fad_last) {
      u32 fad_in_track = fad - track_fad_first;

      // On Dreamcast, CHD audio tracks have a 2 second pregap which GDIs do not.
      // For now, we'll just add this to the fad to get the correct sector skipping
      // the 2 seconds of silence.
      // TODO : Unify this somewhere else since CHD discs may differ in this regard
      // for different platforms.
      if (track.is_audio_track()) {
        fad_in_track += 150;
      }

      const u32 fad_within_hunkseq = fad_in_track + track.backing_fad;

      // CHDs will contain the sector data (2352B), but may also contain
      // 96 bytes of useful subchannel data as well. Regardless of what is
      // actually present beyond the 2352 bytes of sector data, the accounting
      // of data within hunks is always in multiples of 'unitbytes'.
      const u32 chd_bytes_per_sector = header->unitbytes;

      // A "hunk" is the compressed blob of data in a CHD. There are many hunks.
      // The hunk size is a multiple of the addressing size of the source media,
      // i.e. CDROM sector, page, etc. The hunk size thus always contains 1 or more
      // whole units of the source media.

      const u32 data_offset = fad_within_hunkseq * chd_bytes_per_sector;
      const i32 hunknum     = data_offset / header->hunkbytes;
      const i32 hunkoff     = data_offset % header->hunkbytes;

      // If this is not the same hunk we last read from, load this new hunk.
      // This helps to reduce repeatedly reading the same hunk for contiguous
      // accesses.
      if (hunknum != m_hunknum) {
        // printf("Loading uncached hunk %d\n", hunknum);
        chd_read((chd_file *)m_chd_file, hunknum, m_hunk_data.data());
        m_hunknum = hunknum;
      }

      // We always copy the entire sector (but no subchannel or other goodies).
      const u32 read_size = std::min(u64(2352), output.count);
      memcpy(output.ptr, &m_hunk_data[hunkoff], read_size);
      // NOTE: we don't need to read from another hunk after this one because
      // hunks are always multiples of the source media unit (i.e. sector) size.

      // "Experimentally", audio was screwed up until I found out that it seems
      // CHD audio tracks have endianness swapped, which this will correct.
      if (track.is_audio_track()) {
        for (u32 i = 0; i < read_size; i += 2) {
          std::swap(output.ptr[i], output.ptr[i + 1]);
        }
      }

      SectorReadResult result = {};
      result.bytes_read       = read_size;
      result.track_num        = track.number;
      result.sector_layout    = track.sector_layout;
      return result;
    }
  }

  return SectorReadResult { 0 };
}

} // namespace zoo::media
