#include <fmt/core.h>
#include <imgui.h>

#include "fox/jit/cache.h"
#include "shared/utils.h"
#include "guest/sh4/sh4_debug.h"
#include "gui/window_jit_workbench.h"

/* XXX */
#define AVERAGE_CYCLES_PER_INSTRUCTION 0.6f
#define NANOSECONDS_PER_CYCLE 5lu

namespace gui {

JitWorkbenchWindow::JitWorkbenchWindow(std::shared_ptr<ConsoleDirector> director)
  : Window("JIT Workbench"),
    m_director(director),
    m_sh4([](fox::ref<fox::jit::CacheEntry> input) {
      const cpu::SH4::BasicBlock *const ebb = (cpu::SH4::BasicBlock *)input.get();
      std::vector<std::string> lines;
      cpu::Debugger::disassemble(ebb->instructions(), lines);
      return lines;
    }),
    m_bytecode([](fox::ref<fox::jit::CacheEntry> input) {
      const cpu::SH4::BasicBlock *const ebb = (cpu::SH4::BasicBlock *)input.get();
      if (!ebb->m_bytecode) {
        return std::vector<std::string>({ "Not compiled" });
      }

      return splitlines(ebb->m_bytecode->disassemble());
    }),
    m_native([](fox::ref<fox::jit::CacheEntry> input) {
      const cpu::SH4::BasicBlock *const ebb = (cpu::SH4::BasicBlock *)input.get();
      if (!ebb->m_bytecode) {
        return std::vector<std::string>({ "Not compiled" });
      }

      return splitlines(ebb->m_native->disassemble());
    })
{
  return;
}

void
JitWorkbenchWindow::set_target(fox::ref<fox::jit::CacheEntry> target)
{
  m_target = target;
}

void
JitWorkbenchWindow::render()
{

  ImGui::SetNextWindowSize(ImVec2(1175, 600), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("JIT Workbench", NULL, ImGuiWindowFlags_NoScrollbar)) {
    ImGui::End();
    return;
  }

  if (!m_target.get()) {
    ImGui::Text("Nothing selected");
    ImGui::End();
    return;
  }

  ImGui::Text("Entry @ 0x%08X", m_target->virtual_address());

  ImGui::BeginTabBar("##backend");

  if (ImGui::BeginTabItem("SH4")) {
    m_sh4.set_target(m_target);
    m_sh4.render();
    ImGui::EndTabItem();
  }

  if (ImGui::BeginTabItem("SSA-IR")) {
    m_ir_analyzer.set_target(m_target);
    m_ir_analyzer.render();
    ImGui::EndTabItem();
  }

  if (ImGui::BeginTabItem("Bytecode")) {
    m_bytecode.set_target(m_target);
    m_bytecode.render();
    ImGui::EndTabItem();
  }

  if (ImGui::BeginTabItem("Native")) {
    m_native.set_target(m_target);
    m_native.render();
    ImGui::EndTabItem();
  }

  ImGui::EndTabBar();

  ImGui::End();
}

}
