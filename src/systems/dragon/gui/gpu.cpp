#include <imgui.h>
#include <stack>

#include "systems/dragon/gui/gpu.h"
#include "systems/dragon/console.h"
#include "systems/dragon/hw/gpu_regs.h"

#include "./gpu_debug.h"

namespace zoo::dragon::gui {

GPU::GPU(Console *console) : ::gui::Window("GPU"), m_console(console)
{
  m_command_list.id = 0x1234567;
}

const char *
get_register_name(u32 reg_index)
{
  switch (reg_index & 0xff) {
    case gpu::Register::BUSY:
      return "BUSY";
    case gpu::Register::WAIT:
      return "WAIT";
    case gpu::Register::CMD_FIFO_START:
      return "CMD_FIFO_START";
    case gpu::Register::CMD_FIFO_CLEAR:
      return "CMD_FIFO_CLEAR";
    case gpu::Register::CMD_FIFO_COUNT:
      return "CMD_FIFO_COUNT";
    case gpu::Register::CMD_BUF_BEGIN:
      return "CMD_BUF_BEGIN";
    case gpu::Register::CMD_BUF_END:
      return "CMD_BUF_END";
    case gpu::Register::CMD_BUF_EXEC:
      return "CMD_BUF_EXEC";
    case gpu::Register::EE_INTERRUPT:
      return "EE_INTERRUPT";
    case gpu::Register::EXEC_DRAW_TRIANGLES:
      return "EXEC_DRAW_TRIANGLES";
    case gpu::Register::TRIANGLE_FORMAT:
      return "TRIANGLE_FORMAT";
    case gpu::Register::TRIANGLE_INDEX_ADDR:
      return "TRIANGLE_INDEX_ADDR";
    case gpu::Register::TRIANGLE_VERTEX_ADDR:
      return "TRIANGLE_VERTEX_ADDR";
    case gpu::Register::TRIANGLE_COUNT:
      return "TRIANGLE_COUNT";
    case gpu::Register::DRAW_BIN_XY:
      return "DRAW_BIN_XY";
    case gpu::Register::EXEC_VPU0_DMA:
      return "EXEC_VPU0_DMA";
    case gpu::Register::EXEC_VPU1_DMA:
      return "EXEC_VPU1_DMA";
    case gpu::Register::VPU0_DMA_CONFIG:
      return "VPU0_DMA_CONFIG";
    case gpu::Register::VPU1_DMA_CONFIG:
      return "VPU1_DMA_CONFIG";
    case gpu::Register::VPU0_DMA_BUFFER_ADDR:
      return "VPU0_DMA_BUFFER_ADDR";
    case gpu::Register::VPU1_DMA_BUFFER_ADDR:
      return "VPU1_DMA_BUFFER_ADDR";
    case gpu::Register::VPU0_DMA_EXTERNAL_ADDR:
      return "VPU0_DMA_EXTERNAL_ADDR";
    case gpu::Register::VPU1_DMA_EXTERNAL_ADDR:
      return "VPU1_DMA_EXTERNAL_ADDR";
    case gpu::Register::VPU_REG_XY:
      return "VPU_REG_XY";
    case gpu::Register::VPU_REG_ZW:
      return "VPU_REG_ZW";
    case gpu::Register::EXEC_WRITE_VPU_GLOBAL:
      return "EXEC_WRITE_VPU_GLOBAL";
    case gpu::Register::EXEC_WRITE_VPU_SHARED:
      return "EXEC_WRITE_VPU_SHARED";
    case gpu::Register::EXEC_VPU_LAUNCH_ARRAY:
      return "EXEC_VPU_LAUNCH_ARRAY";
    default:
      return "UNKNOWN";
  }
}

void
GPU::process_command_list()
{
  dragon::GPU *gpu = m_console->gpu();
  const bool did_change = gpu->get_command_list_if_different(&m_command_list);

  if (!did_change) {
    return;
  }

  const u8 *const root = (u8 *)m_console->memory()->root();

  u32 level = 0;
  m_tree_nodes.clear();

  for (size_t i = 0; i < m_command_list.commands.size(); ++i) {
    const dragon::GPU::Command &command = m_command_list.commands[i];
    // const u32 command_address = m_command_list.base_address + i * 8;
    const bool is_user_area = (command.command & 0x8000'0100) == 0x8000'0100;
    const bool is_debug_index = (command.command & 0xf8) == 0xf8;
    const u32 debug_type = (command.command >> 16) & 0xff;
    // const u32 debug_slot = (command.command & 0xf8) - 0xf8;

    CommandTreeNode node;
    node.command = command.command;
    node.value = command.value;
    node.level = level;

    if (is_user_area && is_debug_index) {
      node.is_debug = true;
      node.debug_word = debug_type;
      if (debug_type == gpu_debug::DEBUG_WORD_PUSH_SECTION) {
        char buff[512];
        snprintf(buff, sizeof(buff), "Section '%s'", (const char *)&root[node.value]);
        node.name = buff;

        node.level = level;
        level++;
      } else if (debug_type == gpu_debug::DEBUG_WORD_POP_SECTION) {
        node.level = level;
        level--;
      }
    }

    m_tree_nodes.push_back(node);
  }
}

void
GPU::render_raw_command_list()
{
  dragon::GPU *gpu = m_console->gpu();
  if (!ImGui::BeginChild("GPU EE Command List")) {
    ImGui::EndChild();
    return;
  }
  ImGui::Columns(5);

  // Headers
  ImGui::Text("Index");
  ImGui::NextColumn();
  ImGui::Text("Bus address");
  ImGui::NextColumn();
  ImGui::Text("Register");
  ImGui::NextColumn();
  ImGui::Text("Value");
  ImGui::NextColumn();
  ImGui::Text("Comment");
  ImGui::NextColumn();

  ImGui::Separator();

  for (size_t i = 0; i < m_command_list.commands.size(); ++i) {
    const dragon::GPU::Command &command = m_command_list.commands[i];
    const u32 command_address = m_command_list.base_address + i * 8;

    ImGui::BeginGroup();

    ImGui::Text("%d", i);
    ImGui::NextColumn();

    const bool current = gpu->m_ee.fifo_address_current == command_address;
    if (current)
      ImGui::TextColored(ImVec4 { 1.0f, 1.0f, 0.0f, 1.0f }, "0x%08x", command_address);
    else
      ImGui::TextColored(ImVec4 { 1.0f, 1.0f, 0.0f, .6f }, "0x%08x", command_address);

    ImGui::NextColumn();

    if ((command.command & 0x100) == 0) {
      ImGui::Text("%s", get_register_name(command.command));
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("Register 0x%08x", command.command);
        ImGui::EndTooltip();
      }
    } else {
      ImGui::Text("Perf register 0x%08x", command.command);
    }
    ImGui::NextColumn();

    ImGui::Text("0x%08x", m_command_list.commands[i].value);
    ImGui::NextColumn();

    ImGui::Text("");
    ImGui::NextColumn();

    ImGui::EndGroup();
  }

  ImGui::Columns(1);
  ImGui::EndChild();
}

void
GPU::render_pretty_command_list()
{
  const auto gpu = m_console->gpu();

  if (!ImGui::BeginTable("pretty_table", 4, ImGuiTableFlags_BordersV)) {
    return;
  }

  ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed);
  ImGui::TableSetupColumn("Raw Register:Value", ImGuiTableColumnFlags_WidthFixed);
  ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed);
  ImGui::TableSetupColumn("Info");

  ImGui::TableHeadersRow();

  auto GetNodeName = [&](const CommandTreeNode &node) {
    if (node.debug_word == gpu_debug::DEBUG_WORD_PUSH_SECTION) {
      return node.name.c_str();
    } else if (node.debug_word == gpu_debug::DEBUG_WORD_POP_SECTION) {
      return "PopSection";
    } else if ((node.command & 0x100) == 0) {
      return get_register_name(node.command);
    } else {
      return "PerfRegister";
    }
  };

  std::stack<u32> open_nodes;

  static const char *indents[] = {
    "", ". ", ". . ", ". . . ", ". . . . ",
  };

  using namespace gpu_debug;

  u32 debug_words[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

  for (u32 i = 0; i < m_tree_nodes.size(); i++) {
    const CommandTreeNode &node = m_tree_nodes[i];

    bool is_debug_arg = false;
    if (node.is_debug) {
      const dragon::GPU::Command &command = m_command_list.commands[i];
      // const u32 debug_type = (command.command >> 16) & 0xff;
      const u32 debug_slot = (command.command & 0xf8) - 0xf8;
      debug_words[debug_slot] = command.value;
      is_debug_arg = debug_slot > 0;
    }

    const u32 current_address = gpu->m_ee.fifo_address_current;
    const u32 command_address = m_command_list.base_address + i * 8;
    const bool is_current = current_address == command_address;

    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    ImGui::Text("%u", i);

    ImGui::TableSetColumnIndex(1);
    if (is_current) {
      const ImVec4 highlight_color { 0.5f, 0.5f, 1.0f, 1.0f };
      ImGui::TextColored(highlight_color, "0x%08x:0x%08x", node.command, node.value);
    } else {
      ImGui::Text("0x%08x:0x%08x", node.command, node.value);
    }

    const char *node_name = GetNodeName(node);
    if (is_debug_arg) {
      node_name = "(Debug Argument)";
    }

    const bool grey =
      node.debug_word == DEBUG_WORD_POP_SECTION || node.debug_word == DEBUG_WORD_NOP;

    ImGui::TableSetColumnIndex(2);
    if (node.is_debug && grey) {
      ImGui::TextDisabled("%s%s", indents[node.level], node_name);
    } else {
      ImGui::Text("%s%s", indents[node.level], node_name);
    }

    ImGui::TableSetColumnIndex(3);
    if (node.is_debug && node.debug_word == DEBUG_WORD_INTENT_DMA_SYSMEM_TO_TILE) {
      // const u32 tb = m_tree_nodes[i + 1].value & 0xff;
      ImGui::Text(
        "DMA Sysmem to Tile (Bus Address 0x%08x -> TB%u)", node.value, debug_words[1]);
    }
    if (node.is_debug && node.debug_word == DEBUG_WORD_INTENT_DMA_TILE_TO_SYSMEM) {
      // const u32 tb = m_tree_nodes[i + 1].value & 0xff;
      ImGui::Text(
        "DMA Tile to Sysmem (TB%u -> Bus Address 0x%08x)", debug_words[1], node.value);
    }
  }

  ImGui::EndTable();
}

void
GPU::render()
{
  dragon::GPU *gpu = m_console->gpu();
  process_command_list();

  ImGui::Begin("GPU EE");
  ImGui::Text("FIFO_COUNT %u", gpu->m_registers[gpu::Register::CMD_FIFO_COUNT]);
  ImGui::Text("Command List (id=%08x, count=%d)",
              m_command_list.id,
              m_command_list.commands.size());

  render_pretty_command_list();
  // render_raw_command_list();

  ImGui::End();
}
}
