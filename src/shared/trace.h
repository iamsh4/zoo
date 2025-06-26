#pragma once

#include <cstdio>
#include <string_view>
#include <memory>

#include "shared/types.h"

class Trace {
public:
  Trace(const char *output_file_path);
  ~Trace();

  void register_track_name(u32 track_num, std::string_view track_name);
  void instant(u32 track_num, u64 timestamp, std::string_view name);
  void zone(u32 track_num, u64 start, u64 end, std::string_view zone_name);

  u64 get_zone_count() const
  {
    return m_zone_count;
  }

private:
  FILE *m_trace_fd;
  u64 m_zone_count = 0;
};
