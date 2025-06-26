#include <algorithm>
#include <fmt/core.h>
#include <imgui.h>

#include "guest/sh4/sh4.h"
#include "guest/sh4/sh4_jit.h"
#include "gui/widget_ir_analysis.h"

namespace gui {

IrAnalysisWidget::IrAnalysisWidget()
{
  return;
}

IrAnalysisWidget::~IrAnalysisWidget()
{
  return;
}

void
IrAnalysisWidget::set_target(fox::ref<fox::jit::CacheEntry> target)
{
  m_target = target;
}

const auto DARK_GREY = ImVec4(0.3, 0.3, 0.3, 1);
const auto BLUE = ImVec4(0.2, 0.2, 0.9, 1);
const auto GREEN = ImVec4(0.2, 0.9, 0.2, 1);
const auto RED = ImVec4(0.9, 0.2, 0.2, 1);

const auto ConstantColor = BLUE;
const auto DefaultRegisterColor = RED;
const auto HighlightedRegister = GREEN;

static const char *padding_string_fmt[] = {
  "%s",   "%-1s", "%-2s", "%-3s",  "%-4s",  "%-5s",  "%-6s",
  "%-7s", "%-8s", "%-9s", "%-10s", "%-11s", "%-12s",
};

struct RegisterIntervals {
  static constexpr u8 n_lanes = 32;
  using LaneAssignments = std::array<int, n_lanes>;

  RegisterIntervals(const fox::ir::Instructions &instructions);

  void print();

  // One LaneAssignments per instruction, each indicating which registers are in use.
  std::vector<LaneAssignments> assignments;

  std::vector<std::vector<u32>> instruction_to_reg_start;
  std::unordered_map<u32, u32> reg_to_last_use;
  u32 max_width;
};

RegisterIntervals::RegisterIntervals(const fox::ir::Instructions &instructions)
{
  max_width = 0;
  instruction_to_reg_start.resize(instructions.size());

  // Compute first time we see a register as an output, latest time we see it as a source.
  for (auto it = instructions.begin(); it != instructions.end(); ++it) {
    if (it->result_count() > 0) {
      assert(it->result_count() == 1);
      instruction_to_reg_start[it.index()].push_back(it->result(0).register_index());
    }

    for (size_t i = 0; i < it->source_count(); ++i) {
      if (it->source(i).is_register()) {
        reg_to_last_use[it->source(i).register_index()] = it.index();
      }
    }
  }

  // Any register which is never used is considered to have lasted one instruction
  for (u32 i = 0; i < instructions.size(); ++i) {
    for (const auto reg : instruction_to_reg_start[i]) {
      if (reg_to_last_use[reg] == 0) {
        reg_to_last_use[reg] = i + 1;
      }
    }
  }

  // Track which which temps/regs are in use by SSA regs
  LaneAssignments active_set = std::array<int, n_lanes>();
  active_set.fill(-1);

  for (u32 i = 0; i < instructions.size(); ++i) {
    // See if we remove them from the active set.
    for (u32 j = 0; j < active_set.size(); ++j) {
      if (active_set[j] >= 0) {
        if (reg_to_last_use[active_set[j]] == i) {
          active_set[j] = -1;
        }
      }
    }

    // See what we add from this instruction to the active set.
    for (const auto reg : instruction_to_reg_start[i]) {
      for (u32 j = 0; j < n_lanes; ++j) {
        max_width = std::max(max_width, j + 1);
        if (active_set[j] == -1) {
          active_set[j] = reg;
          break;
        }
      }
    }

    assignments.push_back(active_set);
  }
}

void
RegisterIntervals::print()
{
  for (u32 i = 0; i < assignments.size(); ++i) {
    for (u32 j = 0; j < max_width; ++j) {
      printf("%c", assignments[i][j] >= 0 ? '*' : '.');
    }

    printf("\n");
  }
}

////////////////////////////////////////////////////////////

struct OperandRenderer {
  const fox::ir::Operand &op;
  int &selected_register;
  int padding = 10;

  void render();
};

void
OperandRenderer::render()
{
  char buff[64];
  ImVec4 color;

  if (!op.is_valid()) {
    snprintf(buff, sizeof(buff), " ");
  } else if (op.is_constant()) {
    color = ConstantColor;
    switch (op.type()) {
      case fox::ir::Type::Integer32:
        if (u32 val = op.value().u32_value; val < 0x10000000) {
          snprintf(buff, sizeof(buff), "%d", op.value().u32_value);
        } else {
          snprintf(buff, sizeof(buff), "0x%08x", op.value().u32_value);
        }
        break;

      case fox::ir::Type::Integer16:
        snprintf(buff, sizeof(buff), "%d", op.value().u16_value);
        break;

      case fox::ir::Type::Integer8:
        snprintf(buff, sizeof(buff), "%d", op.value().u8_value);
        break;

      case fox::ir::Type::Bool:
        snprintf(buff, sizeof(buff), "%s", op.value().bool_value ? "true" : "false");
        break;

      case fox::ir::Type::Float32:
        snprintf(buff, sizeof(buff), "%8f", op.value().f32_value);
        break;

      case fox::ir::Type::Float64:
        snprintf(buff, sizeof(buff), "%8lf", op.value().f64_value);
        break;

      case fox::ir::Type::Integer64:
        strcpy(buff, fmt::format("{:x}", op.value().u64_value).c_str());
        break;

      case fox::ir::Type::HostAddress: {
        snprintf(buff, sizeof(buff), "*%p", op.value().hostptr_value);
        break;
      }

      default:
        snprintf(buff, sizeof(buff), "?????");
        break;
    }
  } else {
    color = DefaultRegisterColor;
    snprintf(buff, sizeof(buff), "$%d ", op.register_index());
  }

  if (selected_register >= 0) {
    if (op.is_valid() && op.is_register() &&
        int(op.register_index()) == selected_register) {
      color = DefaultRegisterColor;
    } else {
      color = DARK_GREY;
    }
  }

  ImGui::TextColored(color, padding_string_fmt[padding], buff);
}

struct IRPassRenderer {
  const fox::ir::Instructions instructions;
  RegisterIntervals liveness_data;
  int selected_register;

  IRPassRenderer(const fox::ir::Instructions &input)
    : instructions(input),
      liveness_data(input),
      selected_register(-1)
  {
    return;
  }

  void render();
  void draw_vertical_line(ImVec2 start, float width, float height, ImU32 color);
};

void
IRPassRenderer::draw_vertical_line(ImVec2 start, float width, float height, ImU32 color)
{
  auto *draw_list = ImGui::GetWindowDrawList();
  ImVec2 p1 = start;
  ImVec2 p2 = ImVec2(p1.x + width, p1.y + height);
  draw_list->AddRectFilled(p1, p2, color);
}

// Draw dissasembly for a particular pass
void
IRPassRenderer::render()
{
  const float line_height = ImGui::GetTextLineHeight();
  std::vector<ImVec2> cursors;

  int highlighted_register = -1;

  for (auto it = instructions.begin(); it != instructions.end(); ++it) {
    const fox::ir::Instruction &ins = *it;

    // Instruction number
    ImGui::TextColored(DARK_GREY, "[%04d] ", it.index());
    ImGui::SameLine();

    // Remember this location for any later drawing. Done this way because
    // calculating postions based on line_heights/spacing etc is error-prone.
    // Also, create some horizontal space for later drawing here.
    cursors.push_back(ImGui::GetCursorScreenPos());
    ImGui::Dummy(ImVec2(32, line_height));
    ImGui::SameLine();

    // Instruction result register
    if (ins.result_count() > 0 && ins.result(0).is_register()) {
      assert(ins.result_count() == 1);
      OperandRenderer { .op = ins.result(0),
                        .selected_register = selected_register,
                        .padding = 5 }
        .render();
      if (ImGui::IsItemHovered()) {
        highlighted_register = ins.result(0).register_index();
      }

      if (ImGui::IsItemClicked()) {
        selected_register =
          (selected_register == -1) ? ins.result(0).register_index() : -1;
      }

      ImGui::SameLine();
      ImGui::Text("<-");
    } else {
      ImGui::Text("        ");
    }
    ImGui::SameLine();

    // Instruction opcode
    {
      static char buff[64];
      snprintf(
        buff, sizeof(buff), "%s.%s", fox::ir::opcode_to_name(ins.opcode()), fox::ir::type_to_name(ins.type()));
      ImGui::Text("%-12s", buff);
      ImGui::SameLine();
    }

    // Instruction sources
    for (size_t i = 0; i < ins.source_count(); ++i) {
      const fox::ir::Operand &operand = ins.source(i);
      if (!operand.is_valid()) {
        break;
      }

      OperandRenderer { .op = operand,
                        .selected_register = selected_register,
                        .padding = 0 }
        .render();

      if (ImGui::IsItemHovered() && operand.is_register()) {
        highlighted_register = operand.register_index();
      }

      if (ImGui::IsItemClicked() && operand.is_register()) {
        selected_register = (selected_register == -1) ? operand.register_index() : -1;
      }

      ImGui::SameLine();
    }

    ImGui::NewLine();
  }

  // Draw vertical lines based on liveness of registers.
  for (u32 line = 0; line < instructions.size(); ++line) {
    for (const auto reg : liveness_data.instruction_to_reg_start[line]) {
      const int last_line = liveness_data.reg_to_last_use[reg];
      if (last_line == int(line)) {
        continue;
      }

      const auto lanes = liveness_data.assignments[line];
      int lane = std::distance(lanes.begin(), std::find(lanes.begin(), lanes.end(), reg));

      ImVec2 start = cursors[line];
      start.y += line_height * 0.6f;

      ImVec2 end = cursors[last_line];
      end.y += line_height * 0.4f;

      const float width = 1.f;
      const float margin = 2.f;
      const float height = end.y - start.y;

      ImU32 color = 0xFFFFFFFF;
      if (highlighted_register >= 0) {
        color = (highlighted_register == int(reg))
                  ? ImGui::GetColorU32(HighlightedRegister)
                  : ImGui::GetColorU32(DARK_GREY);
      }

      if (selected_register >= 0) {
        color = (selected_register == int(reg)) ? ImGui::GetColorU32(HighlightedRegister)
                                                : ImGui::GetColorU32(DARK_GREY);
      }

      draw_vertical_line(
        { start.x + lane * (width + margin), start.y }, width, height, color);
    }
  }
}

void
IrAnalysisWidget::render()
{
  if (!m_target.get() || !m_target->is_compiled()) {
    return;
  }

  // Buttons for unoptimized / optimized IR
  enum class OptimizationMode
  {
    Nothing,
    NoOptimization,
    Optimized
  };

  static OptimizationMode current_mode = OptimizationMode::NoOptimization;
  OptimizationMode new_mode = OptimizationMode::Nothing;
  if (ImGui::Button("No Opt")) {
    new_mode = OptimizationMode::NoOptimization;
  }
  ImGui::SameLine();
  if (ImGui::Button("Full Opt")) {
    new_mode = OptimizationMode::Optimized;
  }

  const ImGuiWindowFlags window_flags = ImGuiWindowFlags_HorizontalScrollbar;
  ImGui::BeginChild("##scrollarea", ImVec2(0, 0), true, window_flags);

  // TODO : Properly persist passes
  static cpu::SH4::BasicBlock const *last_ebb = nullptr;
  static std::unique_ptr<IRPassRenderer> pass_renderer;

  if (new_mode != OptimizationMode::Nothing && new_mode != current_mode) {
    current_mode = new_mode;
    last_ebb = nullptr; // Force re-generating the IRPassRenderer
  }

  const cpu::SH4::BasicBlock *const ebb =
    (cpu::SH4::BasicBlock *)m_target.get();
  if (ebb != last_ebb) {
    fox::ir::ExecutionUnit eu = ebb->m_unit->copy();
    if (current_mode == OptimizationMode::Optimized) {
      eu = cpu::optimize(eu);
    }

    pass_renderer = std::make_unique<IRPassRenderer>(eu.instructions());
    last_ebb = ebb;
  }

  pass_renderer->render();
  ImGui::EndChild();
}

}
