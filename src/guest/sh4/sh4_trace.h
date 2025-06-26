#pragma once

#include <mutex>
#include <deque>
#include <unordered_map>

#include "shared/types.h"

namespace tracing {

struct Block {
  u32 start_pc;
  bool operator==(const Block &o) const
  {
    return start_pc == o.start_pc;
  }
};

struct Edge {
  u32 source;
  u32 destination;
  bool operator==(const Edge &o) const
  {
    return source == o.source && destination == o.destination;
  }
};

}

namespace std {

template<>
struct hash<tracing::Block> {
  std::size_t operator()(const tracing::Block &block) const
  {
    return std::hash<u32> {}(block.start_pc);
  }
};

template<>
struct hash<tracing::Edge> {
  std::size_t operator()(const tracing::Edge &edge) const
  {
    return std::hash<u32> {}(edge.source + edge.destination);
  }
};

}

namespace tracing {

class Trace {
public:
  static const u64 NONE_SPECIFIED = UINT64_MAX;

  struct Stats {
    /* Statistics */
    u64 first_visit;
    u64 most_recent_visit;
    u64 visit_count;
  };

  struct BlockAndTime {
    u32 start_pc;
    u64 cycles;
  };

  void visit(u32 block_address, u64 cycle_count);
  void reset();

  const std::unordered_map<tracing::Block, Stats> &get_block_stats() const
  {
    return m_block_stats;
  }

  const std::unordered_map<tracing::Edge, Stats> &get_edge_stats() const
  {
    return m_edge_stats;
  }

  const std::deque<BlockAndTime> &get_recent_blocks() const
  {
    return m_recent_blocks;
  }

private:
  static const unsigned MAX_RECENT = 1000 * 1000;

  std::unordered_map<tracing::Block, Stats> m_block_stats;
  std::unordered_map<tracing::Edge, Stats> m_edge_stats;
  std::deque<BlockAndTime> m_recent_blocks;
  std::mutex m_mutex;
};

}
