#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include "shared/types.h"

class ExecutionGraph {
public:
  struct Node {
    u32 start_pc;
    u32 code_length;
    u32 top_of_call_stack;
    std::string label;
  };

  bool has_code_region(u32 start_pc) const;

  //
  void add_code_region(const Node &&);
  //
  void increment_edge(u32 src_node, u32 dest_node);
  //
  void save(const char *file_path) const;

  void clear();

private:
  // start_pc -> Node
  std::unordered_map<u32, Node> m_nodes;

  // start_pc src block -> start_pc dest block
  std::unordered_map<u32, std::unordered_map<u32, u32>> m_edge_visit_count;
};
