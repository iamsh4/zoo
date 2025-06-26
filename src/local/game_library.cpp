#include <filesystem>
#include <algorithm>
#include <fstream>

#include "local/game_library.h"
#include "media/chd_disc.h"
#include "media/gdi_disc.h"
#include "shared/string.h"
#include "shared/crc32.h"

#include <json/json.h>

namespace zoo::local {

void
ensure_file_exists(const std::string &db_file_path)
{
  // TODO : Ensure/create folder

  FILE *fp = fopen(db_file_path.c_str(), "r");
  if (!fp) {
    fp = fopen(db_file_path.c_str(), "w");
  }
  fclose(fp);
}

bool
is_file_like(const std::filesystem::directory_entry &entry)
{
  // TODO : Check that symlink resolves to regular file, those should also return true
  return entry.is_regular_file();
}

const std::vector<GameLibraryEntry> &
GameLibrary::data() const
{
  return m_entries;
}

void
GameLibrary::clear()
{
  m_entries = {};
}

void
GameLibrary::scan(std::string_view directory_path,
                  ScanSettings settings,
                  ScanProgressCallback progress_callback)
{
  using namespace std::filesystem;

  const auto is_target_file_type = [&](const directory_entry &dir_entry) {
    if (!is_file_like(dir_entry)) {
      return false;
    }
    std::string str = dir_entry.path().extension();
    return std::find(settings.extensions.begin(), settings.extensions.end(), str) !=
           settings.extensions.end();
  };

  std::vector<directory_entry> paths;
  if (settings.recursive) {
    for (const auto &dir_entry : recursive_directory_iterator(directory_path)) {
      if (is_target_file_type(dir_entry)) {
        paths.push_back(dir_entry);
      }
    }
  } else {
    for (const auto &dir_entry : directory_iterator(directory_path)) {
      if (is_target_file_type(dir_entry)) {
        paths.push_back(dir_entry);
      }
    }
  }

  // Clear out existing library.
  m_entries = {};

  // Some of the below logic is dreamcast specific
  // TODO: Move dreamcast specific Entry generator to another function.

  std::vector<u8> buffer(4096);
  u32 processed_count = 0;
  for (const directory_entry &dir_entry : paths) {
    const char *file_path = dir_entry.path().c_str();

    if (auto disc = zoo::media::Disc::open(file_path)) {
      disc->read_sector(150, { buffer.data(), 2352 });

      const auto read_string = [](const u8 *data, u8 offset, u8 len) {
        char tmp[1024] = { 0 };
        memcpy(tmp, data + offset, len);
        return std::string(tmp);
      };

      std::string maker = read_string(buffer.data(), 0x80, 16);
      std::string product = read_string(buffer.data(), 0x90, 64);
      std::string serial = read_string(buffer.data(), 0x50, 10);

      rtrim(maker);
      rtrim(product);
      rtrim(serial);

      const u64 last_modified_time =
        dir_entry.last_write_time().time_since_epoch().count();

      // TODO : Support GDI file size
      const u64 file_size = dir_entry.file_size();

      // Media ID == CRC32(first data sector)
      const u64 media_id = crc32(buffer.data(), 2352);

      GameLibraryEntry entry {
        .file_path = file_path,
        .file_size = file_size,
        .media_id = media_id,
        .last_modified = last_modified_time,
        .serial = serial,
        .name = product,
        .play_count = 0, // XXX : Ideally merge with any existing results
      };

      m_entries.push_back(entry);
      processed_count++;

      if (progress_callback) {
        progress_callback(entry, processed_count, paths.size());
      }
    }
  }

  // Sort by file name
  std::sort(m_entries.begin(),
            m_entries.end(),
            [](const GameLibraryEntry &a, const GameLibraryEntry &b) {
              return strcmp(a.file_path.c_str(), b.file_path.c_str()) < 0;
            });
}

void
GameLibrary::save(const std::string &db_file_path)
{
  Json::Value root;

  Json::Value &media_list = root["media"];
  u32 i = 0;
  for (const auto &entry : m_entries) {
    Json::Value &media_entry = media_list[i++];

    media_entry["path"] = entry.file_path;
    media_entry["size"] = entry.file_size;
    media_entry["last_modified"] = entry.last_modified;
    media_entry["name"] = entry.name;
    media_entry["play_count"] = entry.play_count;
    media_entry["serial"] = entry.serial;
    media_entry["media_id"] = entry.media_id;
  }

  Json::StreamWriterBuilder builder;
  builder["commentStyle"] = "None";
  builder["indentation"] = "  ";

  std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
  std::ofstream out(db_file_path, std::ofstream::binary);
  writer->write(root, &out);
}

void
GameLibrary::load(const std::string &db_file_path)
{
  ensure_file_exists(db_file_path);
  std::ifstream file(db_file_path, std::ifstream::binary);

  Json::Value root;
  try {
    file >> root;
  } catch (std::exception &) {
    // ... TODO: Most likely the file didn't contain any content yet
  }

  m_entries.clear();
  for (const Json::Value &val : root["media"]) {
    GameLibraryEntry entry {};
    entry.file_path = val["path"].asString();
    entry.file_size = val["size"].asUInt64();
    entry.last_modified = val["last_modified"].asUInt64();
    entry.name = val["name"].asString();
    entry.play_count = val["play_count"].asUInt();
    entry.serial = val["serial"].asString();
    entry.media_id = val["media_id"].asUInt64();

    m_entries.push_back(entry);
  }
}

} // namespace zoo::local
