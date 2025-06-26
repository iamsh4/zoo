#include <filesystem>
#include "media/gdrom_utilities.h"
#include "media/disc.h"

void
print_json(const char* disc_path, GDROMDiscMetadata metadata)
{
  // Get the file name from this disc path
  std::filesystem::path path = disc_path;
  const std::string file_name = path.filename().string();

  printf(
    R"({"filename": "%s", "device_info":"%s","area_symbols":"%s","peripherals":"%s","product_number":"%s","product_version":"%s","release_date":"%s","boot_filename":"%s","company_name":"%s","software_name":"%s"})",
    file_name.c_str(),
    metadata.device_info.c_str(),
    metadata.area_symbols.c_str(),
    metadata.peripherals.c_str(),
    metadata.product_number.c_str(),
    metadata.product_version.c_str(),
    metadata.release_date.c_str(),
    metadata.boot_filename.c_str(),
    metadata.company_name.c_str(),
    metadata.software_name.c_str());
}

int
main(int argc, char *argv[])
{
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <disc_path or folder>\n", argv[0]);
    return 1;
  }

  // Determine if argv[1] is a folder or a file
  std::filesystem::path path = argv[1];
  const bool is_folder       = std::filesystem::is_directory(path);

  if (!is_folder) {
    const char *disc_path      = argv[1];
    const auto disc            = zoo::media::Disc::open(disc_path);
    GDROMDiscMetadata metadata = gdrom_disc_metadata(disc.get());
    print_json(disc_path, metadata);
    printf("\n");
  } else {
    printf("[\n");
    bool first = true;

    // Iterate over every .chd file in this folder and print metadata
    for (const auto &entry : std::filesystem::directory_iterator(path)) {
      const bool valid_extension =
        entry.path().extension() == ".chd" || entry.path().extension() == ".gdi";

      if (entry.is_regular_file() && valid_extension) {
        if (!first) {
          printf(",\n");
        } else {
          first = false;
        }
        const char* disc_path = entry.path().c_str();
        const auto disc            = zoo::media::Disc::open(disc_path);
        GDROMDiscMetadata metadata = gdrom_disc_metadata(disc.get());
        print_json(disc_path, metadata);
      }
    }

    printf("\n]\n");
  }

  return 0;
}