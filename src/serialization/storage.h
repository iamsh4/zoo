#pragma once

#include <filesystem>
#include <functional>
#include <iterator>
#include <shared/types.h>
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>
#include <cstring>

namespace serialization {

struct Storage {
  struct Range {
    u64 start_address;
    u64 length;
    u8 data[0];
  };

  /*! @brief The storage data containing all Ranges and their backing data */
  std::vector<u8> data;
};
static_assert(sizeof(Storage::Range) == 16);

class Snapshot {
public:
  using id_t = u64;
  static constexpr id_t NO_PARENT = UINT64_MAX;

private:
  id_t m_id = 0;

  /*! @brief The parent id of this snapshot. If this is a complete snapshot, then this is
   * NO_PARENT. */
  id_t m_parent_id = 0;

  /*! @brief A map of component names to their storage. */
  std::unordered_map<std::string, Storage> m_components;

  u64 m_console_timestamp_nanos = 0;

  u64 m_total_size = 0;
  void recalculate_total_size();

  struct Header {
    id_t id;
    id_t parent_id;
    id_t console_time;
    u32 component_count;
  };

public:
  Snapshot();
  Snapshot(id_t my_id, id_t parent_id, u64 console_nanos);

  void save(std::filesystem::path file_path);
  void load(std::filesystem::path file_path);

  id_t get_id() const;
  id_t get_parent_id() const;

  void add_range(const std::string &component_name, u64 length, const void *src);

  void reserve_bytes(const std::string &component_name, u64 bytes);

  void add_range(const std::string &component_name,
                 u64 start_address,
                 u64 length,
                 const void *src);

  using component_visitor = std::function<void(const Storage::Range *)>;
  void visit_ranges(const std::string &component_name,
                    const component_visitor &visitor) const;

  void apply_all_ranges(const std::string &component_name, void *dst) const;

  void apply_all_ranges(const std::string& component_name,
                        std::function<void(const Storage::Range*)> applier) const;

  u64 get_total_bytes() const;

  u64 get_console_timestamp_nanos() const;

  std::unordered_map<std::string, u64> get_total_bytes_by_component() const;

  void print_snapshot_report(bool show_section_breakdown = false) const;
};

}
