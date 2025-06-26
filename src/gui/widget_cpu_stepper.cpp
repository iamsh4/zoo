#include <fmt/printf.h>

#include "guest/sh4/sh4_debug.h"
#include "gui/widget_cpu_stepper.h"
#include "shared/print.h"
#include "frontend/sdk_symbols.h"
#include "core/registers.h"

namespace gui {

CpuStepperWidget::CpuStepperWidget(std::shared_ptr<CPUWindowGuest> guest,
                                   JitWorkbenchWindow *const workbench,
                                   const unsigned context_lines)
  : m_cpu_guest(guest),
    m_workbench(workbench),
    m_context_lines(context_lines),
    m_last_pc(0)
{
  return;
}

CpuStepperWidget::~CpuStepperWidget()
{
  return;
}

/* TODO Concept of symbols needs to be exposed at the GUI Guest interface */
#if 0
/*!
 * @brief Take a line containing disassembly and append dereferences/register
 *        names to pointers when possible.
 */
static std::string
dereference_disas(std::shared_ptr<Console> &console, const std::string &line)
{
  // Helpers to dereference memory.
  static const u32 invalid_address = 0xFFFFFFFF;
  const auto deref = [&](u32 addr) {
    try {
      return console->memory()->read<u32>(addr);
    } catch (std::exception &) {
      return invalid_address;
    }
  };

  // The below loop will replace every instance of (A) with (B):
  // A) "... @$0x12345678, ..."
  // B) "... @0x12345678 (SB_ISTNRM), ..."

  std::vector<const SDKSymbol *> symbols;

  // TODO: Pull out the hint generation, also do this for e.g. "@R3".
  std::string processed = line;
  while (true) {
    size_t index = processed.find("@$0x");
    if (index == std::string::npos) {
      break;
    }

    const std::string pre = processed.substr(0, index);
    const std::string address_string = processed.substr(index + 4, 8);
    const std::string post = processed.substr(index + 4 + 8);

    // Assume the address is well-formed in the disassembly, see if what it points to is
    // a register name, otherwise just show the dereferenced value.
    const u32 address_hex = deref(read_hex_u32(address_string.c_str()));
    std::string hint_string;
    if (address_hex != invalid_address) {
      if (std::string dc_reg_name; is_register(address_hex, dc_reg_name)) {
        hint_string = " (" + dc_reg_name + ")";
      } else if (address_hex % 2 == 0 &&
                 console->memory()->check_ram(address_hex, 2048)) {

        const u32 matching_symbol_count =
          SDKSymbolManager::instance().get_matching_function_symbols(
            *console->memory(), address_hex, symbols, 1);

        if (matching_symbol_count > 0) {
          const auto &sym = symbols[0];
          hint_string = fmt::sprintf(
            " (0x%08x [%s.%s])", address_hex, sym->library_name, sym->symbol_name);
        } else {
          hint_string = fmt::sprintf(" (0x%08x)", address_hex);
        }
      }
    }

    processed = pre + "@0x" + address_string + hint_string + post;
  }

  return processed;
}
#endif

void
CpuStepperWidget::render()
{
  std::vector<u32> breakpoints;
  m_cpu_guest->breakpoint_list(&breakpoints);
  const auto breakpoint_check = [&breakpoints](const u32 pc) {
    for (const u32 entry : breakpoints) {
      if (entry == pc) {
        return true;
      }
    }
    return false;
  };

  ImGui::PushItemWidth(ImGui::GetContentRegionAvailWidth());

  /* Calculate address range to show */
  const u8 bytes_per_instruction = m_cpu_guest->bytes_per_instruction();
  const u32 next_pc = m_cpu_guest->get_pc();
  const u32 full_start_pc = next_pc - ((m_context_lines / 2) * bytes_per_instruction);

  const auto *font = ImGui::GetFont();
  const float row_height = font->FontSize + ImGui::GetStyle().CellPadding.y * 2;
  const float visible_lines_f = ImGui::GetContentRegionAvail().y / row_height;
  if (m_last_pc != next_pc) {
    ImGui::SetScrollY(row_height * (m_context_lines - visible_lines_f) * 0.5f);
    m_last_pc = next_pc;
  }

  int draw_start_row, draw_end_row;
  ImGui::CalcListClipping(m_context_lines, row_height, &draw_start_row, &draw_end_row);
  const u32 visible_lines = u32(draw_end_row) - u32(draw_start_row);
  const u32 start_pc = full_start_pc + draw_start_row * bytes_per_instruction;
  const u32 end_pc = start_pc + visible_lines * bytes_per_instruction;

  /* Collect all instructions in the range visible in the output window. */
  struct {
    u32 raw = 0;
    u32 pc = 0;
    bool is_breakpoint = false;
    fox::ref<fox::jit::CacheEntry> jit;
  } instructions[visible_lines];

  /* 
   * Get an iterator over memory regions. As we walk over PC, we can first make sure
   * we're pointing at a valid memory region. Otherwise the MemoryTable::read will
   * kill the program. 
   */
  fox::MemoryTable::MemoryRegions mem_regions = m_cpu_guest->memory_regions();
  auto region = mem_regions.begin();

  for (size_t i = 0, pc = start_pc; i < visible_lines; ++i, pc += bytes_per_instruction) {
    instructions[i].pc = pc;
    instructions[i].raw = 0xFFFFFFFF;
    instructions[i].is_breakpoint = breakpoint_check(pc);

    const u32 pc_phys = pc & 0x1fff'ffff;
    while (region != mem_regions.end() && pc_phys > (*region)->phys_end) {
      region++;
    }

    if (region != mem_regions.end() && pc_phys >= (*region)->phys_offset &&
        pc_phys < (*region)->phys_end) {
      try {
        instructions[i].raw = m_cpu_guest->fetch_instruction(pc);
      } catch (...) {
        /* Ignore */
      }
    }
  }

  /* Collect mapping from visible instructions to jit blocks. */
  do {
    fox::jit::Cache *const cache = m_cpu_guest->get_jit_cache();
    if (!cache) {
      break;
    }

    std::lock_guard _(*cache);
    const std::multimap<u32, fox::ref<fox::jit::CacheEntry>> &blocks =
      cache->invalidation_map();
    auto it = blocks.upper_bound(start_pc & 0x1fffffffu);
    if (it == blocks.end()) {
      break;
    }

    if (it->second->virtual_address() >= end_pc) {
      break;
    }

    for (size_t i = 0; i < visible_lines; ++i) {
      const u32 pc = instructions[i].pc;
      while (it->second->virtual_address() + it->second->size() <= pc) {
        if (++it == blocks.end()) {
          break;
        }
      }

      if (it == blocks.end()) {
        break;
      } else if (it->second->virtual_address() > pc) {
        continue;
      }

      assert(it->second->virtual_address() <= pc);
      assert(it->second->virtual_address() + it->second->size() > pc);

      instructions[i].jit = it->second;
    }
  } while (0);

  ImGui::BeginChild("disassembly", ImVec2(0.0f, row_height * m_context_lines));
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + draw_start_row * row_height);
  if (!ImGui::BeginTable("table", 5)) {
    ImGui::EndChild();
    ImGui::PopItemWidth();
    return;
  }

  const float render_address_width = ImGui::CalcTextSize("00000000 ").x;
  const float render_raw_width = ImGui::CalcTextSize("00 ").x * bytes_per_instruction;
  ImGui::TableSetupColumn(
    "block", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, 5.0f);
  ImGui::TableSetupColumn("address",
                          ImGuiTableColumnFlags_WidthFixed |
                            ImGuiTableColumnFlags_NoResize,
                          render_address_width);
  ImGui::TableSetupColumn("raw",
                          ImGuiTableColumnFlags_WidthFixed |
                            ImGuiTableColumnFlags_NoResize,
                          render_raw_width);
  ImGui::TableSetupColumn("disassembly");
  ImGui::TableSetupColumn(
    "actions", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, 40.0f);

  const ImU32 row_color_current = ImGui::GetColorU32(ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
  const ImU32 row_color_breakpoint = ImGui::GetColorU32(ImVec4(0.2f, 0.1f, 0.1f, 1.0f));
  const ImU32 row_color_block_none = ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
  const ImU32 row_color_blocks[7] = {
    ImGui::GetColorU32(ImVec4(0.5f, 0.0f, 0.0f, 1.0f)),
    ImGui::GetColorU32(ImVec4(0.0f, 0.5f, 0.0f, 1.0f)),
    ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.5f, 1.0f)),
    ImGui::GetColorU32(ImVec4(0.4f, 0.4f, 0.0f, 1.0f)),
    ImGui::GetColorU32(ImVec4(0.0f, 0.4f, 0.4f, 1.0f)),
    ImGui::GetColorU32(ImVec4(0.4f, 0.0f, 0.4f, 1.0f)),
    ImGui::GetColorU32(ImVec4(0.3f, 0.3f, 0.3f, 1.0f)),
  };
  const ImVec4 text_color_address = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
  const ImVec4 text_color_raw = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
  fox::jit::CacheEntry *last_block = nullptr;
  unsigned last_block_color = 0;
  for (ssize_t i = 0; i < visible_lines; ++i) {
    const u32 pc = instructions[i].pc;

    ImGui::PushID(i);
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);

    const float mouse_row_y =
      ImGui::GetMousePos().y - ImGui::GetCursorScreenPos().y - ImGui::GetScrollY();
    const bool is_row_hovered = mouse_row_y >= 0.0f && mouse_row_y < row_height;

    const bool is_current = (pc == next_pc);
    const bool is_breakpoint = instructions[i].is_breakpoint;
    if (is_current) {
      ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, row_color_current);
    } else if (is_breakpoint) {
      ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, row_color_breakpoint);
    }

    /* Color-code jit blocks. */
    if (instructions[i].jit.get() != last_block) {
      last_block = instructions[i].jit.get();
      last_block_color = (last_block_color + 1) % 7;
    }

    if (last_block != nullptr) {
      ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
                             row_color_blocks[last_block_color]);
      if (ImGui::Selectable("###select_block")) {
        m_workbench->set_target(last_block);
      }
    } else {
      ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, row_color_block_none);
    }

    ImGui::TableNextColumn();
    ImGui::TextColored(text_color_address, "%08x", u32(pc));

    ImGui::TableNextColumn();
    switch (bytes_per_instruction) {
      case 1:
        ImGui::TextColored(text_color_raw, "%02X", instructions[i].raw & 0xFFu);
        break;

      case 2:
        ImGui::TextColored(text_color_raw,
                           "%02X:%02x",
                           (instructions[i].raw >> 0) & 0xFFu,
                           (instructions[i].raw >> 8) & 0xFFu);
        break;

      case 4:
        ImGui::TextColored(text_color_raw,
                           "%02X:%02x:%02x:%02x",
                           (instructions[i].raw >> 0) & 0xFFu,
                           (instructions[i].raw >> 8) & 0xFFu,
                           (instructions[i].raw >> 16) & 0xFFu,
                           (instructions[i].raw >> 24) & 0xFFu);
        break;

      default:
        /* TODO */
        ImGui::TextColored(text_color_raw, "??");
    }

    const std::string disassembly = m_cpu_guest->disassemble(instructions[i].raw, pc);
    const std::string annotated_disassembly = disassembly;

    ImGui::TableNextColumn();
    ImGui::Text("%s", annotated_disassembly.c_str());

    ImGui::TableNextColumn();
    if (instructions[i].is_breakpoint) {
      if (ImGui::SmallButton(" X ")) {
        m_cpu_guest->breakpoint_remove(pc);
      }
    } else if (is_row_hovered) {
      if (ImGui::SmallButton("   ")) {
        m_cpu_guest->breakpoint_add(pc);
      }
    }

    ImGui::PopID();
  }

  ImGui::EndTable();
  ImGui::EndChild();
  ImGui::PopItemWidth();
}
}
