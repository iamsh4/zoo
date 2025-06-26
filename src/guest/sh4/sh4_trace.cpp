#include "sh4_trace.h"

namespace tracing {

void
Trace::reset()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_block_stats.clear();
  m_edge_stats.clear();
  m_recent_blocks.clear();
}

void
Trace::visit(u32 block_address, u64 cycle_count)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  /* Recent block deque */
  if (m_recent_blocks.size() == MAX_RECENT) {
    m_recent_blocks.pop_front();
  }

  /* Block statistics */
  const Block block = { block_address };
  if (auto it = m_block_stats.find(block); it == m_block_stats.end()) {
    m_block_stats.insert({ block,
                           {
                             .first_visit = cycle_count,
                             .most_recent_visit = cycle_count,
                             .visit_count = 1,
                           } });
  } else {
    it->second.visit_count++;
    it->second.most_recent_visit = cycle_count;
  }

  /* Edge statistics */
  const u32 last_block_pc =
    m_recent_blocks.empty() ? 0xFFFFFFFF : m_recent_blocks.back().start_pc;
  const Edge edge = { .source = block_address, .destination = last_block_pc };
  if (auto it = m_edge_stats.find(edge); it == m_edge_stats.end()) {
    m_edge_stats.insert({ edge,
                          {
                            .first_visit = cycle_count,
                            .most_recent_visit = cycle_count,
                            .visit_count = 1,
                          } });
  } else {
    it->second.visit_count++;
    it->second.most_recent_visit = cycle_count;
  }

  m_recent_blocks.push_back({ .start_pc = block_address, .cycles = cycle_count });
}

}
