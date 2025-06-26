#include "media/disc.h"

#include "media/chd_disc.h"
#include "media/gdi_disc.h"

#include "media/iso9660.h"

namespace zoo::media {

std::shared_ptr<Disc>
Disc::open(const char *path)
{
  // Helper to check extension. This is std in c++20 when we migrate
  const auto ends_with = [](const char *str, const char *pattern) {
    const size_t str_len     = strlen(str);
    const size_t pattern_len = strlen(pattern);
    if (pattern_len > 0 && pattern_len < str_len) {
      return !!strstr(str + str_len - pattern_len, pattern);
    }
    return false;
  };

  // Load the disc from the appropriate storage format
  if (ends_with(path, ".chd")) {
    return std::make_shared<zoo::media::CHDDisc>(path);
  } else if (ends_with(path, ".gdi")) {
    return std::make_shared<zoo::media::GDIDisc>(path);
  }

  printf("Don't know how to open this disc '%s'\n", path);
  return nullptr;
}

u32
Disc::read_bytes(u32 sector, u32 num_bytes, util::Span<u8> output)
{
  u8 buffer[2352];

  u32 bytes_read = 0;
  u8 *output_ptr = output.ptr;

  while (num_bytes > 0) {
    SectorReadResult read_result = read_sector(sector, { buffer, sizeof(buffer) });

    // Skip any header areas of the sector before consuming data into the output buffer
    const u32 header_size      = read_result.sector_layout.header_size();
    const u8 *read_result_data = &buffer[header_size];

    const u32 bytes_to_copy = std::min(num_bytes, 2048u);
    memcpy(output_ptr, read_result_data, bytes_to_copy);

    output_ptr += bytes_to_copy;
    bytes_read += bytes_to_copy;
    num_bytes -= bytes_to_copy;
  }

  return bytes_read;
}

u32
Disc ::load_file(const char *file_name, util::Span<u8> destination)
{
  iso::PrimaryVolumeDescriptor desc;
  static_assert(sizeof(desc) == 2048);

  // start of usual area data, skip over 16 sectors to ISO data
  const u32 pvd_sector = 45150 + 16;
  read_bytes(pvd_sector, sizeof(desc), { (u8 *)&desc, sizeof(desc) });
  // u8 sector_data[2352];
  // const u32 bytes_read = read_sector(pvd_sector, { sector_data, 2352 });
  // memcpy(&desc, &sector_data[16], 2048);

  char buffer[256];
  memset(buffer, 0, sizeof(buffer));
  memcpy(buffer, desc.volume_id, 32);

  // Print out some of the PVD data
  memcpy(buffer, desc.volume_id, 32);
  printf("Volume Identifier: %s\n", buffer);
  memcpy(buffer, desc.system_id, 32);
  printf("System Identifier: %s\n", buffer);
  printf("Volume Space Size: %u\n", *(u32 *)desc.volume_space_size);
  printf("Logical Block Size: %u\n", *(u32 *)desc.logical_block_size);
  printf("Path Table Size: %u\n", *(u32 *)desc.path_table_size);
  printf("LBA Path Table: %u\n", *(u32 *)desc.lba_path_table);
  printf("LBA Path Table BE: %u\n", *(u32 *)desc.lba_path_table_be);

  memcpy(buffer, desc.volume_set_id, 128);
  printf("Volume Set Identifier: %s\n", buffer);
  // printf("Publisher Identifier: %s\n", desc.publisher_id);
  // printf("Data Preparer Identifier: %s\n", desc.data_preparer_id);
  memcpy(buffer, desc.application_id, 128);
  printf("Application Identifier: %s\n", buffer);

  // The root directory entry
  iso::Directory root_dir;
  memcpy(&root_dir, desc.root_directory_entry, sizeof(root_dir));
  printf("Root Directory Length: %u\n", root_dir.length);
  printf("Root Directory LBA: %u\n", *(u32 *)root_dir.extent_lba);

  // Iterate over items in the root directory
  u32 root_dir_lba;
  memcpy(&root_dir_lba, &root_dir.extent_lba, 4);
  root_dir_lba += 150;

  std::vector<u8> dir_buff;
  dir_buff.resize(128 * 1024);

  read_bytes(root_dir_lba, dir_buff.size(), dir_buff);

  u32 offset = 0;
  for (u8 i = 0; i < root_dir.length; i++) {
    iso::Directory *dir_entry = (iso::Directory *)&dir_buff[offset];
    // memcpy(&dir_entry, &dir_buff[offset], sizeof(dir_entry));

    u32 file_size;
    memcpy(&file_size, dir_entry->extent_size, 4);

    memcpy(buffer, dir_entry->name, dir_entry->name_len);
    buffer[dir_entry->name_len] = '\0';

    printf("Root Directory Entry '%s' file size %u bytes\n", buffer, file_size);

    // If this file is the one we're looking for, read it into the destination buffer
    if (strcmp(buffer, file_name) == 0) {
      u32 file_lba;
      memcpy(&file_lba, dir_entry->extent_lba, 4);
      file_lba += 150;

      u32 bytes_read = read_bytes(file_lba, file_size, destination);
      printf("Read %u bytes from file '%s'\n", bytes_read, file_name);
      return bytes_read;
    }

    offset += dir_entry->length;
  }
  // printf("Bytes read from root dir: %u\n", bytes_read);

  return 0;
}

} // namespace zoo::media
