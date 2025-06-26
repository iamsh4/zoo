#pragma once

#include <vector>

#include "frontend/console_director.h"
#include "core/console.h"
#include "guest/sh4/sh4_jit.h"
#include "gui/window.h"
#include "gui/widget_ir_analysis.h"
#include "gui/widget_disassembly.h"

struct ImFont;

namespace gui {

class JitWorkbenchWindow : public Window {
public:
  JitWorkbenchWindow(std::shared_ptr<ConsoleDirector> director);

  void set_target(fox::ref<fox::jit::CacheEntry> target);

private:
  std::shared_ptr<ConsoleDirector> m_director;

  IrAnalysisWidget m_ir_analyzer;
  DisassemblyWidget m_sh4;
  DisassemblyWidget m_bytecode;
  DisassemblyWidget m_native;

  fox::ref<fox::jit::CacheEntry> m_target;

  void draw_workbench();
  void render() override;
};

}
