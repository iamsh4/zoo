#include <cstdio>
#include "trace.h"

Trace::Trace(const char *output_file_path) : m_zone_count(0)
{
  m_trace_fd = fopen(output_file_path, "w");

  fprintf(m_trace_fd, "{\n");
  fprintf(m_trace_fd, "\"displayTimeUnit\": \"ns\",\n");
  fprintf(m_trace_fd, "\"traceEvents\": [\n");
}

Trace::~Trace()
{
  fprintf(m_trace_fd, "]\n");
  fprintf(m_trace_fd, "}\n");
  fclose(m_trace_fd);
}

void
Trace::register_track_name(u32 track_num, std::string_view track_name)
{
  // TODO
}

void
Trace::zone(u32 track_num, u64 start, u64 end, std::string_view zone_name)
{
  fprintf(
    m_trace_fd,
    R"(  {"name": "%s", "ph": "X", "pid": 0, "tid": %u, "ts": %f, "dur": %f },)",
    zone_name.data(),
    track_num,
    double(start) * 0.001,
    double(end - start) * 0.001);

  fprintf(m_trace_fd, "\n");

  m_zone_count++;
}

void Trace::instant(u32 track_num, u64 timestamp, std::string_view name)
{
  fprintf(
    m_trace_fd,
    R"(  {"name": "%s", "ph": "i", "pid": 0, "tid": %u, "ts": %f },)",
    name.data(),
    track_num,
    double(timestamp) * 0.001);

  fprintf(m_trace_fd, "\n");
}
