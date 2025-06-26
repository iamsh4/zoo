#pragma once

#include <functional>
#include <string>
#include <vector>

#include "shared/types.h"

namespace zoo::local {

enum Regions
{
  Regions_America = 1,
  Regions_PAL = 2,
  Regions_Japan = 4,
};

enum System
{
  Sytem_Playstation1 = 0,
  System_Dreamcast = 1,
};

struct GameLibraryEntry {
  /** Path of the game file */
  std::string file_path;

  /** Size of the file on-disc */
  u64 file_size;

  /** A unique id for this game/media which would be consistent across storage
   *  representations. This could be e.g. a hash of a metadata sector from a
   *  disc. */
  u64 media_id;

  /** Last-modified time since last scan */
  u64 last_modified;

  /** Product serial number, usually extracted from the game media itself */
  std::string serial;

  /** The name of the game */
  std::string name;

  /** How many times this game has been launched */
  u32 play_count;

  u32 regions;
};

class GameLibrary {
public:
  void load(const std::string &db_file_path);
  void save(const std::string &db_file_path);

  using ScanProgressCallback =
    std::function<void(const GameLibraryEntry &latest, u32 completed, u32 total)>;

  struct ScanSettings {
    /** Directories should be recursively scanned for content */
    bool recursive = true;
    /** Whether the scan should only look for new or modified files. If this is
     *  false, then a full scan of the directory(s) will take place. */
    bool only_modified = false;
    /** List of extensions to consider for scanning. Each entry should be just
     *  the extension with, including the dot. */
    std::vector<std::string_view> extensions;
  };

  /** Scan the content of the game directory */
  void scan(std::string_view directory_path,
            ScanSettings settings,
            ScanProgressCallback progress_callback = nullptr);

  const std::vector<GameLibraryEntry> &data() const;

  void clear();

private:
  /** Games resulting from the scan */
  std::vector<GameLibraryEntry> m_entries;
};

} // namespace zoo::local
