#pragma once

#include "frontend/console_director.h"
#include "core/console.h"
#include "gui/window.h"
#include "gui/widget_cpu_stepper.h"
#include "gui/window_jit_workbench.h"
#include "shared/cpu.h"

#include <frontend/sdk_symbols.h>

namespace gui {

class CPUWindow : public Window {
public:
  CPUWindow(std::string_view name, std::shared_ptr<CPUWindowGuest> cpu_guest, JitWorkbenchWindow *workbench);

private:
  std::unordered_map<u32, const SDKSymbol *> m_sdk_symbols;
  const SDKSymbol *get_symbol_for_pc(u32 pc);

  std::shared_ptr<CPUWindowGuest> m_cpu_guest;
  JitWorkbenchWindow *const m_workbench;
  std::unique_ptr<CpuStepperWidget> m_cpu_stepper;
  u32 m_last_pc = 0;
  std::vector<u32> m_breakpoints;
  std::string m_window_name;

  void render_pen_disassembly(unsigned disassembly_lines);
  void render() override;
};

}
