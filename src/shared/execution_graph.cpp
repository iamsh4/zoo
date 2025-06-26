#include <algorithm>
#include <cstdio>

#include "shared/execution_graph.h"

bool
ExecutionGraph::has_code_region(u32 start_pc) const
{
  return m_nodes.find(start_pc) != m_nodes.end();
}

void
ExecutionGraph::add_code_region(const ExecutionGraph::Node &&node)
{
  // if (m_nodes.size() > 300)
  //   return;

  m_nodes.emplace(node.start_pc, std::move(node));
}

void
ExecutionGraph::increment_edge(u32 src_node, u32 dest_node)
{
  if (m_nodes.find(src_node) == m_nodes.end())
    return;
  if (m_nodes.find(dest_node) == m_nodes.end())
    return;

  m_edge_visit_count[src_node][dest_node]++;
}

void
ExecutionGraph::clear()
{
  m_nodes = {};
  m_edge_visit_count = {};
}

void
ExecutionGraph::save(const char *file_path) const
{
  printf("Writing dot file '%s'\n", file_path);

  FILE *f = fopen(file_path, "w");
  fprintf(f, "digraph G {\n");
  fprintf(f, " compound=true;\n");

  std::vector<Node> all_nodes;
  for (const auto &[_, node] : m_nodes) {
    all_nodes.push_back(node);
  }

  std::sort(all_nodes.begin(), all_nodes.end(), [](const Node &a, const Node &b) {
    return a.top_of_call_stack < b.top_of_call_stack;
  });

  u32 current_function = 0xFFFFFFFF;
  for (const auto &node : all_nodes) {
    if (node.top_of_call_stack != current_function) {
      if (current_function != 0xFFFFFFFF) {
        fprintf(f, " }\n");
      }

      fprintf(f, " subgraph cluster_0x%08x {\n", node.top_of_call_stack);
      fprintf(f, "  label=\"0x%08x\";\n", node.top_of_call_stack);
      current_function = node.top_of_call_stack;
    }
    // fprintf(f, "    \"0x%08x\" [label=\"%s\"];\n", node.start_pc, node.label.c_str());
    fprintf(f, "    \"0x%08x\" [label=\"%08x\"];\n", node.start_pc, node.start_pc);
  }
  // Close last subgraph
  fprintf(f, " }\n\n");

  for (const auto &[start_pc, edge_counts] : m_edge_visit_count) {
    for (const auto &[end_pc, count] : edge_counts) {
      fprintf(f, " \"0x%08x\" -> \"0x%08x\" [label=\"%u\"];\n", start_pc, end_pc, count);
    }
  }

  fprintf(f, "}\n");
  fclose(f);
}
