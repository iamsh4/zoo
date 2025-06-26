#include <imgui.h>

#include "gui/widget_memory_table.h"

namespace gui {

MemoryTableWidget::MemoryTableWidget(fox::MemoryTable *const memory,
                                     const u32 address_start,
                                     const u32 address_end)
  : m_memory(memory),
    m_address_start(address_start),
    m_address_end(address_end),
    m_hover_address(-1),
    m_edit_address(-1),
    m_edit_refocus(false)
{
  memset(m_data_input, 0, sizeof(m_data_input));
  memset(m_address_input, 0, sizeof(m_address_input));
}

MemoryTableWidget::~MemoryTableWidget()
{
  return;
}

static const ImVec4 SECTION_COLORS[] = {
  ImVec4(0.8f, 0.3f, 0.3f, 0.1f),
  ImVec4(0.3f, 0.8f, 0.3f, 0.1f),
  ImVec4(0.3f, 0.3f, 0.8f, 0.1f),

  ImVec4(0.8f, 0.8f, 0.3f, 0.1f),
  ImVec4(0.8f, 0.3f, 0.8f, 0.1f),
  ImVec4(0.3f, 0.8f, 0.8f, 0.1f),
};

/*!
 * @brief Add a named section to the memory table.
 *
 * @param name Name of the section.
 * @param start Start address of the section.
 * @param end End address of the section.
 */
void
MemoryTableWidget::add_named_section(const char *name, const u32 start, const u32 end)
{
  // static const u32 color = ImGui::GetColorU32(ImVec4(0.3f, 0.3f, 0.8f, 0.1f));
  static u32 color_index = 0;
  m_sections.push_back({ name, start, end, SECTION_COLORS[color_index] });
  color_index = (color_index + 1) % 6;
}

void
MemoryTableWidget::render()
{
  const u32 address_range = m_address_end - m_address_start;

  /* TODO Ensure round down to power of 2. */
  const unsigned address_digits = 8;
  const unsigned bytes_per_line = 32; /* XXX */

  const float glyph_width = ImGui::CalcTextSize("F").x;
  const float line_height = ImGui::GetTextLineHeight();
  const unsigned line_total_count =
    ((address_range + bytes_per_line - 1) / bytes_per_line);

  /* Header bar */

  ImGui::AlignTextToFramePadding();
  ImGui::PushItemWidth(ImGui::GetContentRegionAvailWidth() / 4);
  ImGui::Text("GOTO:");

  auto scroll_to = [&](u32 target) {
    target = std::min(std::max(target, m_address_start), m_address_end - 1);
    if (target >= 0 && target < address_range) {
      ImGui::BeginChild("##scrolling");
      ImGui::SetScrollFromPosY(ImGui::GetCursorStartPos().y +
                               (target / bytes_per_line) * ImGui::GetTextLineHeight());
      ImGui::EndChild();
      m_edit_address = target;
    }
  };

  ImGui::SameLine();
  const auto address_flags =
    ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue;
  if (ImGui::InputText("##addr", m_address_input, 32, address_flags)) {
    u32 target;
    if (sscanf(m_address_input, "%x", &target) == 1) {
      scroll_to(target);
    }
  }
  ImGui::PopItemWidth();

  // Sections dropdown
  ImGui::SameLine();
  if (ImGui::BeginCombo("##section", "Goto Section...")) {
    bool did_select = false;
    u32 target = 0;

    for (const auto &section : m_sections) {
      if (ImGui::Selectable(section.name.c_str())) {
        did_select = true;
        target = section.start;
      }
    }
    ImGui::EndCombo();

    if (did_select) {
      scroll_to(target);
    }
  }

  ImGui::Separator();

  /* Memory viewer */

  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));

  ImGui::BeginChild("##scrolling", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));
  if (!ImGui::BeginTable("table", 3)) {
    ImGui::EndChild();
    return;
  }

  ImGui::TableSetupColumn("address",
                          ImGuiTableColumnFlags_WidthFixed |
                            ImGuiTableColumnFlags_NoResize,
                          glyph_width * (address_digits + 1));
  ImGui::TableSetupColumn("value",
                          ImGuiTableColumnFlags_WidthFixed |
                            ImGuiTableColumnFlags_NoResize,
                          glyph_width * bytes_per_line * 3);
  ImGui::TableSetupColumn("ascii",
                          ImGuiTableColumnFlags_WidthFixed |
                            ImGuiTableColumnFlags_NoResize,
                          glyph_width * bytes_per_line);

  ImGuiListClipper clipper;
  clipper.Begin(line_total_count, line_height);
  clipper.Step();

  // static const ImU32 row_color1 = ImGui::GetColorU32(ImVec4(0.3f, 0.3f, 0.8f, 0.1f));
  // static const ImU32 row_color2 = ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
  static const ImVec4 address_color = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
  static const ImVec4 color_highlight(1.0f, 0.2f, 0.2f, 0.9f);
  static const ImVec4 color_nonzero(1.0f, 1.0f, 1.0f, 0.5f);
  static const ImVec4 color_zero(1.0f, 1.0f, 1.0f, 0.2f);
  bool is_row_hovered = false;
  for (int line_i = clipper.DisplayStart; line_i < clipper.DisplayEnd; line_i++) {
    const u32 address = line_i * bytes_per_line;

    /* Load all bytes required to render this row. RAM can only start / end on
     * a page boundary so as long as column count is a power of 2 it can't
     * partially overlap RAM / non-RAM memory ranges. */
    u8 row_data[bytes_per_line];
    const bool is_ram = m_memory->check_rom(address, bytes_per_line);
    if (is_ram) {
      memcpy(row_data, m_memory->root() + address, bytes_per_line);
    }

    const i64 highlight_address = m_edit_address >= 0 ? m_edit_address : m_hover_address;

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);

    ImVec4 row_color = ImVec4(0.3f, 0.3f, 0.3f, 0.1f);

    // Check if this address is in a named section
    for (const auto &section : m_sections) {
      if (address >= section.start && address < section.end) {
        row_color = section.color;
        break;
      }
    }
    if (highlight_address >= address && highlight_address < address + bytes_per_line) {
      row_color.w = 0.3f; 
    }
    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(row_color));

    ImGui::TextColored(address_color, "%0*x", address_digits, address);

    // See if we can provide a tooltip for this address
    if (ImGui::IsItemHovered()) {
      Section const *hovered_section = nullptr;
      for (const auto &section : m_sections) {
        if (address >= section.start && address < section.end) {
          hovered_section = &section;
          break;
        }
      }
      if (hovered_section != nullptr) {
        ImGui::BeginTooltip();
        ImGui::Text("%s: 0x%08x - 0x%08x",
                    hovered_section->name.c_str(),
                    hovered_section->start,
                    hovered_section->end);
        ImGui::EndTooltip();
      }
    }

    ImGui::TableNextColumn();
    for (unsigned j = 0; j < bytes_per_line; ++j) {
      const u32 byte_address = address + j;
      ImGui::PushID(byte_address * 2 + 0); /* XXX */
      if (m_edit_address == byte_address) {
        if (m_edit_refocus) {
          ImGui::SetKeyboardFocusHere();
          snprintf(m_data_input, sizeof(m_data_input), "%02X ", row_data[j]);
        }

        const auto cursor_callback = [](ImGuiInputTextCallbackData *const data) {
          int *const p_cursor_pos = (int *)data->UserData;
          if (!data->HasSelection()) {
            *p_cursor_pos = data->CursorPos;
          }
          return 0;
        };

        ImGui::PushItemWidth(glyph_width * 2);
        const ImGuiInputTextFlags input_flags =
          ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue |
          ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_NoHorizontalScroll |
          ImGuiInputTextFlags_AlwaysInsertMode | ImGuiInputTextFlags_CallbackAlways;
        int cursor_position = 0;
        if (ImGui::InputText("##data",
                             m_data_input,
                             4,
                             input_flags,
                             cursor_callback,
                             &cursor_position) ||
            cursor_position >= 2) {

          u8 new_val;
          u32 new32;
          sscanf(m_data_input, "%x", &new32);
          new_val = new32 & 0xff;
          memcpy((void *)(m_memory->root() + m_edit_address), &new_val, 1);

          m_edit_address++;
          m_edit_refocus = true;
        } else {
          if (!ImGui::IsItemActive()) {
            /* XXX */
            if (!m_edit_refocus) {
              m_edit_address = -1;
            }
          } else {
            m_edit_refocus = false;
          }
        }

        ImGui::PopItemWidth();

        ImGui::SameLine();
        ImGui::Text(" ");
      } else {
        char buffer[4];
        if (is_ram) {
          snprintf(buffer, sizeof(buffer), "%02X ", row_data[j]);
        } else {
          snprintf(buffer, sizeof(buffer), "?? ");
        }

        if (highlight_address == byte_address) {
          ImGui::TextColored(color_highlight, "%s", buffer);
        } else if (is_ram && row_data[j] != 0) {
          ImGui::TextColored(color_nonzero, "%s", buffer);
        } else {
          ImGui::TextColored(color_zero, "%s", buffer);
        }
      }

      if (ImGui::IsItemHovered()) {
        if (ImGui::IsMouseClicked(0)) {
          snprintf(m_data_input, sizeof(m_data_input), "%02X ", row_data[j]);
          m_edit_address = byte_address;
          m_edit_refocus = true;
        }

        m_hover_address = byte_address;
        is_row_hovered = true;
      }

      ImGui::PopID();
      ImGui::SameLine();
    }

    ImGui::TableNextColumn();
    for (unsigned j = 0; j < bytes_per_line; ++j) {
      const u32 byte_address = address + j;
      ImGui::PushID(byte_address * 2 + 1); /* XXX */
      char buffer[2] = { '?', 0 };
      if (is_ram) {
        if (row_data[j] >= 32 && row_data[j] < 128) {
          buffer[0] = row_data[j];
        } else {
          buffer[0] = '.';
        }
      }
      buffer[1] = 0;

      if (highlight_address == byte_address) {
        ImGui::TextColored(color_highlight, "%s", buffer);
      } else if (is_ram && row_data[j] != 0) {
        ImGui::TextColored(color_nonzero, "%s", buffer);
      } else {
        ImGui::TextColored(color_zero, "%s", buffer);
      }

      ImGui::PopID();
      ImGui::SameLine();
    }
  }
  clipper.End();

  if (!is_row_hovered) {
    m_hover_address = -1;
  }

  ImGui::EndTable();
  ImGui::EndChild();
  ImGui::PopStyleVar(3);
}
}
