#include "gui/widget_disassembly.h"

namespace gui {

DisassemblyWidget::DisassemblyWidget(const Disassembler disassembler)
  : m_disassembler(disassembler)
{
  return;
}

DisassemblyWidget::~DisassemblyWidget()
{
  return;
}

void
DisassemblyWidget::set_target(fox::ref<fox::jit::CacheEntry> target)
{
  if (target.get() == m_target.get()) {
    return;
  }

  m_target = target;
  if (target.get() == nullptr) {
    m_lines = std::vector<std::string>();
    return;
  }

  m_lines = m_disassembler(target);
}

void
DisassemblyWidget::render()
{
  const ImGuiWindowFlags window_flags = ImGuiWindowFlags_HorizontalScrollbar;
  ImGui::BeginChild("##scrollarea", ImVec2(0, 0), true, window_flags);

  for (const std::string &line : m_lines) {
    ImGui::TextUnformatted(line.c_str());
  }

  ImGui::EndChild();
}

}
