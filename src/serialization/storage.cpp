#include <fmt/core.h>
#include <algorithm>
#include <vector>

#include "storage.h"

namespace serialization {

Snapshot::Snapshot(Snapshot::id_t my_id, Snapshot::id_t parent_id, u64 console_nanos)
  : m_id(my_id),
    m_parent_id(parent_id),
    m_console_timestamp_nanos(console_nanos)
{
}

Snapshot::Snapshot() : m_id(-1), m_parent_id(-1), m_console_timestamp_nanos(-1) {}

void
Snapshot::save(std::filesystem::path file_path)
{
  Header header {};
  header.console_time    = m_console_timestamp_nanos;
  header.id              = m_id;
  header.parent_id       = m_parent_id;
  header.component_count = m_components.size();

  FILE *f = fopen(file_path.c_str(), "wb");
  fwrite(&header, 1, sizeof(header), f);

  u32 data32;
  for (const auto &it : m_components) {
    // Component name (length + data)
    data32 = it.first.length();
    fwrite(&data32, 1, sizeof(u32), f);
    fwrite(it.first.data(), 1, data32, f);

    // Component data (length + data)
    data32 = it.second.data.size();
    fwrite(&data32, 1, sizeof(u32), f);
    fwrite(it.second.data.data(), 1, data32, f);
  }

  fclose(f);
}

void
Snapshot::load(std::filesystem::path file_path)
{
  FILE *f = fopen(file_path.c_str(), "rb");

  Header header {};
  fread(&header, 1, sizeof(header), f);
  m_console_timestamp_nanos = header.console_time;
  m_id                      = header.id;
  m_parent_id               = header.parent_id;

  char buff[512];

  // We don't support creating components from FDs yet.
  // std::vector<u8> data_buff;

  u32 data32;
  for (u32 i = 0; i < header.component_count; ++i) {
    // Component name (length + data)
    fread(&data32, 1, sizeof(data32), f);
    memset(buff, 0, 512);
    fread(buff, 1, data32, f);

    // Component data (length + data)
    fread(&data32, 1, sizeof(data32), f);
    std::vector<u8> component_data(data32);
    fread(component_data.data(), 1, data32, f);

    m_components.insert({buff, Storage{std::move(component_data)}});
  }
  fmt::println("Loaded {} components from '{}' (id {})", m_components.size(), file_path.c_str(), m_id);
}

Snapshot::id_t
Snapshot::get_id() const
{
  return m_id;
}

Snapshot::id_t
Snapshot::get_parent_id() const
{
  return m_parent_id;
}

u64
Snapshot::get_console_timestamp_nanos() const
{
  return m_console_timestamp_nanos;
}

void
Snapshot::add_range(const std::string &component_name, u64 length, const void *src)
{
  add_range(component_name, 0, length, src);
}

void
Snapshot::add_range(const std::string &component_name,
                    u64 start_address,
                    u64 length,
                    const void *src)
{
  Storage::Range range;
  range.start_address = start_address;
  range.length        = length;

  Storage &storage_wrapper(m_components[component_name]);
  std::vector<u8> &storage(storage_wrapper.data);

  if (storage.capacity() < storage.size() + sizeof(range) + length)
    storage.reserve(storage.size() + sizeof(range) + length);

  storage.insert(storage.end(), (u8 *)&range, (u8 *)(&range + 1));
  storage.insert(storage.end(), (u8 *)src, ((u8 *)src) + length);

  m_total_size += sizeof(range) + length;
}

void
Snapshot::visit_ranges(const std::string &component_name,
                       const component_visitor &visitor) const
{
  auto it = m_components.find(component_name);
  if (it == m_components.end()) {
    fmt::println("Skipping missing snapshot component '{}'", component_name.c_str());
    return;
  }

  const Storage &storage_wrapper = it->second;
  const std::vector<u8> &storage = storage_wrapper.data;
  u64 offset                     = 0;
  while (offset < storage.size()) {
    const Storage::Range *entry = (Storage::Range *)&storage[offset];
    const u64 next_offset       = offset + entry->length + sizeof(Storage::Range);
    visitor(entry);
    offset = next_offset;
  }
}

void
Snapshot::apply_all_ranges(const std::string &component_name, void *dst) const
{
  visit_ranges(component_name, [dst](const Storage::Range *range) {
    memcpy(((u8 *)dst) + range->start_address, range->data, range->length);
  });
}

void
Snapshot::apply_all_ranges(const std::string &component_name,
                           std::function<void(const Storage::Range *)> applier) const
{
  visit_ranges(component_name,
               [&applier](const Storage::Range *range) { applier(range); });
}

u64
Snapshot::get_total_bytes() const
{
  return m_total_size;
}

void
Snapshot::recalculate_total_size()
{
  u64 total = 0;
  for (const auto &it : m_components) {
    total += it.second.data.size();
  }
  m_total_size = total;
}

std::unordered_map<std::string, u64>
Snapshot::get_total_bytes_by_component() const
{
  std::unordered_map<std::string, u64> result;
  for (const auto &it : m_components) {
    result.insert({ it.first, (u64)it.second.data.size() });
  }
  return result;
}

void
Snapshot::print_snapshot_report(bool show_section_breakdown) const
{
  fmt::print("Created snapshot with {} KB\n", get_total_bytes() / 1024);

  if (show_section_breakdown) {
    std::vector<std::pair<std::string, u64>> sizes;
    for (const auto &[k, v] : get_total_bytes_by_component()) {
      sizes.push_back({ k, v });
    }
    std::sort(sizes.begin(), sizes.end());

    for (const auto &[k, v] : sizes) {
      if (v > 1024) {
        fmt::print("  - {:>8} KiB  {}\n", v / 1024, k);
      } else {
        fmt::print("  - {:>8} B    {}\n", v, k);
      }
    }
  }
}

}
